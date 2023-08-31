#include "timer.h"

#include <cstdio>
#include <unistd.h>

static void print_elapsed_time()
{
    static struct timespec start;
    struct timespec curr{};
    static int first_call = 1;
    __time_t secs, nsecs;

    if (first_call) {
        first_call = 0;
        if (clock_gettime(CLOCK_MONOTONIC, &start) == -1) {
            printf("clock_gettime \n");
            return;
        }
    }

    if (clock_gettime(CLOCK_MONOTONIC, &curr) == -1) {
        printf("clock_gettime \n");
        return;
    }
    secs = curr.tv_sec - start.tv_sec;
    nsecs = curr.tv_nsec - start.tv_nsec;
    if (nsecs < 0) {
        secs--;
        nsecs += 1000000000;
    }
    printf("%ld.%03ld:\t", secs, (nsecs + 500000) / 1000000);
}

int main() {
    Timer timer;
    print_elapsed_time();
    printf("timer started\n");
    timer.add(std::chrono::seconds(2), Timer::Cycle, [](){
        print_elapsed_time();
        printf("---------> cycle for 2 sec\n");
    }, true);
    timer.add(std::chrono::seconds(6), Timer::Cycle, [](){
        print_elapsed_time();
        printf("---------> cycle for 6 sec\n");
    });
    int ret = timer.add(std::chrono::seconds(1), Timer::Cycle, [&](){
        print_elapsed_time();
        printf("---------> one shoot for 8 sec\n");
    });
    timer.start();
    sleep(2);
    timer.pause(ret);
    printf("-------------------> pause end\n");
    sleep(5);
    timer.resume(ret);
    printf("-------------------> resume end\n");
    sleep(5);
    timer.stop();
    printf("-------------------> stop end\n");
    sleep(6);
    timer.start();
    printf("-------------------> start end\n");
    pause();
    printf("------------------->end\n");
    return 0;
}
