cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

add_subdirectory(examples)

#set project name
project(piolib)

add_compile_definitions(LIBRARY_BUILD=1)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

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

install(TARGETS pio ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT piolib)
foreach ( file ${INCLUDE_FILES} )
    get_filename_component( dir ${file} DIRECTORY )
    install( FILES "include/${file}" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/piolib/${dir}" COMPONENT piolib)
endforeach()
