file(GLOB_RECURSE DeviceIo_SRC "${DeviceIo_SOURCE_DIR}/src/linux/*.cpp")

add_library(DeviceIo SHARED
            ${DeviceIo_SRC})

target_link_libraries(DeviceIo)

target_include_directories(DeviceIo PUBLIC
        "${DeviceIo_SOURCE_DIR}/include"
        "${RAPIDJSON_INCLUDE_DIR}")

if(BLUEZ5_UTILS)
        add_definitions(-DBLUEZ5_UTILS)
endif(BLUEZ5_UTILS)

add_definitions("-Wall -g")

# install target
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)

