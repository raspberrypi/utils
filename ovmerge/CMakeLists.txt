cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

#set project name
project(ovmerge)

#add executables
install(PROGRAMS ovmerge DESTINATION ${CMAKE_INSTALL_BINDIR})
