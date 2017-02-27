#include "dynamic_library.h"

#include <iostream>
#include <vector>
#include <cerrno>
#include <cstring>

#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <dlfcn.h>


using namespace std;


int main(int argc, char* argv[]) {
    // failed to make it work, now using
    // LD_PRELOAD=/usr/lib/libasan.so:./libepoll_checker.so ./demo
    vector<DynamicLibraryHolder> libraries;
    for (int i = 1; i < argc; ++i) {
        try {
            libraries.emplace_back(argv[i]);
        } catch (std::runtime_error& e) {
            cerr << "failed to load library " << argv[i] << ": " << e.what() << endl;
            return 1;
        }
    }

    int epfd = epoll_create(1);
    if (epfd < 0) {
        cerr << "epoll_create() failed" << endl;
        return 1;
    }

    int sockets[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
        cerr << "failed to create a socketpair: " << strerror(errno) << endl;
        return 1;
    }

    for (int i = 0; i < 2; ++i) {
        int nonblock = 1;
        if (ioctl(sockets[i], FIONBIO, &nonblock) != 0) {
            cerr << "setting nonblocking mode failed: " << strerror(errno) << endl;
            return 2;
        }
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sockets[0];

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockets[0], &ev) != 0) {
        cerr << "epoll_ctl() failed: " << strerror(errno) << endl;
        return 1;
    }

    ev.events = EPOLLOUT;
    ev.data.fd = sockets[0];
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, sockets[0], &ev) != 0) {
        cerr << "epoll_ctl() failed: " << strerror(errno) << endl;
        return 1;
    }
}
