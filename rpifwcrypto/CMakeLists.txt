cmake_minimum_required(VERSION 3.10...3.27)
include(GNUInstallDirs)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

# Set project name
project(rpifwcrypto)

# Find GnuTLS package
find_package(GnuTLS REQUIRED)

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

# Create the shared library
add_library(rpifwcrypto rpifwcrypto.c)
target_sources(rpifwcrypto PUBLIC rpifwcrypto.h)
set_target_properties(rpifwcrypto PROPERTIES PUBLIC_HEADER rpifwcrypto.h)
set_target_properties(rpifwcrypto PROPERTIES SOVERSION 0)

# Create the executable
add_executable(rpi-fw-crypto main.c)
target_link_libraries(rpi-fw-crypto rpifwcrypto ${GNUTLS_LIBRARIES})
target_include_directories(rpi-fw-crypto PRIVATE ${GNUTLS_INCLUDE_DIRS})

# Install rules
install(TARGETS rpi-fw-crypto RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS rpifwcrypto
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
