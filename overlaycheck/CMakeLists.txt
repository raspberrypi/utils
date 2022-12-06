cmake_minimum_required(VERSION 3.10)

#set project name
project(overlaycheck)

#add executables
install(PROGRAMS overlaycheck DESTINATION bin)
install(FILES overlaycheck_exclusions.txt DESTINATION bin)
