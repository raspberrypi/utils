cmake_minimum_required(VERSION 3.10...3.27)
include(GNUInstallDirs)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(testptp testptp.c)
target_include_directories(testptp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(testptp PRIVATE m)

configure_file(rp1-pps.in ${CMAKE_CURRENT_BINARY_DIR}/rp1-pps @ONLY)
configure_file(rp1-pps.service.in ${CMAKE_CURRENT_BINARY_DIR}/rp1-pps.service @ONLY)
configure_file(rp1-pps-duplex.service.in
               ${CMAKE_CURRENT_BINARY_DIR}/rp1-pps-duplex.service @ONLY)
configure_file(rp1-pps.conf.example.in
               ${CMAKE_CURRENT_BINARY_DIR}/rp1-pps.conf.example @ONLY)

install(TARGETS testptp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/rp1-pps
        DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES rp1_pps.py phc_identity.py
        DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/rp1-ptp-pps)
set(RP1_PPS_SYSTEMD_UNITDIR "lib/systemd/system" CACHE STRING
    "Directory for RP1 PPS systemd unit files")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rp1-pps.service
              ${CMAKE_CURRENT_BINARY_DIR}/rp1-pps-duplex.service
        DESTINATION ${RP1_PPS_SYSTEMD_UNITDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rp1-pps.conf.example
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/rp1-ptp-pps/examples)
install(FILES README.md LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/rp1-ptp-pps)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME rp1-ptp-pps-runtime
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
                -s ${CMAKE_CURRENT_SOURCE_DIR}/tests
                -p test_rp1_pps_runtime.py)
endif()
