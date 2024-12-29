#include <cstring>
#include <future>
#include <ranges>
#include <semaphore>

#define NBDKIT_API_VERSION 1
#include <nbdkit-plugin.h>

#include "src/Pinger.hpp"

#define THREAD_MODEL NBDKIT_THREAD_MODEL_SERIALIZE_ALL_REQUESTS

using PingerT = Pinger<1024, 1024, 2>;

PingerT *get_pinger(void *handle) { return static_cast<PingerT *>(handle); };

static void *my_open(int) {
    nbdkit_error("==> opened!!!");
    auto [rx, tx] = create_channel<ReadRequest>();
    return new PingerT("/home/james/ping-fs/victims.txt", std::move(tx), std::move(rx));
}

static void my_close(void *handle) {
    nbdkit_error("==> closed!!!");
    delete get_pinger(handle);
}

int my_pread(void *handle, void *buf, uint32_t count, uint64_t offset) {
    auto pinger = get_pinger(handle);
    std::counting_semaphore<> sem{0};

    nbdkit_error("==> %d", count % pinger->blocksize());
    if (count < pinger->blocksize() || count % pinger->blocksize() != 0)
        return -1;

    // register reads
    bool failed{false};
    nbdkit_error("==> pread called");
    for (auto i : std::ranges::iota_view(0ul, count / pinger->blocksize())) {
        // SAFETY: we are always writing to different regions of the buffer with
        // no overlap
        auto cb = [i, buf, &failed, &sem,
                   &pinger](int res, std::span<const std::uint8_t> data) {
            if (res < 0 || data.size() == 0) {
                nbdkit_error("==> Error! %d ", res);
                failed = true;
                sem.release();
                return;
            }
            nbdkit_error("==> CALLBACK! : %02x %02x %02x %02x", data[0],
                         data[1], data[2], data[3]);
            ::memcpy(static_cast<std::uint8_t *>(buf) + i * pinger->blocksize(),
                     data.data(), data.size());
            sem.release();
        };
        nbdkit_error("==> issue read read %d %d",
                     offset + i * pinger->blocksize(), pinger->blocksize());
        pinger->read(offset + i * pinger->blocksize(), pinger->blocksize(), cb);
    };

    // block on n many returns
    for (auto _ : std::ranges::iota_view(0ul, count / pinger->blocksize()))
        sem.acquire();

    nbdkit_error("==> pread returned");
    if (failed)
        return -1;
    return 0;
}

int my_pwrite(void *handle, const void *buf, uint32_t count, uint64_t offset) {
    auto pinger = get_pinger(handle);
    std::counting_semaphore<> sem{0};

    nbdkit_error("==> %d", count % pinger->blocksize());
    if (count < pinger->blocksize() || count % pinger->blocksize() != 0)
        return -1;

    // register reads
    nbdkit_error("==> pwrite called");
    for (auto i : std::ranges::iota_view(0ul, count / pinger->blocksize())) {
        // SAFETY: we are always writing to different regions of the buffer with
        // no overlap
        auto cb = [&sem](int res) {
            if (res < 0) {
                nbdkit_error("==> ERROR! %d", res);
            }
            nbdkit_error("==> CALLBACK!");
            sem.release();
        };
        auto data = std::span<const std::uint8_t>(
            static_cast<const std::uint8_t *>(buf) + i * pinger->blocksize(),
            pinger->blocksize());

        pinger->write(offset + i * pinger->blocksize(), data, cb);
    };

    // block on n many returns
    for (auto _ : std::ranges::iota_view(0ul, count / pinger->blocksize()))
        sem.acquire();

    nbdkit_error("==> pwrite returned");
    return 0;
};

int64_t my_get_size(void *handle) {
    auto pinger = get_pinger(handle);

    nbdkit_error("==> total size %d",
                 pinger->blocksize() * pinger->num_blocks());
    // return pinger->blocksize();
    return pinger->blocksize() * pinger->num_blocks();
}

int my_block_size(void *handle, uint32_t *minimum, uint32_t *preferred,
                  uint32_t *maximum) {
    auto pinger = get_pinger(handle);

    nbdkit_error("==> block_size size %d",
                 pinger->blocksize() * pinger->num_blocks());
    *minimum   = pinger->blocksize();
    *preferred = pinger->blocksize();
    *maximum   = pinger->blocksize();

    return 0;
}

extern "C" {
/*
  int (*pread) (void *handle, void *buf, uint32_t count, uint64_t offset);
  int (*pwrite) (void *handle, const void *buf, uint32_t count,
                 uint64_t offset);
  int (*flush) (void *handle);
  int (*trim) (void *handle, uint32_t count, uint64_t offset);
  int (*zero) (void *handle, uint32_t count, uint64_t offset, int may_trim);
*/

int disable(void *) { return 0; }
int enable(void *) { return 1; }

static struct nbdkit_plugin plugin = {
    .name      = "myplugin",
    .open      = my_open,
    .close     = my_close,
    .get_size  = my_get_size,
    .can_write = enable,

    .pread  = my_pread,
    .pwrite = my_pwrite,

    .can_cache = disable,

    .block_size = my_block_size,
    /* etc */

};

NBDKIT_REGISTER_PLUGIN(plugin)
}
