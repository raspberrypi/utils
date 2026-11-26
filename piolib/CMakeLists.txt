cmake_minimum_required(VERSION 3.10...3.27)

#set project name
project(piolib)

# After project(), so that the target architecture (and hence the Debian
# multiarch library directory) is known
include(GNUInstallDirs)

add_subdirectory(examples)

add_compile_definitions(LIBRARY_BUILD=1)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
option(ENABLE_WERROR "Treat compiler warnings as errors" OFF)
if(ENABLE_WERROR)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
endif()

if (CMAKE_COMPILER_IS_GNUCC)
   add_definitions (-ffunction-sections)
endif ()

add_library (pio piolib.c library_piochips.c pio_rp1.c)
target_include_directories(pio PUBLIC include)
set_target_properties(pio PROPERTIES SOVERSION 0)

set(INCLUDE_FILES
    "piolib.h"
    "pio_platform.h"
    "hardware/clocks.h"
    "hardware/gpio.h"
    "hardware/pio.h"
    "hardware/pio_instructions.h"
    "hardware/timer.h"
    "pico/stdlib.h"
)

install(TARGETS pio
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT piolib
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT piolib)

# pkg-config file; packagers can set PIOLIB_VERSION to the package version
set(PIOLIB_VERSION "0" CACHE STRING "Version reported by libpio.pc")
configure_file(libpio.pc.in libpio.pc @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libpio.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig" COMPONENT piolib)

foreach ( file ${INCLUDE_FILES} )
    get_filename_component( dir ${file} DIRECTORY )
    install( FILES "include/${file}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/piolib/${dir}" COMPONENT piolib)
endforeach()
