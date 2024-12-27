#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <libnbd.h>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <stdlib.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace std;
static constexpr uint32_t Magic = 0x25609513;

struct NbdHandshakeClient {
    uint32_t magic;
    uint32_t version = 0x100;
    uint8_t _reserved[16];
} __attribute__((packed));

struct NbdHandshakeServer {
    uint32_t magic;
    uint32_t version = 0x100;
    uint8_t _reserved[16];
    uint32_t blocksize = 1024;
} __attribute__((packed));

struct NbdRequest {
    uint32_t magic;
    uint8_t type;
    uint8_t reserved;
    uint16_t flags;
    uint32_t handle;
    uint32_t length;
    uint64_t offset;
    std::uint8_t _reserved[4];

    enum class Type : uint8_t {
        Read       = 0x00,
        Write      = 0x01,
        Disconnect = 0x04,
        Flush      = 0x08,
    };
} __attribute__((packed));

struct Client {
    int sock;
    struct sockaddr_in addr;

    struct sockaddr *addr_ptr{reinterpret_cast<struct sockaddr *>(&addr)};
    socklen_t addrlen{sizeof(struct sockaddr_in)};

    void serve() {
        NbdHandshakeClient handshake;
        int n = ::recvfrom(sock, &handshake, sizeof(handshake), 0, addr_ptr,
                           &addrlen);

        // incoming handshake
        if (n != sizeof(handshake) || handshake.magic != Magic)
            return;

        // outgoing handshake
        handshake = {Magic};
        n = sendto(sock, &handshake, sizeof(handshake), 0, addr_ptr, addrlen);
        if (n != sizeof(handshake))
            return;

        // request event loop
        NbdRequest request;
        while ((n = recvfrom(sock, &request, sizeof(request), 0, addr_ptr,
                             &addrlen) == sizeof(NbdRequest))) {
            std::array<uint8_t, 2048> buffer;
            if (request.magic != Magic)
                return;

            auto type{static_cast<NbdRequest::Type>(request.type)};
            switch (type) {
            case NbdRequest::Type::Read:
                // TODO
                break;
            case NbdRequest::Type::Write:
                // TODO
                break;
            case NbdRequest::Type::Flush:
                // placebo, do nothing
                break;
            case NbdRequest::Type::Disconnect:
                return;
            }
        }
    };

    ~Client() { close(sock); }
};

class Listener {
    int port;
    int sock;

  public:
    Listener(int port) : port(port) {
        int sockfd;
        struct sockaddr_in server_addr;
        const char *server_ip = "127.0.0.1"; // Change to "0.0.0.0" for
                                             // INADDR_ANY or use specific IP

        // Create socket
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw std::runtime_error("Failed to create socket");
        }

        server_addr.sin_family      = AF_INET;
        server_addr.sin_port        = htons(port);
        server_addr.sin_addr.s_addr = INADDR_ANY;

        if (bind(sock, (struct sockaddr *)&server_addr,
                 sizeof(struct sockaddr_in)) < 0) {
            close(sockfd);
            throw std::runtime_error("Failed to bind");
        }

        if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
            close(sockfd);
            throw std::runtime_error(
                "Invalid address or address not supported");
        }

        if (listen(sockfd, 0) < 0) {
            close(sockfd);
            throw std::runtime_error("Listen failed");
        }
    }

    std::optional<Client> accept() {
        struct sockaddr_in addr {};
        socklen_t addrlen = sizeof(struct sockaddr_in);

        int res = ::accept(sock, reinterpret_cast<struct sockaddr *>(&addr),
                           &addrlen);
        if (res < 0) {
            return std::nullopt;
        }
        else {
            return std::optional<Client>({res, addr});
        }
    };

    ~Listener() { close(sock); }
    friend Client;
};

int main(int argc, char *argv[]) {
    int port{9999};
    if (argc == 2) {
        port = atoi(argv[1]);
    }

    Listener listener(port);
    while (auto x = listener.accept()) {
        auto client = make_unique<Client>(std::move(*x));

        std::thread client_thread([&client]() {
            auto local = std::move(client);
            local->serve();
        });

        // leak the thread, she'll be aight
    };
}
