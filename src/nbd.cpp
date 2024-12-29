#include <cstddef>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include "nbdserv.h"
#include "src/Pinger.hpp"

using namespace std;

template <unsigned BS = 1024> class RamDisk {
  private:
    static constexpr size_t NB = 1024;
    Pinger<BS, NB> pinger;
    std::pair<ReceiverChannel<ReadRequest>,
              SenderChannel<ReadRequest>>
        _channels;


  public:
    RamDisk(size_t _placebo)
        : _channels(create_channel<ReadRequest>()),
          pinger("Nothing yet", std::move(_channels.second),
                 std::move(_channels.first)) {}

    // should return false if some unrecoverable error has occurred
    bool good() const { return true; }

    // number of bytes per block
    static consteval size_t blocksize() { return BS; }

    // number of blocks in device
    size_t numblocks() const { return NB; }

    // read a single block from the device
    // index is the index of the block
    // data is a pointer to an array of size at last blocksize()
    void read(size_t index, nbdcpp::byte *data) const {
        // std::copy(_data.at(index).begin(), _data.at(index).end(), data);
    }

    // write a single block to the device
    // index is the index of the block
    // data is a pointer to an array of size at last blocksize()
    void write(size_t index, const nbdcpp::byte *data) {
        std::cout << "Write attempted" << std::endl;
        // std::copy(data, data + BS, _data.at(index).begin());
    }

    // read multiple blocks at once
    void multiread(size_t index, size_t count, nbdcpp::byte *data) const {
        nbdcpp::multiread_default(*this, index, count, data);
    }

    // write multiple blocks at once
    void multiwrite(size_t index, size_t count, const nbdcpp::byte *data) {
        nbdcpp::multiwrite_default(*this, index, count, data);
    }

    // returns true iff the flush operation is supported
    constexpr bool flushes() const { return true; }

    // Syncs all pending read/write ops to any underlying device
    void flush() const {}

    // returns true iff the trim operation is supported
    constexpr bool trims() const { return true; }

    // Performs a DISCARD/TRIM operation (optional)
    void trim(size_t, size_t) {}
};

int main(int argc, char *argv[]) {
    using namespace nbdcpp;
    auto usage = [argv]() {
        errout() << "usage: " << argv[0] << " size" << nbd_usage_line() << "\n";
        errout() << "  Provides a ramdisk over an NBD server.\n";
        errout() << "  size is the size of the ramdisk in KB\n";
        nbd_usage_doc(errout());
    };

    // size must be the first command line argument
    size_t size;
    if (argc < 2 || !(istringstream(argv[1]) >> size) || size <= 0) {
        usage();
        return 1;
    }

    return nbdcpp_main<RamDisk<>>(argc, argv, 2, usage, RamDisk<>::blocksize());
}
