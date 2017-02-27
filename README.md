# Epoll checker

This is a tool to be used with binaries using epoll() calls family to check for errors in using it.

Say, we have a server that processes TCP clients. Client socket is full-duplex. So, you may want to both write and read from it asynchronously. Example of such wish is http2 protocol. One may be reading client's request for css file while writing to it js file. It is an error to register `EPOLLIN`, not getting any of `read, readv, recv, recvfrom, recvmsg`, and setting `EPOLLOUT` and vice versa for `write, writev, send, sendto, sendmsg`.

NB: it is not thread-safe, but it can be added easily.

# Build

Build process requires `cmake`, `make` and a `c++` compiler:

```bash
git clone https://github.com/serejkus/epoll_checker.git
cd epoll_checker
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ../
make
```

The result is `libepoll_checker.so` library to be used with `LD_LIBRARY_PRELOAD`

# Usage

To use this library one's need to preload it to the binary being tested:

```bash
LD_PRELOAD=./libepoll_checker.so ./demo
```

# AddressSanitizer

This library might be used with [Address Sanizier](https://github.com/google/sanitizers). It requires adding a flag to `cmake` configuration and preloading asan library before checker dynamic library.

```bash
git clone https://github.com/serejkus/epoll_checker.git
cd epoll_checker
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DSANITIZER_TYPE=Address ../
make
LD_PRELOAD=/usr/lib/libasan.so:./libepoll_checker.so ./demo
```
