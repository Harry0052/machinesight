cmake_minimum_required(VERSION 3.16)
project(machinesight VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(MACHINESIGHT_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(machinesight_core STATIC
    src/parse.c
    src/compare.c
    src/report.c
)
target_include_directories(machinesight_core PUBLIC include)

add_executable(machinesight src/main.c)
target_link_libraries(machinesight PRIVATE machinesight_core)

function(machinesight_target target)
    target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wshadow
        -Wstrict-prototypes
        -Wmissing-prototypes
        -Wconversion
        -Wsign-conversion
        -Wformat=2
        -Wnull-dereference
    )
    if(MACHINESIGHT_SANITIZE)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=address,undefined
        )
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

machinesight_target(machinesight_core)
machinesight_target(machinesight)

enable_testing()

add_executable(test_parse tests/test_parse.c)
target_link_libraries(test_parse PRIVATE machinesight_core)
machinesight_target(test_parse)
add_test(NAME parse COMMAND test_parse ${CMAKE_SOURCE_DIR})

add_executable(test_compare tests/test_compare.c)
target_link_libraries(test_compare PRIVATE machinesight_core)
machinesight_target(test_compare)
add_test(NAME compare COMMAND test_compare ${CMAKE_SOURCE_DIR})

add_test(NAME cli
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/test_cli.sh $<TARGET_FILE:machinesight> ${CMAKE_SOURCE_DIR}
)
set_tests_properties(parse compare cli PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
