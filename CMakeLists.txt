cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)

PROJECT (deviceio_test)

message(${PROJECT_SOURCE_DIR})
message(${PROJECT_BINARY_DIR})


set(SRC_FILES
    DeviceIOTest.cpp
)

add_executable(deviceio_test ${SRC_FILES})
target_link_libraries(deviceio_test pthread DeviceIo asound)

install(TARGETS deviceio_test DESTINATION bin)

add_subdirectory("DeviceIO")
