cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(splashasm)

add_subdirectory(parser)

install(PROGRAMS splash_assembler.py DESTINATION ${CMAKE_INSTALL_BINDIR} RENAME splash-assembler)
