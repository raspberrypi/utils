cmake_minimum_required(VERSION 3.1...3.27)

project(utils)

option(ENABLE_WERROR "Treat compiler warnings as errors" OFF)

# Customize binaries for specific distributions
set(KNOWN_DISTRIBUTIONS debian gentoo generic)
option(DISTRIBUTION "Enable distribution specific functions" debian) # debian is the base of raspberry pi os
if(NOT DISTRIBUTION IN_LIST KNOWN_DISTRIBUTIONS)
  message(STATUS "specified distribution not known - assuming generic distribution")
  set(DISTRIBUTION generic)
endif()

# List of subsidiary CMakeLists
add_subdirectory(dtapply)
add_subdirectory(dtmerge)
add_subdirectory(eeptools)
add_subdirectory(kdtc)
add_subdirectory(otamaker)
add_subdirectory(otpset)
add_subdirectory(overlaycheck)
add_subdirectory(ovmerge)
add_subdirectory(pinctrl)
add_subdirectory(piolib)
add_subdirectory(raspinfo)
add_subdirectory(rpieepromab)
add_subdirectory(rpi-gpu-usage)
add_subdirectory(vcgencmd)
add_subdirectory(vclog)
add_subdirectory(vcmailbox)

# Only build rpifwcrypto if GnuTLS is available
include(CheckIncludeFile)
check_include_file(gnutls/crypto.h HAVE_GNUTLS_CRYPTO_H)
if(HAVE_GNUTLS_CRYPTO_H)
    add_subdirectory(rpifwcrypto)
else()
    message(STATUS "gnutls/crypto.h not found - skipping rpifwcrypto")
endif()
