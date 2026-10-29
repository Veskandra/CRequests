cmake_minimum_required(VERSION 3.16)
project(cpprequests VERSION 1.0.0 LANGUAGES C CXX)

option(CPPREQUESTS_BUILD_EXAMPLES "Build cpprequests examples" ON)
option(CPPREQUESTS_BUILD_TESTS "Build cpprequests tests" ON)
option(BUILD_SHARED_LIBS "Build cpprequests as a DLL/shared library" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(CORE_SOURCES
    "${CORE_DIR}/util.c"
    "${CORE_DIR}/url.c"
    "${CORE_DIR}/cookies.c"
    "${CORE_DIR}/transport.c"
    "${CORE_DIR}/http.c"
    "${CORE_DIR}/client.c"
)

add_library(cpprequests
    src/cpprequests.cpp
    ${CORE_SOURCES}
)
add_library(cpprequests::cpprequests ALIAS cpprequests)

target_include_directories(cpprequests
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_features(cpprequests PUBLIC cxx_std_17)
target_compile_definitions(cpprequests PRIVATE _WIN32_WINNT=0x0601)
target_link_libraries(cpprequests PUBLIC ws2_32 secur32 crypt32)

if(MSVC)
    target_compile_options(cpprequests PRIVATE /W4)
else()
    target_compile_options(cpprequests PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(cpprequests PRIVATE CPPR_BUILD)
    target_compile_definitions(cpprequests PUBLIC CPPR_SHARED)
    set_target_properties(cpprequests PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        OUTPUT_NAME cpprequests)
    if(MINGW)
        target_link_options(cpprequests PRIVATE -Wl,--exclude-all-symbols)
    endif()
endif()

function(cpprequests_copy_dll target)
    if(WIN32 AND BUILD_SHARED_LIBS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:cpprequests> $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()

if(CPPREQUESTS_BUILD_EXAMPLES)
    foreach(example get post_json session json_demo)
        add_executable(cpprequests_${example} examples/${example}.cpp)
        target_link_libraries(cpprequests_${example} PRIVATE cpprequests)
        cpprequests_copy_dll(cpprequests_${example})
    endforeach()
endif()

if(CPPREQUESTS_BUILD_TESTS)
    enable_testing()
    add_executable(cpprequests_tests tests/test_cpprequests.cpp)
    target_link_libraries(cpprequests_tests PRIVATE cpprequests)
    cpprequests_copy_dll(cpprequests_tests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cpprequests_tests
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py
                         $<TARGET_FILE:cpprequests_tests>)
    else()
        add_test(NAME cpprequests_tests COMMAND cpprequests_tests)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS cpprequests
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/cpprequests.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES include/cpprequests/json.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cpprequests)
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
