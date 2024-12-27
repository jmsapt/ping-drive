#include <algorithm>
#include <cstring>
#include <iostream>
#include <libnbd.h>
#include <semaphore>
#include <span>
#include <unistd.h>

#include "Pinger.hpp"
#include "lib/Channel.hpp"

int main() {
    nbd_handle *nbd = nbd_create();

    if (!nbd) {
        std::cerr << "Failed to create nbd handle\n";
        return 1;
    }

    std::cout << "eke?" << std::endl;
    auto [rx, tx] = create_channel<Pinger::ReadRequest>();

    auto pinger = Pinger{"TODO", std::move(tx), std::move(rx)};

    // auto _ = std::thread([&] { pinger.run(); });
    std::string msg;

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
    pinger.write(0, payload, cb);

    // sleep 3 seconds, then echo out the message from the network
    sleep(3);
    std::binary_semaphore wait{false};
    auto read_cb = [&](int x, std::span<const std::uint8_t> data) {
        std::string result{data.begin(), data.end()};
        std::cout << "Read returned with length " << data.size()
                  << "\nContents: " << result << std::endl;
        wait.release();
    };

    pinger.read(0, pinger.blocksize(), read_cb);
    wait.acquire();

    // Close connection
    nbd_close(nbd);
    return 0;
}
