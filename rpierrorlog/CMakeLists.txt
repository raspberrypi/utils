cmake_minimum_required(VERSION 3.10...3.27)
include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

project(rpierrorlog)

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

add_library(rpierrorlog rpierrorlog.c)
target_sources(rpierrorlog PUBLIC rpierrorlog.h)
set_target_properties(rpierrorlog PROPERTIES PUBLIC_HEADER rpierrorlog.h)
set_target_properties(rpierrorlog PROPERTIES SOVERSION 0)

add_executable(rpi-error-log main.c)
target_link_libraries(rpi-error-log rpierrorlog)

install(TARGETS rpi-error-log RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS rpierrorlog
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
