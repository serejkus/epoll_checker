#include "dynamic_library.h"

#include <cerrno>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <unistd.h>


class DescriptorState {
public:
    DescriptorState() noexcept = default;
#define DS_GETTER_SETTER(f) \
    bool f() const noexcept { \
        return f ## _; \
    } \
    DescriptorState& f(bool value) noexcept { \
        f ## _ = value; \
        return *this; \
    }

    DS_GETTER_SETTER(InMonitored);
    DS_GETTER_SETTER(OutMonitored);
    DS_GETTER_SETTER(HadInOpsAfterMonitoring);
    DS_GETTER_SETTER(HadOutOpsAfterMonitoring);
    DS_GETTER_SETTER(PollReportedInEvent);
    DS_GETTER_SETTER(PollReportedOutEvent);
#undef DS_GETTER_SETTER

    static DescriptorState FromEpollEvents(const struct epoll_event& event) {
        return FromEpollEvents(event.events);
    }

    static DescriptorState FromEpollEvents(std::uint32_t events) {
        DescriptorState retval;

        if (events & (EPOLLIN|EPOLLPRI)) {
            retval.InMonitored(true);
        }

        if (events & EPOLLOUT) {
            retval.OutMonitored(true);
        }

        return retval;
    }

private:
    bool InMonitored_{ false };
    bool OutMonitored_{ false };
    bool HadInOpsAfterMonitoring_{ false };
    bool HadOutOpsAfterMonitoring_{ false };
    bool PollReportedInEvent_{ false };
    bool PollReportedOutEvent_{ false };
};


enum class ERead : std::uint8_t {
    NothingToRead,
    ReadSuccessfully,
    EndOfFile,
    Error,
};

enum class EWrite : std::uint8_t {
    WriteBufFull,
    WroteSuccesfully,
    Error,
};


class State {
public:
    using Epfd = int;
    using Fd = int;

private:
    using PolledDescriptorsMap = std::map<Fd, DescriptorState>;
    using PollMap = std::map<Epfd, PolledDescriptorsMap>;
    using FdsMap = std::map<Fd, std::set<Epfd>>;

public:
    State() noexcept = default;

    void OnEpollCreate(Epfd epfd);
    // do not call it if epoll_ctl returned fail
    void OnAddEvent(Epfd epfd, Fd fd, const struct epoll_event& ev);
    // do not call it if epoll_ctl returned fail
    void OnModEvent(Epfd epfd, Fd fd, const struct epoll_event& ev);
    void OnEraseEvent(Epfd epfd, Fd fd);
    void OnRead(Fd fd, ERead result);
    void OnWrite(Fd fd, EWrite result);
    void OnClose(Fd fd);

private:
    bool HasEpfd(Epfd epfd) const noexcept;
    // assumes epfdIt is a valid iterator to PollMap_
    void EraseEpfd(PollMap::const_iterator epfdIt) noexcept;
    void EraseFd(Fd fd) noexcept;

    // assumes fd is in FdsMap_ and epfd is one of its elements
    void OnRead(Epfd epfd, Fd fd, ERead result);
    // assumes fd is in FdsMap_ and epfd is one of its elements
    void OnWrite(Epfd epfd, Fd fd, EWrite result);

private:
    PollMap PollMap_{};
    FdsMap FdsMap_{};
};


static State STATE{};


using EpollCreateFn = int(int);
static EpollCreateFn* ORIGINAL_EPOLL_CREATE = reinterpret_cast<EpollCreateFn*>(DynamicLibraryHolder::CheckedNextSymbol("epoll_create"));

using EpollCreate1Fn = int(int);
static EpollCreate1Fn* ORIGINAL_EPOLL_CREATE1 = reinterpret_cast<EpollCreate1Fn*>(DynamicLibraryHolder::CheckedNextSymbol("epoll_create1"));

using EpollCtlFn = int(int, int, int, struct epoll_event*);
static EpollCtlFn* ORIGINAL_EPOLL_CTL = reinterpret_cast<EpollCtlFn*>(DynamicLibraryHolder::CheckedNextSymbol("epoll_ctl"));

using EpollWaitFn = int(int, struct epoll_event*, int, int);
static EpollWaitFn* ORIGINAL_EPOLL_WAIT = reinterpret_cast<EpollWaitFn*>(DynamicLibraryHolder::CheckedNextSymbol("epoll_wait"));

using CloseFn = int(int);
static CloseFn* ORIGINAL_CLOSE = reinterpret_cast<CloseFn*>(DynamicLibraryHolder::CheckedNextSymbol("close"));

#define GET_ORIGINAL(identificator, funcname, functype) \
    using identificator ## Fn = functype; \
    static identificator ## Fn* identificator = reinterpret_cast<identificator ## Fn*>(DynamicLibraryHolder::CheckedNextSymbol(funcname));

GET_ORIGINAL(ORIGINAL_READ, "read", ssize_t(int, void*, size_t))
GET_ORIGINAL(ORIGINAL_READV, "readv", ssize_t(int, const struct iovec*, int))
GET_ORIGINAL(ORIGINAL_RECV, "recv", ssize_t(int, void*, size_t, int));
GET_ORIGINAL(ORIGINAL_RECVMSG, "recvmsg", ssize_t(int, struct msghdr*, int))

GET_ORIGINAL(ORIGINAL_WRITE, "write", ssize_t(int, const void*, size_t));
GET_ORIGINAL(ORIGINAL_WRITEV, "writev", ssize_t(int, const struct iovec*, int))
GET_ORIGINAL(ORIGINAL_SEND, "send", ssize_t(int, const void*, size_t, int));
GET_ORIGINAL(ORIGINAL_SENDMSG, "sendmsg", ssize_t(int, const struct msghdr*, int));

#undef GET_ORIGINAL



// TODO: proper work with ONESHOT

static void OnRead(int fd, ssize_t result) noexcept {
    try {
        if (result > 0) {
            STATE.OnRead(fd, ERead::ReadSuccessfully);
        } else if (result == 0) {
            STATE.OnRead(fd, ERead::EndOfFile);
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            STATE.OnRead(fd, ERead::NothingToRead);
        } else if (errno != EINTR) {
            STATE.OnRead(fd, ERead::Error);
        }
    } catch (...) {
        std::cerr << "something went really wrong in OnRead" << std::endl;
    }
}

static void OnWrite(int fd, ssize_t result) noexcept {
    try {
        if (result >= 0) {
            STATE.OnWrite(fd, EWrite::WroteSuccesfully);
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            STATE.OnWrite(fd, EWrite::WriteBufFull);
        } else if (errno != EINTR) {
            STATE.OnWrite(fd, EWrite::Error);
        }
    } catch (...) {
        std::cerr << "something went really wrong in OnWrite" << std::endl;
    }
}


extern "C" int epoll_create(int size) {
    const int ret = ORIGINAL_EPOLL_CREATE(size);
    STATE.OnEpollCreate(ret);
    return ret;
}

extern "C" int epoll_create1(int flags) {
    const int ret = ORIGINAL_EPOLL_CREATE1(flags);
    STATE.OnEpollCreate(ret);
    return ret;
}

extern "C" int epoll_ctl(int epfd, int op, int fd, struct epoll_event* event) {
    const int ret = ORIGINAL_EPOLL_CTL(epfd, op, fd, event);

    if (ret == 0) {
        switch (op) {
            case EPOLL_CTL_ADD:
                STATE.OnAddEvent(epfd, fd, *event);
                break;
            case EPOLL_CTL_MOD:
                STATE.OnModEvent(epfd, fd, *event);
                break;
            case EPOLL_CTL_DEL:
                STATE.OnEraseEvent(epfd, fd);
                break;
        }
    }

    return ret;
}

extern "C" int epoll_wait(int epfd, struct epoll_event* events, int maxevents, int timeout) {
    return ORIGINAL_EPOLL_WAIT(epfd, events, maxevents, timeout);
}

extern "C" int close(int fd) {
    const int ret = ORIGINAL_CLOSE(fd);

    if (ret == 0) {
        STATE.OnClose(fd);
    }

    return ret;
}

extern "C" ssize_t read(int fd, void* buf, size_t count) {
    const ssize_t ret = ORIGINAL_READ(fd, buf, count);
    OnRead(fd, ret);
    return ret;
}

extern "C" ssize_t readv(int fd, const struct iovec* iov, int iovcnt) {
    const ssize_t ret = ORIGINAL_READV(fd, iov, iovcnt);
    OnRead(fd, ret);
    return ret;
}

extern "C" ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    const ssize_t ret = ORIGINAL_RECV(sockfd, buf, len, flags);
    OnRead(sockfd, ret);
    return ret;
}

extern "C" ssize_t recvmsg(int sockfd, struct msghdr* msg, int flags) {
    const ssize_t ret = ORIGINAL_RECVMSG(sockfd, msg, flags);
    OnRead(sockfd, ret);
    return ret;
}

extern "C" ssize_t write(int fd, const void* buf, size_t count) {
    const ssize_t ret = ORIGINAL_WRITE(fd, buf, count);
    OnWrite(fd, ret);
    return ret;
}

extern "C" ssize_t writev(int fd, const struct iovec* iov, int iovcnt) {
    const ssize_t ret = ORIGINAL_WRITEV(fd, iov, iovcnt);
    OnWrite(fd, ret);
    return ret;
}

extern "C" ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    const ssize_t ret = ORIGINAL_SEND(sockfd, buf, len, flags);
    OnWrite(sockfd, ret);
    return ret;
}

extern "C" ssize_t sendmsg(int sockfd, const struct msghdr* msg, int flags) {
    const ssize_t ret = ORIGINAL_SENDMSG(sockfd, msg, flags);
    OnWrite(sockfd, ret);
    return ret;
}


void State::OnEpollCreate(Epfd epfd) {
    using namespace std;

    if (epfd >= 0) {
        if (!HasEpfd(epfd)) {
            PollMap_[epfd] = {};
        } else {
            cerr << "OnEpollCreate() encountered an error: " << epfd << " is registred already" << endl;
        }
    }
}

void State::OnAddEvent(Epfd epfd, Fd fd, const struct epoll_event& ev) {
    PollMap_[epfd][fd] = DescriptorState::FromEpollEvents(ev);
}

void State::OnModEvent(Epfd epfd, Fd fd, const struct epoll_event& ev) {
    using namespace std;

    auto& pollMap = PollMap_.at(epfd);
    auto& state = pollMap.at(fd);
    auto newState = DescriptorState::FromEpollEvents(ev);
    if (state.InMonitored() && !newState.InMonitored() && !state.HadInOpsAfterMonitoring()) {
        cerr << "error for fd " << fd << ": it's been monitored for input, had not got its event and now it is not monitored for output" << endl;
    }
    if (state.OutMonitored() && !newState.OutMonitored() && !state.HadOutOpsAfterMonitoring()) {
        cerr << "error for fd " << fd << ": it's been monitored for output, had not got its event and now it is not monitored for input" << endl;
    }
    pollMap[fd] = newState;
}

void State::OnEraseEvent(Epfd epfd, Fd fd) {
    auto& pollMap = PollMap_.at(epfd);
    auto fdIt = pollMap.find(fd);
    if (fdIt != pollMap.end()) {
        pollMap.erase(fdIt);
        FdsMap_.erase(fd);
    }
}

void State::OnRead(Fd fd, ERead result) {
    auto fdIt = FdsMap_.find(fd);
    if (fdIt == FdsMap_.end()) {
        return;
    }

    const auto& epfds = fdIt->second;
    for (Epfd epfd: epfds) {
        OnRead(epfd, fd, result);
    }
}

void State::OnWrite(Fd fd, EWrite result) {
    auto fdIt = FdsMap_.find(fd);
    if (fdIt == FdsMap_.end()) {
        return;
    }

    const auto& epfds = fdIt->second;
    for (Epfd epfd: epfds) {
        OnWrite(epfd, fd, result);
    }
}

void State::OnClose(Fd fd) {
    auto epfdIt = PollMap_.find(fd);
    if (epfdIt != PollMap_.end()) {
        EraseEpfd(epfdIt);
    } else {
        EraseFd(fd);
    }
}

bool State::HasEpfd(Epfd epfd) const noexcept {
    return PollMap_.find(epfd) != PollMap_.end();
}

void State::EraseEpfd(PollMap::const_iterator epfdIt) noexcept {
    const Epfd epfd = epfdIt->first;

    PollMap_.erase(epfdIt);

    std::set<Fd> emptyFds;
    for (auto& fdData: FdsMap_) {
        auto& fdsSet = fdData.second;

        Fd fd = -1;
        auto fdIt = fdsSet.find(epfd);
        if (fdIt != fdsSet.end()) {
            fd = *fdIt;
            fdsSet.erase(fdIt);
        }

        if (fdsSet.empty()) {
            emptyFds.insert(fd);
        }
    }

    for (Fd fd: emptyFds) {
        FdsMap_.erase(fd);
    }
}

void State::EraseFd(Fd fd) noexcept {
    auto fdIt = FdsMap_.find(fd);

    if (fdIt != FdsMap_.end()) {
        const auto& epfds = fdIt->second;

        for (Epfd epfd: epfds) {
            auto epfdIt = PollMap_.find(epfd);

            if (epfdIt != PollMap_.end()) {
                auto& states = epfdIt->second;
                auto statesIt = states.find(fd);
                if (statesIt != states.end()) {
                    states.erase(statesIt);
                }
            }
        }
    }
}

void State::OnRead(Epfd epfd, Fd fd, ERead result) {
    auto& state = PollMap_.at(epfd).at(fd);
    switch (result) {
        case ERead::ReadSuccessfully:
        case ERead::EndOfFile:
        case ERead::Error:
            state.HadInOpsAfterMonitoring(true);
            break;
        case ERead::NothingToRead:
        default:
            break;
    }
}

void State::OnWrite(Epfd epfd, Fd fd, EWrite result) {
    auto& state = PollMap_.at(epfd).at(fd);
    switch (result) {
        case EWrite::WroteSuccesfully:
        case EWrite::Error: // TODO: maybe error should set err state, so that write() with error transits state to erroneous and socket should be closed even if it is monitored for read()
            state.HadOutOpsAfterMonitoring(true);
            break;
        case EWrite::WriteBufFull:
        default:
            break;
    }
}
