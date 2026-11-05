cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(examples)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
option(ENABLE_WERROR "Treat compiler warnings as errors" OFF)
if(ENABLE_WERROR)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
endif()

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

add_executable(txcount txcount.c)
target_include_directories(txcount PRIVATE ../include)
target_link_libraries(txcount pio)
install(TARGETS txcount RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(blockingtest blockingtest.c)
target_include_directories(blockingtest PRIVATE ../include)
target_link_libraries(blockingtest pio pthread)
install(TARGETS blockingtest RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(rxsource rxsource.c)
target_include_directories(rxsource PRIVATE ../include)
target_link_libraries(rxsource pio)
install(TARGETS rxsource RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(irqtest irqtest.c)
target_include_directories(irqtest PRIVATE ../include ../../pinctrl)
target_link_libraries(irqtest pio gpiolib)
install(TARGETS irqtest RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(irqhog irqhog.c)
target_include_directories(irqhog PRIVATE ../include)
target_link_libraries(irqhog pio)
install(TARGETS irqhog RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(irqtest2 irqtest2.c)
target_include_directories(irqtest2 PRIVATE ../include ../../pinctrl)
target_link_libraries(irqtest2 pio gpiolib)
install(TARGETS irqtest2 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(txsink txsink.c)
target_include_directories(txsink PRIVATE ../include)
target_link_libraries(txsink pio)
install(TARGETS txsink RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(blink blink.c)
target_include_directories(blink PRIVATE ../include)
target_link_libraries(blink pio)
install(TARGETS blink RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(mirror mirror.c)
target_include_directories(mirror PRIVATE ../include)
target_link_libraries(mirror pio)
install(TARGETS mirror RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
