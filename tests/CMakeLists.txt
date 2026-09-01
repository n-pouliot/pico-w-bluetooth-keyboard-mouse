cmake_minimum_required(VERSION 3.16)

project(xbox_pico_gate1_tests LANGUAGES C)

enable_testing()

add_executable(gate1_tests
    test_main.c
    test_hid_report_parser.c
    test_pairing_store.c
    test_bridge_mailbox.c
    ../src/hid_report_parser.c
    ../src/pairing_store.c
    ../src/bridge_mailbox.c
)

target_include_directories(gate1_tests PRIVATE stubs ../src)
set_target_properties(gate1_tests PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(gate1_tests PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(gate1_tests PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(NAME gate1_core COMMAND gate1_tests)
