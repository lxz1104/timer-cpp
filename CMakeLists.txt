cmake_minimum_required(VERSION 3.18)
project(timer)

set(CMAKE_CXX_STANDARD 14)

add_executable(${PROJECT_NAME} main.cpp timer.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
