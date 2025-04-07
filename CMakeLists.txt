project(co2mon)
cmake_minimum_required(VERSION 3.10)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c99")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall -Wextra")

add_subdirectory(libco2mon)
#~ add_subdirectory(co2mond)
add_subdirectory(wb-mqtt-co2mon)
