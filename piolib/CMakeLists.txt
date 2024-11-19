cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

add_subdirectory(examples)

#set project name
project(piolib)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

if (CMAKE_COMPILER_IS_GNUCC)
   add_definitions (-ffunction-sections)
endif ()

add_library (pio OBJECT piolib.c pio_rp1.c)
target_include_directories(pio PRIVATE include)
target_link_libraries(pio)
