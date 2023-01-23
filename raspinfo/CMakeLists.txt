cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

#set project name
project(raspinfo)

#add executables
install(PROGRAMS raspinfo DESTINATION ${CMAKE_INSTALL_BINDIR})
