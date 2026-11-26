cmake_minimum_required(VERSION 3.10...3.27)

#set project name
project(examples)

include(GNUInstallDirs)

function(install_example target)
    if(PIOLIB_INSTALL_EXAMPLES)
        install(TARGETS ${target} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT examples)
    endif()
endfunction()

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
install_example(apitest)

add_executable(piotest piotest.c)
target_include_directories(piotest PRIVATE ../include)
target_link_libraries(piotest pio)
install_example(piotest)

add_executable(piopwm pwm.c)
target_include_directories(piopwm PRIVATE ../include)
target_link_libraries(piopwm pio)
install_example(piopwm)

add_executable(pioseq genseq.c)
target_include_directories(pioseq PRIVATE ../include)
target_link_libraries(pioseq pio)
install_example(pioseq)

add_executable(piows2812 ws2812.c)
target_include_directories(piows2812 PRIVATE ../include)
target_link_libraries(piows2812 pio)
install_example(piows2812)

add_executable(dpi_csync dpi_csync.c)
target_include_directories(dpi_csync PRIVATE ../include)
target_link_libraries(dpi_csync pio)
install_example(dpi_csync)

add_executable(rp1sm rp1sm.c)
target_include_directories(rp1sm PRIVATE ../include)
target_link_libraries(rp1sm pio)
install_example(rp1sm)

add_executable(quadenc quadrature_encoder.c)
target_include_directories(quadenc PRIVATE ../include)
target_link_libraries(quadenc pio)
install_example(quadenc)

add_executable(txcount txcount.c)
target_include_directories(txcount PRIVATE ../include)
target_link_libraries(txcount pio)
install_example(txcount)

add_executable(blockingtest blockingtest.c)
target_include_directories(blockingtest PRIVATE ../include)
target_link_libraries(blockingtest pio pthread)
install_example(blockingtest)

add_executable(rxsource rxsource.c)
target_include_directories(rxsource PRIVATE ../include)
target_link_libraries(rxsource pio)
install_example(rxsource)

add_executable(irqtest irqtest.c)
target_include_directories(irqtest PRIVATE ../include ../../pinctrl)
target_link_libraries(irqtest pio gpiolib)
install_example(irqtest)

add_executable(irqhog irqhog.c)
target_include_directories(irqhog PRIVATE ../include)
target_link_libraries(irqhog pio)
install_example(irqhog)

add_executable(irqtest2 irqtest2.c)
target_include_directories(irqtest2 PRIVATE ../include ../../pinctrl)
target_link_libraries(irqtest2 pio gpiolib)
install_example(irqtest2)

add_executable(txsink txsink.c)
target_include_directories(txsink PRIVATE ../include)
target_link_libraries(txsink pio)
install_example(txsink)

add_executable(blink blink.c)
target_include_directories(blink PRIVATE ../include)
target_link_libraries(blink pio)
install_example(blink)

add_executable(mirror mirror.c)
target_include_directories(mirror PRIVATE ../include)
target_link_libraries(mirror pio)
install_example(mirror)
