cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(examples)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

if (CMAKE_COMPILER_IS_GNUCC)
   add_definitions (-ffunction-sections)
endif ()

add_executable(apitest apitest.c)
target_include_directories(apitest PRIVATE ../include)
target_link_libraries(apitest pio)
install(TARGETS apitest RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(piotest piotest.c)
target_include_directories(piotest PRIVATE ../include)
target_link_libraries(piotest pio)
install(TARGETS piotest RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(piopwm pwm.c)
target_include_directories(piopwm PRIVATE ../include)
target_link_libraries(piopwm pio)
install(TARGETS piopwm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(pioseq genseq.c)
target_include_directories(pioseq PRIVATE ../include)
target_link_libraries(pioseq pio)
install(TARGETS pioseq RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(piows2812 ws2812.c)
target_include_directories(piows2812 PRIVATE ../include)
target_link_libraries(piows2812 pio)
install(TARGETS piows2812 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(dpi_csync dpi_csync.c)
target_include_directories(dpi_csync PRIVATE ../include)
target_link_libraries(dpi_csync pio)
install(TARGETS dpi_csync RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(rp1sm rp1sm.c)
target_include_directories(rp1sm PRIVATE ../include)
target_link_libraries(rp1sm pio)
install(TARGETS rp1sm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(quadenc quadrature_encoder.c)
target_include_directories(quadenc PRIVATE ../include)
target_link_libraries(quadenc pio)
install(TARGETS quadenc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
