#include <algorithm>
#include <cstring>
#include <iostream>
#include <libnbd.h>
#include <semaphore>
#include <span>
#include <unistd.h>

#include "Pinger.hpp"
#include "lib/Channel.hpp"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Error, usage: <IP file>" << std::endl;
        return 1;
    }
    std::string ip_file(argv[1]);


    nbd_handle *nbd = nbd_create();
    std::binary_semaphore wait{false};

    if (!nbd) {
        std::cerr << "Failed to create nbd handle\n";
        return 1;
    }

    std::cout << "Victim file " << ip_file << std::endl;
    auto [rx, tx] = create_channel<ReadRequest>();

    auto pinger = Pinger{ip_file, std::move(tx), std::move(rx)};

    // auto _ = std::thread([&] { pinger.run(); });
    std::string msg;
    std::cout << "Fresh block read (should just return zeros, without blocking)"
              << std::endl;

    auto read_cb = [&](int x, std::span<const std::uint8_t> data) {
        std::string result{data.begin(), data.end()};
        std::cout << "Error " << x << "Read returned with length "
                  << data.size() << "\nContents: " << result << std::endl;
        wait.release();
    };

    pinger.read(0, pinger.blocksize(), read_cb);
    std::cout << "blocking..." << std::endl;
    wait.acquire();

    // blocks for a message
    std::cin >> msg;
    std::cout << "Message to be yeeted: " << msg << std::endl;
    auto cb = [](int x) { std::cout << "Write returned " << x << std::endl; };

    std::vector<std::uint8_t> data{};
    data.reserve(pinger.blocksize());

    std::transform(msg.begin(), msg.end(), std::back_inserter(data),
                   [](char c) { return static_cast<std::uint8_t>(c); });

    std::span<std::uint8_t> payload{data.data(), pinger.blocksize()};
    std::cout << "Message length " << payload.size() << '\n';
    pinger.write(13 * pinger.blocksize(), payload, cb);

    // sleep 3 seconds, then echo out the message from the network
    sleep(3);
    // auto read_cb = [&](int x, std::span<const std::uint8_t> data) {
    //     std::string result{data.begin(), data.end()};
    //     std::cout << "Read returned with length " << data.size()
    //               << "\nContents: " << result << std::endl;
    //     wait.release();
    // };

    pinger.read(13 * pinger.blocksize(), pinger.blocksize(), read_cb);
    wait.acquire();

    // Close connection
    nbd_close(nbd);
    return 0;
}
