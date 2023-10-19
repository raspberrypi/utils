cmake_minimum_required(VERSION 3.10)

add_executable(vcgencmd vcgencmd.c)
install(TARGETS vcgencmd RUNTIME DESTINATION bin)
install(FILES vcgencmd.1 DESTINATION man/man1)
