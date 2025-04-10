cmake_minimum_required(VERSION 3.10...3.27)
include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")

#set project name
project(pinctrl)

add_compile_definitions(LIBRARY_BUILD=1)

add_library(gpiolib STATIC gpiolib.c util.c library_gpiochips.c gpiochip_bcm2835.c gpiochip_bcm2712.c gpiochip_rp1.c)

#add executables
add_executable(pinctrl pinctrl.c)
target_link_libraries(pinctrl gpiolib)
install(TARGETS pinctrl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS gpiolib ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES pinctrl-completion.bash RENAME pinctrl DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/bash-completion/completions")
