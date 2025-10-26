cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(kdtc)

#add executables
install(PROGRAMS kdtc DESTINATION ${CMAKE_INSTALL_BINDIR})
