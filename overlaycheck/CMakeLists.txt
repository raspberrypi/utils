cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(overlaycheck)

#add executables
install(PROGRAMS overlaycheck DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES overlaycheck_exclusions.txt DESTINATION ${CMAKE_INSTALL_BINDIR})
