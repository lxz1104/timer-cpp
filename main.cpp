#include "timer.h"

#include <unistd.h>
#include <cstdio>

int main() {
    Timer::FuncType func = [](){
        printf("cycle event for 5 sec\n");
    };

    Timer timer;
    timer.start();
    int ret = timer.add(std::chrono::seconds(1), Timer::Cycle, [](){
        printf("cycle event for 1 sec\n");
    });
    printf("id: %d\n", ret);
    ret = timer.add(std::chrono::seconds(5), Timer::Cycle, func);
    printf("id: %d\n", ret);
    ret = timer.add(std::chrono::seconds(10), Timer::OneShoot, [](){
        printf("------------> one shoot\n");
    });
    printf("id: %d\n", ret);
    pause();
    timer.stop();
    return 0;
}