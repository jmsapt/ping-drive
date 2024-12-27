#include <cstdint>
#include <stdexcept>
#include <string>
#include <arpa/inet.h>

class IP {
  public:
    IP() = delete;

    IP(const std::string &ip_string) {
        struct in_addr addr;
        if (inet_pton(AF_INET, ip_string.c_str(), &addr) != 1)
            throw std::runtime_error("Couldn't parse IP");
        ip = addr.s_addr;
    }
    std::uint32_t to_network() const { return ip; }

  private:
    std::uint32_t ip;
};
