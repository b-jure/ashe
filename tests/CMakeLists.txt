# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(
    ash_string_test
    ash_string.cpp
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/string.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/utils.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/vec.c
)

add_executable(
    ash_chiter_test
    ash_chiter.cpp
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/string.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/chiter.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/utils.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/vec.c
)

add_executable(
    ash_lexer_test
    ash_lexer.cpp
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/lexer.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/token.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/string.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/chiter.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/utils.c
    ${CMAKE_SOURCE_DIR}/${SRCDIR}/vec.c
)

target_compile_options(ash_lexer_test PRIVATE -fsanitize=address -g)
target_link_options(ash_lexer_test PRIVATE -fsanitize=address)

target_link_libraries(
    ash_string_test
    GTest::gtest_main
)

target_link_libraries(
    ash_chiter_test
    GTest::gtest_main
)

target_link_libraries(
    ash_lexer_test
    GTest::gtest_main
)

add_custom_target(checkleak
    COMMAND env ASAN_OPTIONS=color=always CTEST_OUTPUT_ON_FAILURE=1 GTEST_COLOR=1 ${CMAKE_CTEST_COMMAND}
    DEPENDS ash_string_test ash_chiter_test ash_lexer_test)

add_custom_target(check
    COMMAND env ASAN_OPTIONS=detect_leaks=0:color=always CTEST_OUTPUT_ON_FAILURE=1 GTEST_COLOR=1 ${CMAKE_CTEST_COMMAND}
    DEPENDS ash_string_test ash_chiter_test ash_lexer_test)

configure_file(cmdline.txt ${CMAKE_BINARY_DIR}/tests/cmdline.txt COPYONLY)

include(GoogleTest)
gtest_discover_tests(ash_string_test)
gtest_discover_tests(ash_chiter_test)
gtest_discover_tests(ash_lexer_test)
