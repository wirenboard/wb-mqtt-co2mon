project(wb-mqtt-co2mon)
cmake_minimum_required(VERSION 2.8)

find_package(PkgConfig)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)


include_directories(
    ../libco2mon/include
    ${LIBUSB_INCLUDE_DIRS}
    ${GLIB2_INCLUDE_DIRS}
    ${GIO2_INCLUDE_DIRS})

link_directories(
    ${LIBUSB_LIBRARY_DIRS}
    ${GLIB2_LIBRARY_DIRS}
    ${GIO2_LIBRARY_DIRS})

aux_source_directory(src SRC_LIST)
add_executable(wb-mqtt-co2mon ${SRC_LIST})
target_link_libraries(wb-mqtt-co2mon
    co2mon
    mosquitto)

install (TARGETS wb-mqtt-co2mon DESTINATION bin)
