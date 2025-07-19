
cmake_minimum_required(VERSION 3.28)
project(neuro_game_sdk_websocketpp)

set(CMAKE_CXX_STANDARD 11)

add_executable(NeuroRPS Test/RockPaperScissors.cpp)
find_package(Boost REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})
include_directories(websocketpp)