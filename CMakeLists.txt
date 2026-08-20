
cmake_minimum_required(VERSION 3.28)
project(neuro_game_sdk_websocketpp)

set(CMAKE_CXX_STANDARD 11)

add_executable(deriveUriTest Test/Unit/deriveUri.cpp)
add_executable(NeuroRPS Test/RockPaperScissors.cpp)
add_executable(VoiceTest Test/VoiceTest.cpp)
include_directories(include)
add_compile_definitions(_WEBSOCKETPP_CPP11_THREAD_)
link_libraries(ws2_32)
add_compile_definitions(ASIO_STANDALONE)
include_directories(websocketpp)
