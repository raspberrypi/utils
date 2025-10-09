cmake_minimum_required(VERSION 3.10...3.27)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")

#set project name
project(vclog)

#add executables
add_executable(vclog vclog.c)
install(TARGETS vclog RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
