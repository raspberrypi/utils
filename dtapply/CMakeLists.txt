cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

project(dtapply)

install(PROGRAMS dtapply DESTINATION ${CMAKE_INSTALL_BINDIR})
