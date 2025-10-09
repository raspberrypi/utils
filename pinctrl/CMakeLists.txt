cmake_minimum_required(VERSION 3.10...3.27)
include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

#set project name
project(pinctrl)

add_compile_definitions(LIBRARY_BUILD=1)

add_library(gpiolib gpiolib.c util.c library_gpiochips.c gpiochip_bcm2835.c gpiochip_bcm2712.c gpiochip_rp1.c gpiochip_firmware.c)
target_sources(gpiolib PUBLIC gpiolib.h)
set_target_properties(gpiolib PROPERTIES PUBLIC_HEADER gpiolib.h)
set_target_properties(gpiolib PROPERTIES SOVERSION 0)

#add executables
add_executable(pinctrl pinctrl.c)
target_link_libraries(pinctrl gpiolib)
install(TARGETS pinctrl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS gpiolib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES pinctrl-completion.bash RENAME pinctrl DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/bash-completion/completions")
