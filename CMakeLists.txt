cmake_minimum_required(VERSION 3.1...3.27)

project(utils)

# List of subsidiary CMakeLists
add_subdirectory(dtmerge)
add_subdirectory(eeptools)
add_subdirectory(kdtc)
add_subdirectory(otpset)
add_subdirectory(overlaycheck)
add_subdirectory(ovmerge)
add_subdirectory(pinctrl)
add_subdirectory(piolib)
add_subdirectory(raspinfo)
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
