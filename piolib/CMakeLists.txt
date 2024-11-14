cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(piolib)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

if (CMAKE_COMPILER_IS_GNUCC)
   add_definitions (-ffunction-sections)
endif ()

add_library (pio OBJECT piolib.c pio_rp1.c)
target_include_directories(pio PRIVATE .)
target_link_libraries(pio)

add_executable(piotest piotest.c)
target_include_directories(piotest PRIVATE .)
target_link_libraries(piotest pio)
install(TARGETS piotest RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(piopwm pwm.c)
target_include_directories(piopwm PRIVATE .)
target_link_libraries(piopwm pio)
install(TARGETS piopwm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(pioseq genseq.c)
target_include_directories(pioseq PRIVATE .)
target_link_libraries(pioseq pio)
install(TARGETS pioseq RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(piows2812 ws2812.c)
target_include_directories(piows2812 PRIVATE .)
target_link_libraries(piows2812 pio)
install(TARGETS piows2812 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(rp1sm rp1sm.c)
target_include_directories(rp1sm PRIVATE .)
target_link_libraries(rp1sm pio)
install(TARGETS rp1sm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
