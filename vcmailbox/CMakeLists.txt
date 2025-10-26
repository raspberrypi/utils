cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

#set project name
project(vcmailbox)

add_executable(vcmailbox vcmailbox.c)
target_link_libraries(vcmailbox)

install(TARGETS vcmailbox RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES vcmailbox.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES vcmailbox.7 raspiotp.7 raspirev.7 DESTINATION ${CMAKE_INSTALL_MANDIR}/man7)
