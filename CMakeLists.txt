cmake_minimum_required(VERSION 3.20)
project(CTR-Native C)
include(CheckCCompilerFlag)

set(CTR_NATIVE_VERSION "0.1.0-beta.7.1")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
        "CTR Native currently requires a 32-bit target. "
        "Select Win32 for MSVC, an i686 MinGW compiler on Windows, or pass -m32 "
        "in the initial compiler and linker flags on Linux."
    )
endif()

# Sanitizers for regression and leak hunting: -DCTR_NATIVE_SANITIZE="address;undefined" (GCC/Clang; "leak" too where
# LeakSanitizer runs: Linux), or "address" with MSVC/clang-cl. Off by default; tools/check.py --sanitize builds them.
set(CTR_NATIVE_SANITIZE "" CACHE STRING "Sanitizers for ctr_native and its tests (address, undefined, leak)")

set(CTR_NATIVE_MSVC_FRONTEND OFF)
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    set(CTR_NATIVE_MSVC_FRONTEND ON)
    if("address" IN_LIST CTR_NATIVE_SANITIZE)
        # AddressSanitizer on Windows runs on the DLL C runtime (/MD; its runtime is built against the release CRT),
        # and /RTC run-time checks can't go with it. Every target, SDL too, takes the same CRT.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
        string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    else()
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE NEVER)

execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE CTR_NATIVE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT CTR_NATIVE_GIT_HASH)
    set(CTR_NATIVE_GIT_HASH "unknown")
endif()

execute_process(
    COMMAND git diff --quiet --exit-code
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CTR_NATIVE_GIT_DIRTY
    ERROR_QUIET
)
if(CTR_NATIVE_GIT_DIRTY EQUAL 0)
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}")
else()
    set(CTR_NATIVE_BUILD_ID "${CTR_NATIVE_GIT_HASH}-dirty")
endif()
# The CTR Launcher package carries the source without .git: BUILD_ID names its commit.
# (The launcher itself is launcher/, a Tauri app built with Bun and Cargo, not by this file.)
if(CTR_NATIVE_GIT_HASH STREQUAL "unknown" AND EXISTS "${CMAKE_SOURCE_DIR}/BUILD_ID")
    file(STRINGS "${CMAKE_SOURCE_DIR}/BUILD_ID" CTR_NATIVE_BUILD_ID LIMIT_COUNT 1)
endif()

function(ctr_native_add_label_warning_if_supported target warning flag_check)
    set(saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror=unknown-warning-option")
    endif()

    check_c_compiler_flag("${warning}" "${flag_check}")
    set(CMAKE_REQUIRED_FLAGS "${saved_required_flags}")

    if(${flag_check})
        target_compile_options(${target} PRIVATE "${warning}")
    endif()
endfunction()

# SDL3
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_TRAY OFF CACHE BOOL "" FORCE)
set(SDL_POWER OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO OFF CACHE BOOL "" FORCE)
set(SDL_SNDIO_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_X11_XSCRNSAVER OFF CACHE BOOL "" FORCE)
set(SDL_X11_XTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(externals/SDL)

# CTR Native
add_executable(ctr_native main.c)
set_target_properties(ctr_native PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
target_include_directories(ctr_native PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_definitions(ctr_native PRIVATE
    CTR_NATIVE
    CTR_INTERNAL
    CTR_NATIVE_VERSION=\"${CTR_NATIVE_VERSION}\"
    CTR_NATIVE_BUILD_ID=\"${CTR_NATIVE_BUILD_ID}\"
)
if(CTR_NATIVE_MSVC_FRONTEND)
    target_compile_options(ctr_native PRIVATE
        /W4 #TODO(aalhendi): fix warnings
        /bigobj
        /arch:SSE2
        /wd4152
        /wd4244
        /wd4245
        /wd4310
        /wd5287
    )
else()
    target_compile_options(ctr_native PRIVATE
        -msse
        -Wenum-conversion
        -Wenum-compare
        -Wswitch-enum
        -Wformat
        -Wparentheses
        $<$<CONFIG:Release>:-O2>
        $<$<CONFIG:Release>:-g>
    )

    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        target_compile_options(ctr_native PRIVATE -Wall -Wextra -Wstrict-aliasing=2)
        ctr_native_add_label_warning_if_supported(ctr_native -Wfree-labels CTR_HAS_WFREE_LABELS)
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        ctr_native_add_label_warning_if_supported(ctr_native -Wc23-extensions CTR_HAS_WC23_EXTENSIONS)
    endif()
endif()
target_link_libraries(ctr_native SDL3::SDL3)

# clang-cl leaves AddressSanitizer's runtime to its own link step, but CMake links with lld-link or link.exe directly:
# name the 32-bit runtime and its CRT thunk. LLVM's Windows installer has only 64-bit ones; Visual Studio's x86
# environment (vcvars32) has MSVC's on LIB, and their DLL on PATH.
if(CTR_NATIVE_MSVC_FRONTEND AND CMAKE_C_COMPILER_ID MATCHES "Clang" AND "address" IN_LIST CTR_NATIVE_SANITIZE)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} /clang:-print-resource-dir
        OUTPUT_VARIABLE ctr_clang_resource_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    find_library(CTR_NATIVE_ASAN_RUNTIME NAMES clang_rt.asan_dynamic-i386.lib
        HINTS "${ctr_clang_resource_dir}/lib/windows" ENV LIB)
    find_library(CTR_NATIVE_ASAN_THUNK NAMES clang_rt.asan_dynamic_runtime_thunk-i386.lib
        HINTS "${ctr_clang_resource_dir}/lib/windows" ENV LIB)
    if(NOT CTR_NATIVE_ASAN_RUNTIME OR NOT CTR_NATIVE_ASAN_THUNK)
        message(FATAL_ERROR
            "AddressSanitizer with clang-cl needs clang_rt.asan_dynamic-i386.lib and "
            "clang_rt.asan_dynamic_runtime_thunk-i386.lib. Configure from Visual Studio's x86 environment "
            "(vcvars32.bat; tools/check.py does), which has MSVC's on LIB.")
    endif()
    get_filename_component(ctr_asan_lib_dir "${CTR_NATIVE_ASAN_RUNTIME}" DIRECTORY)
    find_file(CTR_NATIVE_ASAN_DLL NAMES clang_rt.asan_dynamic-i386.dll
        HINTS "${ctr_asan_lib_dir}" "${ctr_asan_lib_dir}/../../bin/Hostx86/x86" "${ctr_asan_lib_dir}/../../bin/Hostx64/x86"
              "${ctr_clang_resource_dir}/lib/windows" ENV PATH)
endif()

function(ctr_native_sanitize target)
    if(NOT CTR_NATIVE_SANITIZE)
        return()
    endif()
    if(CTR_NATIVE_MSVC_FRONTEND)
        if("address" IN_LIST CTR_NATIVE_SANITIZE)
            target_compile_options(${target} PRIVATE /fsanitize=address /Z7)
            target_link_options(${target} PRIVATE /INCREMENTAL:NO /DEBUG)
            if(CTR_NATIVE_ASAN_RUNTIME)
                # Link options, not target_link_libraries: ctr_native already uses its plain signature.
                target_link_options(${target} PRIVATE "${CTR_NATIVE_ASAN_RUNTIME}" "/WHOLEARCHIVE:${CTR_NATIVE_ASAN_THUNK}")
            endif()
            if(CTR_NATIVE_ASAN_DLL)
                # The exe then starts from any folder (a replay check, the launcher), not only from vcvars32's PATH.
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CTR_NATIVE_ASAN_DLL}" "$<TARGET_FILE_DIR:${target}>")
            endif()
        endif()
    else()
        string(REPLACE ";" "," sanitizers "${CTR_NATIVE_SANITIZE}")
        target_compile_options(${target} PRIVATE -fsanitize=${sanitizers} -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g)
        target_link_options(${target} PRIVATE -fsanitize=${sanitizers})
    endif()
endfunction()
ctr_native_sanitize(ctr_native)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(ctr_native PRIVATE -static-libgcc)
endif()
if(MINGW)
    target_link_options(ctr_native PRIVATE -static)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME ctr_native_version COMMAND $<TARGET_FILE:ctr_native> --version)
    set_tests_properties(ctr_native_version PROPERTIES PASS_REGULAR_EXPRESSION "^CTR Native ")
    # A checkpoint another build wrote (other addresses) restores its function and data pointers here.
    add_test(NAME ctr_checkpoint_translation COMMAND $<TARGET_FILE:ctr_native> --checkpoint-selftest)

    add_executable(ctr_native_config_test tests/native_config_test.c platform/native_config.c)
    set_target_properties(ctr_native_config_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(ctr_native_config_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    ctr_native_sanitize(ctr_native_config_test)
    add_test(NAME ctr_native_config COMMAND $<TARGET_FILE:ctr_native_config_test>)

    add_executable(ctr_prim_arena_test tests/prim_arena_test.c)
    set_target_properties(ctr_prim_arena_test PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(ctr_prim_arena_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_prim_arena_test PRIVATE CTR_NATIVE)
    ctr_native_sanitize(ctr_prim_arena_test)
    add_test(NAME ctr_prim_arena COMMAND $<TARGET_FILE:ctr_prim_arena_test>)

    add_executable(ctr_visibility_raw_copy_regression tests/visibility_raw_copy_regression.c)
    set_target_properties(ctr_visibility_raw_copy_regression PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
    )
    target_include_directories(ctr_visibility_raw_copy_regression PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_definitions(ctr_visibility_raw_copy_regression PRIVATE CTR_NATIVE)
    ctr_native_sanitize(ctr_visibility_raw_copy_regression)
    add_test(NAME ctr_visibility_raw_copy_regression COMMAND $<TARGET_FILE:ctr_visibility_raw_copy_regression>)

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME ctr_match_unit
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
            -s ${CMAKE_SOURCE_DIR}/tools/matching
            -p test_*.py
    )

    # Every matched unit's compiled bytes against metadata/retail/ntsc-u-926/match_lock.json (no retail disc needed).
    # Exit 2 = the vendored PSX toolchain is not built on this machine: skipped, not passed.
    add_test(
        NAME ctr_match_lock
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/matching/lock.py check
    )
    set_tests_properties(ctr_match_lock PROPERTIES SKIP_RETURN_CODE 2)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_executable(ctr_particle_renderer_smoke
            tests/particle_renderer_smoke.c
            platform/native_gpu_links.c
        )
        set_target_properties(ctr_particle_renderer_smoke PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        target_include_directories(ctr_particle_renderer_smoke PRIVATE ${CMAKE_SOURCE_DIR}/include)
        target_compile_definitions(ctr_particle_renderer_smoke PRIVATE CTR_NATIVE)
        target_compile_options(ctr_particle_renderer_smoke PRIVATE
            -O2 -Wall -Wextra -ffunction-sections -fdata-sections
        )
        target_link_options(ctr_particle_renderer_smoke PRIVATE -Wl,--gc-sections)
        ctr_native_sanitize(ctr_particle_renderer_smoke)
        add_test(NAME ctr_particle_renderer_smoke COMMAND $<TARGET_FILE:ctr_particle_renderer_smoke>)

        add_executable(ctr_gte_gpl_shift_regression
            tests/gte_gpl_shift_regression.c
            platform/native_gte_core.c
        )
        set_target_properties(ctr_gte_gpl_shift_regression PROPERTIES
            C_STANDARD 17
            C_STANDARD_REQUIRED ON
            C_EXTENSIONS OFF
        )
        target_include_directories(ctr_gte_gpl_shift_regression PRIVATE ${CMAKE_SOURCE_DIR}/include)
        target_compile_definitions(ctr_gte_gpl_shift_regression PRIVATE CTR_NATIVE)
        target_compile_options(ctr_gte_gpl_shift_regression PRIVATE
            -O2 -Wall -Wextra -fsanitize=undefined -fno-sanitize-recover=all
        )
        target_link_options(ctr_gte_gpl_shift_regression PRIVATE -fsanitize=undefined -fno-sanitize-recover=all)
        add_test(NAME ctr_gte_gpl_shift_regression COMMAND $<TARGET_FILE:ctr_gte_gpl_shift_regression>)
    endif()
endif()
