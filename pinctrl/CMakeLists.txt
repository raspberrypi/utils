cmake_minimum_required(VERSION 3.10)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")

#set project name
project(pinctrl)

#add executables
add_executable(pinctrl pinctrl.c gpiolib.c util.c gpiochip_bcm2835.c)
install(TARGETS pinctrl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
