#ifndef TIMER_H_
#define TIMER_H_

#include <memory>
#include <chrono>
#include <functional>

#define TIMER_INVALID_ID 1

class Timer final 
{
public:
    Timer(const Timer &) = delete;
    Timer(const Timer &&) = delete;
    Timer & operator=(const Timer &) = delete;
    Timer & operator=(const Timer &&) = delete;

    explicit Timer(size_t num_worker = 1);
    ~Timer();

    using FuncType = std::function<void(void)>;

    using EventType = enum : uint8_t {
        OneShoot,
        Cycle,
    };
    using Id = int;

    bool start();
    void stop();
    bool isRunning();
    void clear();

    template<typename Rep, typename Period>
    Id add(const std::chrono::duration<Rep, Period>& time_out, EventType type, const FuncType & func, bool immediately = false);

    bool remove(Id timer_id);
    bool pause(Id timer_id);
    bool resume(Id timer_id);
    bool pauseAll();
    bool resumeAll();

private:
    Id registerEvent(const struct timespec &ts, Timer::EventType type, const Timer::FuncType & func, bool immediately);
    bool addEpollEvent(int fd, uint32_t state);
    bool delEpollEvent(int fd, uint32_t state);
    void epollThreadWorker();

    std::unique_ptr<struct TimerPrivate> m_p;
};

template<typename Rep, typename Period>
Timer::Id Timer::add(const std::chrono::duration<Rep, Period> &time_out, Timer::EventType type, const Timer::FuncType & func, bool immediately) {
    if (time_out <= time_out.zero()) {
        return -TIMER_INVALID_ID;
    }
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(time_out);
    auto nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(time_out - sec);
    struct timespec ts = {
            static_cast<std::time_t>(sec.count()),
            static_cast<long int>(nsec.count())
    };
    return this->registerEvent(ts, type, func, immediately);
}

#undef TIMER_INVALID_ID
#endif //TIMER_H_