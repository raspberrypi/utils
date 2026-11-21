cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

project(rpi-pmic)

install(PROGRAMS rpi-pmic DESTINATION ${CMAKE_INSTALL_BINDIR})
