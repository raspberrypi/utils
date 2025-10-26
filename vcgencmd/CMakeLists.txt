cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(vcgencmd)

add_executable(vcgencmd vcgencmd.c)
install(TARGETS vcgencmd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES vcgencmd.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES vcgencmd-completion.bash RENAME vcgencmd DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/bash-completion/completions")
