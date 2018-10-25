
add_library(DeviceIo SHARED
            linux/Timer.cpp
            linux/shell.cpp
            linux/Logger.cpp
            linux/socket_app.cpp
            linux/key.cpp
            linux/led_pwm.cpp
            linux/DeviceIo.cpp
            linux/bluetooth.cpp)

target_link_libraries(DeviceIo)

target_include_directories(DeviceIo PUBLIC
        "${DeviceIo_SOURCE_DIR}/include"
        "${SocketApp_SOURCE_DIR}/include"
        "${LoggerUtils_SOURCE_DIR}/include")

# install target
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)

