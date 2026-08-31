# http-server

Small HTTP/1.1 server in C++ for Linux.

## Results

| version | 1 conn | 100 conns |
|---|---|---|
| blocking, one request at a time | 27k | 68k |
| keep-alive | 87k | 86k |
| one thread per connection | 87k | 416k |
| epoll, single thread | 81k | 213k |
| epoll, one loop per core | 80k | 410k |
| faster parsing and response building | 81k | 405k |
| io_uring | 82k | 412k |
| io_uring end to end | 84k | 417k |

Measured with wrk over loopback, 10 second runs.

## Build and run

    cmake -B build -G Ninja
    ninja -C build
    ./build/src/http_server 8080 www

Then `curl http://localhost:8080/`. Tests: `ctest --test-dir build`.
Needs the nix dev shell (`direnv allow`) for libc++ and liburing.
