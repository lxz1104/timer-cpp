#include "timer.h"

#include <mutex>
#include <vector>
#include <atomic>
#include <thread>
#include <csignal>
#include <algorithm>
#include <unordered_map>

#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>

#define TIMER_INVALID_ID 1

struct TimerPrivate
{
    std::atomic<bool> isRunning{false};
    std::atomic<int>  epollFD{-1};
    std::unordered_map<int, struct itimerspec> resumeMap; // TODO: need thread safe
    std::unordered_map<int, std::pair<Timer::EventType, Timer::FuncType>> eventMap;  // TODO: need thread safe
    std::thread                 epollThread;
};

Timer::Timer(size_t)
    : m_p(std::make_unique<TimerPrivate>())
{
    /* create epoll */
    m_p->epollFD = epoll_create(1);
}

Timer::~Timer()
{
    this->clear();
    this->stop();
    if (m_p->epollFD > 0) {
        close(m_p->epollFD);
    }
}

bool Timer::start() {
    if (this->isRunning()) {
        return false;
    }
    m_p->isRunning = true;
    m_p->epollThread = std::thread(&Timer::epollThreadWorker, this);
    return m_p->resumeMap.empty() || this->resumeAll();
}

void Timer::stop() {
    if (!this->isRunning()) {
        return;
    }
    m_p->isRunning = false;
    /** pause all timer **/
    this->pauseAll();
    /** terminal thread **/
    pthread_kill(m_p->epollThread.native_handle(), SIGUSR1);
    if (m_p->epollThread.joinable() || std::this_thread::get_id() != m_p->epollThread.get_id()) {
        m_p->epollThread.join();
    } else {
        m_p->epollThread.detach();
    }
}

bool Timer::isRunning() {
    return m_p->isRunning;
}

void Timer::clear()
{
    for (const auto & it: m_p->eventMap) {
        this->delEpollEvent(it.first, EPOLLIN);
        close(it.first);
    }
    m_p->eventMap.clear();
}

bool Timer::remove(Timer::Id id)
{
    auto it = m_p->eventMap.find(id);
    if (it != m_p->eventMap.end()) {
        this->delEpollEvent(it->first, EPOLLIN);
        close(it->first);
        m_p->eventMap.erase(it);
        return true;
    }
    return false;
}

bool Timer::pause(Timer::Id timer_id)
{
    if (timer_id < 0 || m_p->eventMap.find(timer_id) == m_p->eventMap.end()) {
        return false;
    }
    struct itimerspec newValue{ {0, 0}, {0, 0} };
    struct itimerspec oldValue{};
    if (timerfd_settime(timer_id, TFD_TIMER_ABSTIME, &newValue, &oldValue) < 0) {
        return false;
    }
    return m_p->resumeMap.emplace(timer_id, oldValue).second;
}

bool Timer::pauseAll() {
    return std::all_of(m_p->eventMap.begin(), m_p->eventMap.end(), [this] (const auto & it) -> bool {
        struct itimerspec newValue{ {0, 0}, {0, 0} };
        struct itimerspec oldValue{};
        if (timerfd_settime(it.first, TFD_TIMER_ABSTIME, &newValue, &oldValue) < 0) {
            return false;
        }
        return m_p->resumeMap.emplace(it.first, oldValue).second;
    });
}

bool Timer::resume(Timer::Id timer_id)
{
    auto it = m_p->resumeMap.find(timer_id);
    if (m_p->resumeMap.end() == it) {
        return false;
    }
    // get current time
    struct timespec now{0, 0};
    (void) clock_gettime(CLOCK_MONOTONIC, &now);
    now.tv_sec  += it->second.it_interval.tv_sec;
    now.tv_nsec += it->second.it_interval.tv_sec;
    it->second.it_value = now;

    if (timerfd_settime(timer_id, TFD_TIMER_ABSTIME, &it->second, nullptr) < 0) {
        this->remove(timer_id);
        return false;
    }
    m_p->resumeMap.erase(it);
    return true;
}

bool Timer::resumeAll() {
    decltype(m_p->resumeMap) map;
    std::swap(m_p->resumeMap, map);
    return std::all_of(map.begin(), map.end(), [] (auto & it) -> int {
        // get current time
        struct timespec now{0, 0};
        (void) clock_gettime(CLOCK_MONOTONIC, &now);
        now.tv_sec  += it.second.it_interval.tv_sec;
        now.tv_nsec += it.second.it_interval.tv_sec;
        it.second.it_value = now;
        if (timerfd_settime(it.first, TFD_TIMER_ABSTIME, &it.second, nullptr) < 0) {
            return false;
        }
        return true;
    });
}

Timer::Id Timer::registerEvent(const struct timespec &ts, Timer::EventType type, const Timer::FuncType &func, bool immediately)
{
    /* create timer */
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (timer_fd < 0) {
        return timer_fd;
    }

    // get current time
    struct timespec now{0, 0};
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        close(timer_fd);
        return -TIMER_INVALID_ID;
    }
    if (!immediately) {
        now.tv_sec  += ts.tv_sec;
        now.tv_nsec += ts.tv_sec;
    }

    // add to epoll
    this->addEpollEvent(timer_fd, EPOLLIN);

    // setup timer
    struct itimerspec its {ts, now};
    if (timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &its, nullptr) < 0) {
        this->delEpollEvent(timer_fd, EPOLLIN);
        close(timer_fd);
        return -TIMER_INVALID_ID;
    }

    auto ret = m_p->eventMap.emplace(timer_fd, std::make_pair(type, func));
    if (!ret.second) {
        this->delEpollEvent(timer_fd, EPOLLIN);
        close(timer_fd);
        return -TIMER_INVALID_ID;
    }
    return timer_fd;
}

bool Timer::addEpollEvent(int fd, uint32_t state)
{
    if (m_p->epollFD < 0) {
        return false;
    }
    struct epoll_event ev = {
            state, {}
    };
    ev.data.fd = fd;
    return epoll_ctl(m_p->epollFD, EPOLL_CTL_ADD, fd, &ev) == 0;
}


bool Timer::delEpollEvent(int fd, uint32_t state)
{
    if (m_p->epollFD < 0) {
        return false;
    }
    struct epoll_event ev = {
            state, {},
    };
    ev.data.fd = fd;
    return epoll_ctl(m_p->epollFD, EPOLL_CTL_DEL, fd, &ev) == 0;
}

void Timer::epollThreadWorker()
{
   /* try to creat epoll instance */
    if (m_p->epollFD < 0) {
        /* create epoll */
        m_p->epollFD = epoll_create(1);
        if (m_p->epollFD < 0) {
            return;
        }
    }

    uint64_t exp{0};
    std::vector<struct epoll_event> events(m_p->eventMap.size());
    { // for safety exit thread
        struct sigaction act{};
        act.sa_handler = [](int){};
        sigemptyset(&act.sa_mask);
        sigaction(SIGUSR1, &act, {});
    }
    while (m_p->isRunning) {
        if (events.size() != m_p->eventMap.size()) {
            events.resize(m_p->eventMap.size());
        }

        // epoll waits without time out
        int fire_events = epoll_wait(m_p->epollFD.load(), events.data(), static_cast<int>(events.size()), -1);
        if (fire_events < 0) {
            bool needExit = false;
            switch (errno) {
                case EBADF:
                case EINVAL:
                case EFAULT:
                    needExit = true;
                    break;
                case EINTR:
                default:
                    break;
            }
            if (needExit) {
                break;
            }
            continue;
        }

        for (ssize_t i = 0; i < fire_events; ++i) {
            int fd = events[i].data.fd;
            ssize_t size = read(fd, &exp, sizeof(exp));
            if (size != sizeof(uint64_t)) {
                // read error, try again
                continue;
            }

            // TODO: change to thread pool
            auto it = m_p->eventMap.find(fd);
            if (it != m_p->eventMap.end()) {

                if (OneShoot == it->second.first) {
                    it->second.second();
                    close(it->first);
                    m_p->eventMap.erase(it);
                } else {
                    it->second.second();
                }
            }
        }
    }
    m_p->isRunning = false;
}
