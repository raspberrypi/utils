cmake_minimum_required(VERSION 3.10)

include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")

#set project name
project(dtmerge)

#add executables
add_executable(dtmerge dtmerge.c dtoverlay.c)
target_link_libraries(dtmerge fdt)
install(TARGETS dtmerge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES dtmerge.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
