cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(dtmerge)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

if (CMAKE_COMPILER_IS_GNUCC)
   add_definitions (-ffunction-sections)
endif ()

add_library (dtovl ${STATIC} dtoverlay.c)
target_link_libraries(dtovl fdt)

add_executable(dtmerge dtmerge.c)
target_link_libraries(dtmerge dtovl)
install(TARGETS dtmerge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES dtmerge.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

add_executable(dtoverlay dtoverlay_main.c utils.c)
target_link_libraries(dtoverlay dtovl)
install(TARGETS dtoverlay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES dtoverlay.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

add_custom_command(TARGET dtoverlay POST_BUILD COMMAND ln;-sf;dtoverlay;dtparam)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dtparam DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES dtparam.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
