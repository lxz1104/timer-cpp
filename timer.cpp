//
// Created by xinzheng long on 2023/8/30.
//

#include "timer.h"

#include <atomic>
#include <thread>
#include <unordered_map>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <vector>
#include <unistd.h>

#define TIMER_INVALID_ID 1

struct TimerPrivate
{
    std::atomic<bool> isRunning{false};
    std::atomic<int>  epollFD{-1};
    std::unordered_map<int, std::pair<Timer::EventType, Timer::FuncType>> eventMap;
    std::thread       epollThread;
};

Timer::Timer()
    : m_p(std::make_unique<TimerPrivate>())
{

}

Timer::~Timer()
{
    this->stop();
}

bool Timer::start() {
    if (this->isRunning()) {
        return false;
    }
    m_p->isRunning = true;
    m_p->epollThread = std::thread(&Timer::epollThreadWorker, this);
    return true;
}

void Timer::stop() {
    if (!this->isRunning()) {
        return;
    }
    this->clear();
    m_p->isRunning = false;
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

Timer::Id Timer::registerEvent(const struct timespec &ts, Timer::EventType type, const Timer::FuncType &func, bool immediately)
{
    /* create timer */
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (timer_fd < 0) {
        return timer_fd;
    }

    // TODO: check if it is already in map
    auto ret = m_p->eventMap.emplace(timer_fd, std::make_pair(type, func));
    if (!ret.second) {
        return -TIMER_INVALID_ID;
    }
    this->addEpollEvent(timer_fd, EPOLLIN);
    if (immediately) {
        this->setupTimer(timer_fd, ts, {0, 0});
    } else {
        this->setupTimer(timer_fd, ts, ts);
    }
    return timer_fd;
}

bool Timer::addEpollEvent(int fd, uint32_t state)
{
    if (m_p->epollFD < 0) {
        return false;
    }
    struct epoll_event ev = {
            state,
            {.fd = fd},
    };
    return epoll_ctl(m_p->epollFD, EPOLL_CTL_ADD, fd, &ev) == 0;
}


bool Timer::delEpollEvent(int fd, uint32_t state)
{
    if (m_p->epollFD < 0) {
        return false;
    }
    struct epoll_event ev = {
            state,
            {.fd = fd},
    };
    return epoll_ctl(m_p->epollFD, EPOLL_CTL_DEL, fd, &ev) == 0;
}

bool Timer::setupTimer(int timerFD, const struct timespec &it_interval, const struct timespec &it_value)
{
    struct itimerspec its = {
            .it_interval = it_interval,
            .it_value = it_value,
    };
    if (timerfd_settime(timerFD, TFD_TIMER_ABSTIME, &its, nullptr) < 0)
    {
        return false;
    }
    return true;
}

void Timer::epollThreadWorker()
{
    /* create epoll */
    m_p->epollFD = epoll_create(1);
    if (m_p->epollFD < 0) {
        // create epoll instance failed
        return;
    }

    uint64_t exp{0};
    std::vector<struct epoll_event> events(m_p->eventMap.size(), { 0 });
    while (m_p->isRunning) {
        if (events.size() != m_p->eventMap.size()) {
            events.resize(m_p->eventMap.size());
        }

        int fire_events = epoll_wait(m_p->epollFD.load(), events.data(), static_cast<int>(events.size()), -1);

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
                it->second.second();
                if (OneShoot == it->second.first) {
                    close(it->first);
                    m_p->eventMap.erase(it);
                }
            }
        }
    }
    close(m_p->epollFD);
    m_p->isRunning = false;
}
