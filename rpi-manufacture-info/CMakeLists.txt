cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

project(rpi-manufacture-info)

install(PROGRAMS rpi-manufacture-info DESTINATION ${CMAKE_INSTALL_BINDIR})
