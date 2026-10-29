cmake_minimum_required(VERSION 3.16)
project(crequests VERSION 1.0.0 LANGUAGES C)

option(CREQUEST_BUILD_EXAMPLES "Build crequests examples" ON)
option(CREQUEST_BUILD_TESTS "Build crequests tests" ON)
option(BUILD_SHARED_LIBS "Build crequests as a DLL/shared library" OFF)

add_library(crequests
    src/util.c
    src/url.c
    src/cookies.c
    src/transport.c
    src/http.c
    src/client.c
)
add_library(crequests::crequests ALIAS crequests)

if(WIN32 AND BUILD_SHARED_LIBS)
    target_compile_definitions(crequests PRIVATE CREQUESTS_BUILD)
    target_compile_definitions(crequests PUBLIC CREQUESTS_SHARED)
    set_target_properties(crequests PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
        OUTPUT_NAME crequests)
    if(MINGW)
        # GNU ld auto-exports every global without this; export only the
        # functions marked CR_API in the public header.
        target_link_options(crequests PRIVATE -Wl,--exclude-all-symbols)
    endif()
endif()

target_include_directories(crequests PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(crequests PUBLIC c_std_11)
target_compile_definitions(crequests PRIVATE _WIN32_WINNT=0x0601)
if(MSVC)
    target_compile_options(crequests PRIVATE /W4)
else()
    target_compile_options(crequests PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(crequests PUBLIC ws2_32 secur32 crypt32)

function(crequests_copy_dll target)
    if(WIN32 AND BUILD_SHARED_LIBS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:crequests> $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()

if(CREQUEST_BUILD_EXAMPLES)
    foreach(example get post_json session)
        add_executable(crequests_${example} examples/${example}.c)
        target_link_libraries(crequests_${example} PRIVATE crequests)
        crequests_copy_dll(crequests_${example})
    endforeach()
endif()

if(CREQUEST_BUILD_TESTS)
    enable_testing()
    add_executable(crequests_tests tests/test_crequests.c)
    target_link_libraries(crequests_tests PRIVATE crequests)
    crequests_copy_dll(crequests_tests)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME crequests_tests
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py
                         $<TARGET_FILE:crequests_tests>)
    else()
        add_test(NAME crequests_tests COMMAND crequests_tests)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS crequests
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES include/crequests.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
