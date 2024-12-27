#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <thread>

#include <sstream>
#include <tins/icmp.h>
#include <tins/ip_address.h>
#include <tins/packet_sender.h>
#include <tins/rawpdu.h>
#include <tins/tins.h>
#include <unistd.h>

#include <arpa/inet.h>
using namespace std;
std::atomic<bool> open{true};

constexpr int threshold{200};

// every `factor` pings, sleep for `throttle`
constexpr std::chrono::microseconds throttle{1'000};
constexpr long factor{50};

int id{0};
int seq{0};

const Tins::IPv4Address host{"192.168.0.123"};

std::chrono::system_clock::time_point get_time(const std::string &payload) {
    std::chrono::system_clock::time_point tp;
    std::istringstream(payload) >> std::chrono::parse("%F %T", tp);
    return tp;
}

void increment_ip(Tins::IPv4Address &addr) {
    std::uint32_t value = addr;
    value               = ntohl(value);
    addr                = Tins::IPv4Address(htonl(++value));
};

void listener(const std::string &path) {
    Tins::SnifferConfiguration config;
    config.set_filter("icmp[0] == 0");
    config.set_immediate_mode(true);
    config.set_timeout(2);
    Tins::Sniffer sniffer("any", config);

    std::ofstream output(path);
    long counter = 0;

    auto handle = [&](const Tins::PDU &pdu) -> bool {
        counter++;

        const Tins::IP &ip     = pdu.rfind_pdu<Tins::IP>();
        const Tins::ICMP &icmp = ip.rfind_pdu<Tins::ICMP>();

        auto b = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(icmp.id()) >> 8) & 0xff);

        // cout << "Heard response " << counter << endl;
        if (b != id)
            return true;

        // extract payload
        const Tins::RawPDU &payload_wrapper = icmp.rfind_pdu<Tins::RawPDU>();
        std::string payload{payload_wrapper.payload().begin(),
                            payload_wrapper.payload().end()};
        auto now  = std::chrono::system_clock::now();
        auto then = get_time(payload);

        auto ms = std::chrono::duration_cast<chrono::milliseconds>(now - then);
        if (ms > std::chrono::milliseconds{threshold}) {
            cout << ip.src_addr() << "\t=> " << ms << std::endl;
            output << ip.src_addr() << " " << ms << std::endl;
        }

        // if (counter % 100) {
        //     std::cout << "Pings received " << counter << std::endl;
        // }

        return true;
    };

    cout << "Listening..." << endl;
    sniffer.sniff_loop(handle);
    cout << "Stopped listening..." << endl;
}

void sender(Tins::IPv4Address start, Tins::IPv4Address stop) {
    // number of sends before we recheck the flag
    Tins::PacketSender sender;
    cout << "Sending..." << endl;
    long count{0};

    for (auto dst = start;
         static_cast<std::uint32_t>(dst) != static_cast<std::uint32_t>(stop);
         increment_ip(dst)) {
        ++count;

        std::stringstream ss;

        Tins::IP ip{dst, host};
        // ip.ttl(64);
        // ip.id(0);

        Tins::ICMP icmp;
        icmp.type(Tins::ICMP::ECHO_REQUEST);
        icmp.id(id << 8);
        icmp.sequence(seq << 8);

        auto now = chrono::system_clock::now();
        ss << now;

        auto payload =
            Tins::RawPDU{reinterpret_cast<std::uint8_t *>(ss.str().data()),
                         static_cast<std::uint32_t>(ss.str().size())};

        icmp /= payload;
        ip /= icmp;

        sender.send(ip);
        if (count % factor == 0) {
            std::this_thread::sleep_for(throttle);
        }
    }
    cout << "Stopped sending..." << endl;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        cerr << "Invalid usage: <output file> <start ip> <end ip>" << '\n'
             << "Range is [start ip, end id)" << endl;
        return 1;
    }

    std::string path{argv[1]};
    Tins::IPv4Address start{argv[2]};
    Tins::IPv4Address stop{argv[3]};

    auto b = static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(start) >> 0) & 0xff);
    id  = b;
    seq = b;
    cout << "start stop " << start << " " << stop << " : " << b << endl;
    if (start > stop) {
        cerr << "End IP must be greater than Start IP" << endl;
        return 1;
    }

    std::thread rx(listener, path);
    std::thread tx(sender, start, stop);

    tx.join();
    // rx.join();
    sleep(3);

    return 0;
}
