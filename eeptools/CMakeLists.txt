cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(eeptools)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

if (CMAKE_COMPILER_IS_GNUCC)
   add_definitions (-ffunction-sections)
endif ()

add_executable(eepmake eepmake.c eeplib.c)
install(TARGETS eepmake RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(eepdump eepdump.c eeplib.c)
install(TARGETS eepdump RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(PROGRAMS eepflash.sh DESTINATION ${CMAKE_INSTALL_BINDIR})
