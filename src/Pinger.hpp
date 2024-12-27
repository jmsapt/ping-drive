#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <tins/icmp.h>
#include <tins/ip_address.h>
#include <tins/ipv6_address.h>
#include <tins/packet_sender.h>

#include "lib/Channel.hpp"

using IP = Tins::IPv4Address;

class Pinger {
    using uid_t   = std::uint16_t;
    using WriteCb = std::function<void(int)>;
    using ReadCb  = std::function<void(int, std::span<const std::uint8_t>)>;

    // packet duplications
    static constexpr size_t ICMPHeaderSize = 8;

    static constexpr int Redundancy   = 1;
    static constexpr size_t BlockSize = 1024;
    static constexpr size_t NumBlocks = 1024 * 2;
    // static constexpr size_t PingPayloadSize = sizeof(uid_t) + BlockSize;

  public:
    struct ReadRequest {
        size_t block_num;
        size_t length;
        ReadCb cb;
    };

    // struct WriteRequest {
    //     size_t offset;
    //     std::array<std::uint8_t, BlockSize> buffer;
    //     WriteCb cb;
    // };

    // using Request = std::variant<ReadRequest, WriteRequest>;

    /// Loads yeeter with the vector of IPs to yeet at
    Pinger(const std::string &ipFilePath, SenderChannel<ReadRequest> rx,
           ReceiverChannel<ReadRequest> tx);
    ~Pinger();

    size_t blocksize();
    void write(size_t offset, std::span<const std::uint8_t> data, WriteCb cb);
    void read(size_t offset, size_t length, ReadCb cb);

    /// Start main loop (blocking)

    // /// Registers a read request, executes the cb on when served
    // void read(size_t offset, size_t length, void *buffer, WriteCb cb);

    // /// Registers a write request, executes the cb on when served
    // void write(size_t offset, size_t length, void *buffer, WriteCb cb);

  private:
    void run();
    // void ping_write(WriteRequest &req);
    // void ping_read(ReadRequest &req);
    void ping_send(std::uint16_t block_num, std::span<const std::uint8_t> payload,
           bool reuse_id = true, int n_sends = Redundancy);

    std::optional<Tins::ICMP> handle_packet(const Tins::ICMP &packet);

    // TODO make this cleaner, rn this is only user by the other thread
    IP next_ip();
    std::thread sniffer_loop;

    const std::vector<Tins::IPv4Address> ip_buffer{
        {"142.250.191.78"},
    };
    const IP LocalIp{"192.168.157.236"};

    SenderChannel<ReadRequest> tx;
    ReceiverChannel<ReadRequest> rx;

    // std::vector<ReadRequest> read_requests; // only buffer read requests
    std::array<uid_t, NumBlocks> blocks {}; // block number -> uid
    // int sock;

    Tins::PacketSender sender;
    std::array<std::uint8_t, ICMPHeaderSize + BlockSize> buffer;
};
