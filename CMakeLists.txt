cmake_minimum_required(VERSION 3.26)

project(ashe)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(BIN ashe)
set(GDBBIN ashegdb)
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
    ${SRCDIR}/vec.c)

set(WARNING_FLAGS -Wall -Werror -Wextra -Wpedantic)
set(SANITIZER_FLAGS -fsanitize=address -fsanitize=undefined)
set(STANDARD -std=c11)
set(OPTS -Os)

add_executable(${BIN} ${SRCFILES})
target_compile_options(${BIN} PRIVATE ${WARNING_FLAGS} ${STANDARD} ${OPTS})
target_include_directories(${BIN} PRIVATE ${INCDIR})

# Install rule
install(TARGETS ${BIN} DESTINATION /usr/local/bin)

if(DEFINED ASHE_DBUG AND ASHE_DBUG STREQUAL "ON")
    add_executable(${GDBBIN} ${SRCFILES})
    target_compile_options(${GDBBIN} PRIVATE ${WARNING_FLAGS} ${STANDARD}
        ${SANITIZER_FLAGS} -g)
    target_link_options(${GDBBIN} PRIVATE ${SANITIZER_FLAGS})
    target_include_directories(${GDBBIN} PRIVATE ${INCDIR})
endif()

# Testing is for schmucks
if(DEFINED ASHE_GTEST AND ASHE_GTEST STREQUAL "ON")
    # TODO: no testing srsly?? yes.
endif()
