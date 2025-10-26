cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(dtmerge)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

if (CMAKE_COMPILER_IS_GNUCC)
   add_definitions (-ffunction-sections)
endif ()

add_library (dtovl dtoverlay.c)
target_link_libraries(dtovl fdt)
set_target_properties(dtovl PROPERTIES PUBLIC_HEADER dtoverlay.h)
set_target_properties(dtovl PROPERTIES SOVERSION 0)
install(TARGETS dtovl
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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
