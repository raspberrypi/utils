cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

#set project name
project(otpset)

#add executables
install(PROGRAMS otpset DESTINATION ${CMAKE_INSTALL_BINDIR})
