cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(otpset)

#add executables
install(PROGRAMS otpset DESTINATION ${CMAKE_INSTALL_BINDIR})
