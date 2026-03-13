cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

# set project name
project(otamaker)

# add executables
install(PROGRAMS otamaker DESTINATION ${CMAKE_INSTALL_BINDIR})
