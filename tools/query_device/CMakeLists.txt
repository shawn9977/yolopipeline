cmake_minimum_required(VERSION 3.12)
project(query_device)

set(TARGET "query_device")

find_package(OpenVINO COMPONENTS Runtime REQUIRED)

add_executable(${TARGET} query_device.cpp)

target_link_libraries(${TARGET} PRIVATE openvino::runtime)
