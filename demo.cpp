#include "dynamic_library.h"

#include <iostream>
#include <vector>

#include <sys/epoll.h>
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

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) != 0) {
        cerr << "epoll add failed" << endl;
        return 2;
    }

    for (int i = 0; i < 3; ++i) {
        int ret = epoll_wait(epfd, &ev, 1, 0);
        if (ret < 0) {
            cerr << "epoll wait failed" << endl;
            return 3;
        }
    }
}
