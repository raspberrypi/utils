cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(raspinfo)

#add executables
install(PROGRAMS raspinfo DESTINATION ${CMAKE_INSTALL_BINDIR})
