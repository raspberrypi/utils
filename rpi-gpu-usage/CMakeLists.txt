cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

project(rpi-gpu-usage)

find_package(Curses REQUIRED)

add_executable(rpi-gpu-usage rpi-gpu-usage.c)
target_include_directories(rpi-gpu-usage PRIVATE ${CURSES_INCLUDE_DIRS})
target_link_libraries(rpi-gpu-usage ${CURSES_LIBRARIES})
install(TARGETS rpi-gpu-usage RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES rpi-gpu-usage.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES rpi-gpu-usage-completion.bash RENAME rpi-gpu-usage DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/bash-completion/completions")
