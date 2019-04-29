file(GLOB_RECURSE DeviceIo_SRC "${DeviceIo_SOURCE_DIR}/src/linux/*.cpp")
file(GLOB_RECURSE DeviceIo_SRC_C "${DeviceIo_SOURCE_DIR}/src/linux/*.c")
file(GLOB_RECURSE DeviceIo_Properties "${DeviceIo_SOURCE_DIR}/src/linux/propity/*.cpp")
file(GLOB_RECURSE DeviceIo_wifi "${DeviceIo_SOURCE_DIR}/src/linux/wifi/*.cpp")
file(GLOB_RECURSE DeviceIo_netlink "${DeviceIo_SOURCE_DIR}/src/linux/netlink/*.cpp")
file(GLOB_RECURSE DeviceIo_encode "${DeviceIo_SOURCE_DIR}/src/linux/encode/*.cpp")

if(REALTEK)
add_definitions(-DREALTEK)
endif()

if(BLUEZ)
message("build bluez...")
add_definitions(-DBLUEZ5_UTILS)
file(GLOB_RECURSE DeviceIo_BLUZ_CXX "${DeviceIo_SOURCE_DIR}/bluetooth/bluez/*.cpp")
file(GLOB_RECURSE DeviceIo_BLUZ_C "${DeviceIo_SOURCE_DIR}/bluetooth/bluez/*.c")

add_library(DeviceIo SHARED
		${DeviceIo_SRC}
		${DeviceIo_SRC_C}
		${DeviceIo_Properties}
		${DeviceIo_wifi}
		${DeviceIo_netlink}
		${DeviceIo_encode}
		${DeviceIo_BLUZ_CXX}
		${DeviceIo_BLUZ_C}
		)
target_include_directories(DeviceIo PUBLIC
		"${DeviceIo_SOURCE_DIR}/bluetooth/bluez/include")
find_package(PkgConfig)
pkg_check_modules (GLIB2 glib-2.0 REQUIRED)
pkg_check_modules (DBUS dbus-1 REQUIRED)
include_directories(${GLIB2_INCLUDE_DIRS})
include_directories(${DBUS_INCLUDE_DIRS})
target_link_libraries (DeviceIo ${GLIB2_LIBRARIES})
target_link_libraries (DeviceIo ${DBUS_LIBRARIES})
target_link_libraries (DeviceIo libbluetooth.so)

elseif(BSA)

file(GLOB_RECURSE DeviceIo_BSA_API_CXX "${DeviceIo_SOURCE_DIR}/bluetooth/bsa/bluetooth_bsa.cpp")

if(CYPRESS)
message("build cypress bsa...")
file(GLOB_RECURSE DeviceIo_BSA_CYPRESS_CXX "${DeviceIo_SOURCE_DIR}/bluetooth/bsa/cypress/*.cpp")
file(GLOB_RECURSE DeviceIo_BSA_CYPRESS_C "${DeviceIo_SOURCE_DIR}/bluetooth/bsa/cypress/*.c")
add_library(DeviceIo SHARED
		${DeviceIo_SRC}
		${DeviceIo_SRC_C}
		${DeviceIo_Properties}
		${DeviceIo_wifi}
		${DeviceIo_netlink}
		${DeviceIo_encode}
		${DeviceIo_BSA_API_CXX}
		${DeviceIo_BSA_CYPRESS_CXX}
		${DeviceIo_BSA_CYPRESS_C}
		)
target_include_directories(DeviceIo PUBLIC
		"${DeviceIo_SOURCE_DIR}/bluetooth/bsa"
		"${DeviceIo_SOURCE_DIR}/bluetooth/bsa/cypress/include")
target_link_libraries (DeviceIo libbsa.so libasound.so)
add_definitions(-DCYPRESS_BSA -DBUILDCFG -DPCM_ALSA -DPCM_ALSA_OPEN_BLOCKING)

elseif(BROADCOM)
message("build broadcom bsa...")
file(GLOB_RECURSE DeviceIo_BSA_BROADCOM_CXX "${DeviceIo_SOURCE_DIR}/bluetooth/bsa/broadcom/*.cpp")
file(GLOB_RECURSE DeviceIo_BSA_BROADCOM_C "${DeviceIo_SOURCE_DIR}/bluetooth/bsa/broadcom/*.c")
add_library(DeviceIo SHARED
                ${DeviceIo_SRC}
                ${DeviceIo_SRC_C}
                ${DeviceIo_Properties}
                ${DeviceIo_wifi}
                ${DeviceIo_netlink}
                ${DeviceIo_encode}
		${DeviceIo_BSA_API_CXX}
                ${DeviceIo_BSA_BROADCOM_CXX}
                ${DeviceIo_BSA_BROADCOM_C}
                )
target_include_directories(DeviceIo PUBLIC
		"${DeviceIo_SOURCE_DIR}/bluetooth/bsa"
                "${DeviceIo_SOURCE_DIR}/bluetooth/bsa/broadcom/include")
target_link_libraries (DeviceIo libbsa.so libasound.so)
add_definitions(-DBROADCOM_BSA -DBUILDCFG -DPCM_ALSA -DPCM_ALSA_OPEN_BLOCKING)
endif()

endif()

include_directories(${WPA_SUPPLICANT_INCLUDE_DIRS})
target_link_libraries (DeviceIo libwpa_client.so)

target_include_directories(DeviceIo PUBLIC
		"${DeviceIo_SOURCE_DIR}/include"
		"${DeviceIo_SOURCE_DIR}/src/linux/bluetooth/include"
		"${RAPIDJSON_INCLUDE_DIR}")

add_definitions("-Wall -g -O0 -fpermissive")

# install target
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_PREFIX}/lib)
install(FILES "${DeviceIo_SOURCE_DIR}/src/linux/wifi/rk_airkiss"
        DESTINATION bin
        PERMISSIONS
          OWNER_READ OWNER_WRITE OWNER_EXECUTE
          GROUP_READ GROUP_EXECUTE
          WORLD_READ WORLD_EXECUTE)
