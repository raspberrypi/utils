cmake_minimum_required(VERSION 3.10...3.27)
include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Set project name
project(rpieepromab)

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

# Create the shared library
add_library(rpieepromab rpieepromab.c)
target_sources(rpieepromab PUBLIC rpieepromab.h)
set_target_properties(rpieepromab PROPERTIES PUBLIC_HEADER rpieepromab.h)
set_target_properties(rpieepromab PROPERTIES SOVERSION 0)

# Create the executable
add_executable(rpi-eeprom-ab main.c)
target_link_libraries(rpi-eeprom-ab rpieepromab)
target_include_directories(rpi-eeprom-ab PRIVATE)

# Install rules
install(TARGETS rpi-eeprom-ab RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS rpieepromab
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
