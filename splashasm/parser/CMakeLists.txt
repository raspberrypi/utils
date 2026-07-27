cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(parse_splash_bin)

add_executable(parse_splash_bin parse_splash_bin.c)
install(TARGETS parse_splash_bin RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
