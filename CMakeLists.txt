cmake_minimum_required(VERSION 3.26)

project(ashe)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CONFIG_DIR /etc/ashe)
set(INSTALL_DIR /usr/local/bin)
set(BIN ashe)
set(SRCDIR src)
set(INCDIR src)
set(SRCFILES 
    ${SRCDIR}/ashe.c
    ${SRCDIR}/chiter.c
    ${SRCDIR}/cmdline.c
    ${SRCDIR}/lexer.c
    ${SRCDIR}/parser.c
    ${SRCDIR}/string.c
    ${SRCDIR}/token.c
    ${SRCDIR}/utils.c
    ${SRCDIR}/jobctl.c
    ${SRCDIR}/async.c
    ${SRCDIR}/input.c
    ${SRCDIR}/shell.c
    ${SRCDIR}/builtin.c
    ${SRCDIR}/config.c
    ${SRCDIR}/vec.c)

set(WARNING_FLAGS -Wall -Werror -Wextra -Wpedantic)
set(STANDARD -std=c99)
set(OPTS -O3)

# Binary
add_executable(${BIN} ${SRCFILES})
target_compile_options(${BIN} PRIVATE ${WARNING_FLAGS} ${STANDARD} ${OPTS})
target_link_options(${BIN} PRIVATE -s)
target_include_directories(${BIN} PRIVATE ${INCDIR})
# Install rule
install(TARGETS ${BIN} DESTINATION ${INSTALL_DIR})
install(DIRECTORY DESTINATION ${CONFIG_DIR})
install(FILES ${CMAKE_SOURCE_DIR}/config/config.ashe DESTINATION ${CONFIG_DIR})


# Asan binary
set(ASANBIN ashe_asan)
set(SANITIZER_FLAGS -fsanitize=address -fsanitize=undefined)
add_executable(${ASANBIN} ${SRCFILES})
target_compile_options(${ASANBIN} PRIVATE ${WARNING_FLAGS} ${STANDARD}
    ${SANITIZER_FLAGS} -ggdb)
target_link_options(${ASANBIN} PRIVATE ${SANITIZER_FLAGS})
target_include_directories(${ASANBIN} PRIVATE ${INCDIR})


# Debug binary
set(DBIN dashe)
add_executable(${DBIN} ${SRCFILES}) 
target_compile_options(${DBIN} PRIVATE ${WARNING_FLAGS} ${STANDARD} -ggdb)
target_include_directories(${DBIN} PRIVATE ${INCDIR})
