# http-server

Small HTTP/1.1 server in C++ for Linux.

## Results

| version | 1 conn | 100 conns | 100 conns, 8 client threads | 500 conns, 8 client threads |
|---|---|---|---|---|
| blocking, one request at a time | 27k | 68k | 93k | 91k |
| keep-alive | 87k | 86k | 87k | 86k |
| one thread per connection | 87k | 416k | 911k | 854k |
| epoll, single thread | 81k | 213k | 210k | 199k |
| epoll, one loop per core | 80k | 410k | 1.04M | 1.05M |
| faster parsing and response building | 81k | 405k | 1.04M | 1.08M |
| io_uring | 82k | 412k | 1.05M | 1.14M |
| io_uring end to end | 84k | 417k | 1.11M | 1.14M |

Measured with wrk over loopback, 10 second runs, on a Ryzen 7 8845HS
(8 cores / 16 threads, Linux 7.1).

## Build and run

    cmake -B build -G Ninja
    ninja -C build
    ./build/src/http_server 8080 www

Then `curl http://localhost:8080/`. Tests: `ctest --test-dir build`.
Needs the nix dev shell (`direnv allow`) for libc++ and liburing.
