file(GLOB_RECURSE DeviceIo_SRC "${DeviceIo_SOURCE_DIR}/src/linux/*.cpp")
file(GLOB_RECURSE DeviceIo_SRC_C "${DeviceIo_SOURCE_DIR}/src/linux/*.c")
file(GLOB_RECURSE Gdbus_SRC "${DeviceIo_SOURCE_DIR}/src/linux/bluetooth/gdbus/*.c")
file(GLOB_RECURSE DeviceIo_Properties "${DeviceIo_SOURCE_DIR}/src/linux/propity/*.cpp")
file(GLOB_RECURSE DeviceIo_wifi "${DeviceIo_SOURCE_DIR}/src/linux/wifi/*.cpp")
file(GLOB_RECURSE Bt_SRC "${DeviceIo_SOURCE_DIR}/src/linux/bluetooth/gatt_config.c")
file(GLOB_RECURSE DeviceIo_netlink "${DeviceIo_SOURCE_DIR}/src/linux/netlink/*.cpp")

if(BLUEZ5_UTILS)
add_definitions(-DBLUEZ5_UTILS)
file(GLOB_RECURSE Bt_SRC "${DeviceIo_SOURCE_DIR}/src/linux/bluetooth/bluez_ctrl.cpp")
endif(BLUEZ5_UTILS)

add_library(DeviceIo SHARED
		${DeviceIo_SRC}
		${DeviceIo_SRC_C}
		${Gdbus_SRC}
		${Bt_SRC}
		${DeviceIo_Properties}
		${DeviceIo_wifi}
		${DeviceIo_netlink}
		)

#link_directories (
#	${DeviceIo_SOURCE_DIR}/src/linux
#)

find_package(PkgConfig)
pkg_check_modules (GLIB2 glib-2.0 REQUIRED)
pkg_check_modules (DBUS dbus-1 REQUIRED)
pkg_check_modules (GSTREAMER gstreamer-1.0 REQUIRED)
include_directories(${GLIB2_INCLUDE_DIRS})
include_directories(${DBUS_INCLUDE_DIRS})
include_directories(${GSTREAMER_INCLUDE_DIRS})
include_directories(${WPA_SUPPLICANT_INCLUDE_DIRS})
target_link_libraries (DeviceIo ${GLIB2_LIBRARIES})
target_link_libraries (DeviceIo ${DBUS_LIBRARIES})
target_link_libraries (DeviceIo ${GSTREAMER_LIBRARIES})
target_link_libraries (DeviceIo libwpa_client.so)
target_link_libraries (DeviceIo libbluetooth.so)

target_link_libraries(DeviceIo)

target_include_directories(DeviceIo PUBLIC
        "${DeviceIo_SOURCE_DIR}/include"
		"${DeviceIo_SOURCE_DIR}/src/linux/bluetooth/include"
        "${RAPIDJSON_INCLUDE_DIR}")

add_definitions("-Wall -g -fpermissive")

# install target
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)

