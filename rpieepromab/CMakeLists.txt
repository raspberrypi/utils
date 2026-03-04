cmake_minimum_required(VERSION 3.10...3.27)
include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Set project name and version
project(rpieepromab VERSION 0.2.1)

# Get git hash for version string
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(GIT_HASH)
    set(VERSION_FULL "${PROJECT_VERSION} \(${GIT_HASH}\)")
else()
    set(VERSION_FULL "${PROJECT_VERSION}")
endif()

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

# Create the shared library
add_library(rpieepromab rpieepromab.c)
target_sources(rpieepromab PUBLIC rpieepromab.h)
set_target_properties(rpieepromab PROPERTIES PUBLIC_HEADER rpieepromab.h)
set_target_properties(rpieepromab PROPERTIES SOVERSION 0)
target_compile_definitions(rpieepromab PRIVATE
    LIBRPIEEPROMAB_VERSION="${VERSION_FULL}")

# Create the executable
add_executable(rpi-eeprom-ab main.c)
target_link_libraries(rpi-eeprom-ab rpieepromab)
target_include_directories(rpi-eeprom-ab PRIVATE)
target_compile_definitions(rpi-eeprom-ab PRIVATE
    VERSION_STRING="${VERSION_FULL}")

# Install rules
install(TARGETS rpi-eeprom-ab RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS rpieepromab
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
