#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <sys/types.h>
#include <tins/icmp.h>
#include <tins/ip_address.h>
#include <tins/rawpdu.h>
#include <tins/sniffer.h>
#include <tins/tins.h>
#include <unistd.h>

#include "Pinger.hpp"
#include <chrono>

size_t Pinger::blocksize() { return BlockSize; }

void print_hex(const std::vector<uint8_t> &data) {
    for (size_t i = 0; i < data.size(); ++i) {
        // Print each byte in hexadecimal format
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)data[i] << " ";

        // Print a newline after every 16 bytes (optional)
        if ((i + 1) % 16 == 0) {
            std::cout << std::endl;
        }
    }

    // Print a final newline
    std::cout << std::endl;
}

Pinger::Pinger(const std::string &ipFilePath, SenderChannel<ReadRequest> tx,
               ReceiverChannel<ReadRequest> rx)
    : tx(std::move(tx)), rx(std::move(rx)) {
    sniffer_loop = std::thread([this]() { run(); });
}

void Pinger::ping_send(std::uint16_t block_num,
                       std::span<const std::uint8_t> payload, bool reuse_id,
                       int n_sends) {
    auto prev_id = blocks[block_num];
    uid_t id     = prev_id;
    if (!reuse_id) {
        id = prev_id == std::numeric_limits<uid_t>::max() ? 1 : prev_id + 1;
    }

    // build packet
    Tins::ICMP icmp;
    icmp.type(Tins::ICMP::ECHO_REQUEST);
    icmp.sequence(block_num);
    icmp.id(id);
    icmp /= Tins::RawPDU{payload.begin(), payload.end()};

    // write packet n times
    for (auto _ : std::ranges::iota_view{0, n_sends}) {
        auto dst  = next_ip();
        auto time = std::chrono::system_clock::now();
        std::cout << time << "Sent to IP: " << dst.to_string() << std::endl;
        auto ip = Tins::IP(dst, LocalIp);
        ip.ttl(100);
        ip /= icmp;

        sender.send(ip);
    }

    blocks[block_num] = id;
}

// Blocking call that bounces the packtes (run on seperate thread)
void Pinger::run() {
    // start sniffer (sniffer does the juggling)
    Tins::SnifferConfiguration config;
    config.set_filter("icmp[0] == 0");
    config.set_immediate_mode(true);
    Tins::Sniffer sniffer("any", config);

    std::vector<ReadRequest> requests;
    requests.reserve(100);

    auto handler = [&](const Tins::PDU &pdu) -> bool {
        // empty channel, moving into thread local buffer
        while (auto req = rx.try_receive()) {
            requests.push_back(std::move(*req));
        }

        const Tins::IP &ip     = pdu.rfind_pdu<Tins::IP>();
        const Tins::ICMP &icmp = ip.rfind_pdu<Tins::ICMP>();

        auto block_num = icmp.sequence();
        auto id        = icmp.id();

        // extract payload
        const Tins::RawPDU &payload_wrapper = icmp.rfind_pdu<Tins::RawPDU>();
        std::span<const std::uint8_t> payload{payload_wrapper.payload().begin(),
                                              payload_wrapper.payload().end()};

        // if (icmp.type() != Tins::ICMP::ECHO_REPLY)
        //     return true;

        if (payload.size() == 69) {
            auto time = std::chrono::system_clock::now();
            std::cout << time << " ==> seq " << icmp.sequence() << " id"
                      << icmp.id() << std::endl;
        }

        // drop packet if it is stale
        if (block_num > blocks.size() || id != blocks[block_num])
            return true;

        // drop if partial payload
        if (payload.size() != BlockSize)
            return true;

        // check requests
        auto predicate = [&block_num](const ReadRequest &x) {
            return x.block_num == block_num;
        };
        auto it = std::find_if(requests.begin(), requests.end(), predicate);
        if (it != requests.end()) {

            // found requested packet, do something
            it->cb(BlockSize, payload);
            requests.erase(it);
        }

        // bounce the packet back ntimes
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ping_send(block_num, payload, false, Redundancy);
        return true;
    };

    std::cout << "Sniffer started..." << std::endl;
    sniffer.sniff_loop(handler);
    std::cout << "Sniffer stopped..." << std::endl;
}

// TODO fix this type alias
IP Pinger::next_ip() {
    static size_t ptr{0};
    auto tmp = ip_buffer[ptr];
    ptr      = (ptr + 1) % ip_buffer.size();
    return tmp;
}

void Pinger::write(size_t offset, std::span<const std::uint8_t> data,
                   WriteCb cb) {
    if (offset % BlockSize != 0) {
        std::cerr << "Unaligned write request at " << offset << ", with size "
                  << data.size() << std::endl;
        cb(-1);
    }

    if (data.size() != BlockSize) {
        std::cerr << "Partial block write request at " << offset
                  << ", with size " << data.size() << std::endl;
        cb(-2);
    }

    auto block_num = offset / BlockSize;
    ping_send(block_num, data, false); // ask for a new block id
    cb(data.size());
}

void Pinger::read(size_t offset, size_t length, ReadCb cb) {
    if (offset % BlockSize != 0) {
        std::cerr << "Unaligned read request at " << offset << ", with size "
                  << length << std::endl;
        cb(-1, {});
    }

    if (length != BlockSize) {
        std::cerr << "Partial block read request at " << offset
                  << ", with size " << length << std::endl;
        cb(-2, {});
    }

    auto block_num = offset / BlockSize;
    ReadRequest req{block_num, length, cb};
    tx.send(std::move(req));
}

Pinger::~Pinger() {}

// void Pinger::ping_read(size_t offset, size_t length, void *buffer,
// WriteCb cb) {}
