cmake_minimum_required(VERSION 3.10...3.27)

add_compile_options(-Wall -Wextra $<$<CONFIG:Debug>:-Werror> -pedantic)

#set project name
project(vclog)

#add executables
add_executable(vclog vclog.c)
install(TARGETS vclog RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
