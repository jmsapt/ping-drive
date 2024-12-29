#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <ranges>
#include <regex>
#include <span>
#include <string>
#include <thread>

#include "lib/Channel.hpp"
#include <tins/tins.h>

using IP = Tins::IPv4Address;

using WriteCb = std::function<void(int)>;
using ReadCb  = std::function<void(int, std::span<const std::uint8_t>)>;

struct ReadRequest {
    size_t block_num;
    size_t length;
    ReadCb cb;
};

template <size_t BlockSize = 1024UL, size_t NumBlocks = 1024UL,
          size_t Redundancy = 3>
class Pinger {
    using uid_t = std::uint16_t;

    // packet duplications
    static constexpr size_t ICMPHeaderSize = 8;

  public:
    /// Loads yeeter with the vector of IPs to yeet at
    Pinger(const std::string &ipFilePath, SenderChannel<ReadRequest> tx,
           ReceiverChannel<ReadRequest> rx)
        : tx(std::move(tx)), rx(std::move(rx)) {
        std::ifstream ips(ipFilePath);
        std::string line;

        std::regex pattern("\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}\\.\\d{1,3}");
        while (std::getline(ips, line)) {
            break;
            std::cout << "read line" << std::endl;
            std::smatch match;
            if (!std::regex_search(line, match, pattern))
                continue;

            ip_buffer.push_back({match[0].str()});
        }
        std::cout << "Registered " << ip_buffer.size() << " IPs" << std::endl;

        sniffer_loop = std::thread([this]() { run(); });

        std::array<std::uint8_t, BlockSize> null_bytes{};
        ping_send(0, std::span(null_bytes), false, Redundancy);
    }

    static constexpr size_t blocksize() { return BlockSize; }
    static constexpr size_t num_blocks() { return NumBlocks; }

    void write(size_t offset, std::span<const std::uint8_t> data, WriteCb cb) {
        auto lgaurd = std::lock_guard(lock);
        if (offset % BlockSize != 0) {
            std::cerr << "Unaligned write request at " << offset
                      << ", with size " << data.size() << std::endl;
            cb(-1);
            return;
        }

        if (data.size() != BlockSize) {
            std::cerr << "Partial block write request at " << offset
                      << ", with size " << data.size() << std::endl;
            cb(-2);
            return;
        }

        auto block_num = offset / BlockSize;
        ping_send(block_num, data, false); // ask for a new block id
        cb(0);
    }

    void read(size_t offset, size_t length, ReadCb cb) {
        auto lgaurd = std::lock_guard(lock);

        if (offset % BlockSize != 0) {
            std::cerr << "Unaligned read request at " << offset
                      << ", with size " << length << std::endl;
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

  private:
    void run() {
        // start sniffer (sniffer does the juggling)
        Tins::SnifferConfiguration config;
        config.set_filter("icmp[0] == 0");
        config.set_buffer_size(50 * 1024 * 1024);
        config.set_timeout(5);
        // config.set_immediate_mode(true);
        Tins::Sniffer sniffer("any", config);

        std::vector<ReadRequest> requests;
        requests.reserve(100);

        auto handler = [&, this](const Tins::PDU &pdu) -> bool {
            auto lgaurd            = std::lock_guard(lock);
            const Tins::IP &ip     = pdu.rfind_pdu<Tins::IP>();
            const Tins::ICMP &icmp = ip.rfind_pdu<Tins::ICMP>();

            auto block_num = icmp.sequence();
            auto id        = icmp.id();

            // empty channel, moving into thread local buffer
            while (auto req = rx.try_receive()) {
                std::cout << "==> read request" << std::endl;
                if (blocks[req->block_num] == 0) {
                    std::array<std::uint8_t, BlockSize> tmp{};
                    req->cb(0, std::span(tmp));
                }
                else {
                    requests.push_back(std::move(*req));
                }
            }

            // extract payload
            const Tins::RawPDU &payload_wrapper =
                icmp.rfind_pdu<Tins::RawPDU>();
            std::span<const std::uint8_t> payload{
                payload_wrapper.payload().begin(),
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
                it->cb(0, payload);
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

    void ping_send(std::uint16_t block_num,
                   std::span<const std::uint8_t> payload, bool reuse_id = false,
                   int n_sends = 1) {
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
            std::cout << time << " Sent to IP: " << dst.to_string() << '(' << id
                      << ") " << '(' << block_num << ')' << std::endl;
            auto ip = Tins::IP(dst, LocalIp);

            ip.ttl(100);
            ip /= icmp;

            sender.send(ip);
        }

        if (!reuse_id)
            blocks[block_num] = id;

        std::this_thread::sleep_for(std::chrono::microseconds{100});
    }

    std::optional<Tins::ICMP> handle_packet(const Tins::ICMP &packet);

    // TODO make this cleaner, rn this is only user by the other thread
    IP next_ip() {
        static size_t ptr{0};
        auto tmp = ip_buffer[ptr];
        ptr      = (ptr + 1) % ip_buffer.size();
        return tmp;
    }
    std::thread sniffer_loop;

    std::vector<Tins::IPv4Address> ip_buffer{
        {"142.250.191.78"},
        {"192.0.3.254"},
    };
    const IP LocalIp{"192.168.178.236"};

    SenderChannel<ReadRequest> tx;
    ReceiverChannel<ReadRequest> rx;

    std::mutex lock;
    std::array<uid_t, NumBlocks> blocks{}; // block number -> uid

    Tins::PacketSender sender;
    std::array<std::uint8_t, ICMPHeaderSize + BlockSize> buffer;
};
