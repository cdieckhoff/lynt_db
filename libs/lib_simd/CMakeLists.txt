# libs/lib_simd/CMakeLists.txt
#
# Portable SIMD abstraction. Leaf library; depends on nothing from
# the rest of the project.
#
# PER-FILE ARCH FLAGS
# -------------------
# On MSVC, _xgetbv and similar intrinsics are only declared when the
# compiler is targeting an architecture that supports the underlying
# instruction. simd_init.c calls _xgetbv, so it must be compiled with
# /arch:AVX. Any CPU with AVX2 has AVX, so this is safe: the file
# cannot run on a CPU that lacks AVX anyway (the OS won't load it).
#
# simd_avx2.c gets /arch:AVX2 so the AVX2 intrinsics are declared.
# simd_sse42.c and the other files stay at the project baseline.

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(SIMD_ARCH_X86_64 ON)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(SIMD_ARCH_ARM64 ON)
endif()

set(SIMD_SOURCES
    src/simd_init.c
    src/simd_api.c
    src/simd_scalar.c
)

if(SIMD_ARCH_X86_64)
    list(APPEND SIMD_SOURCES
        src/simd_sse42.c
        src/simd_avx2.c
    )
elseif(SIMD_ARCH_ARM64)
    list(APPEND SIMD_SOURCES
        src/simd_neon.c
    )
endif()

add_library(lib_simd STATIC ${SIMD_SOURCES})

target_include_directories(lib_simd PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(SIMD_ARCH_X86_64)
    if(MSVC)
        # simd_init.c calls _xgetbv; needs /arch:AVX for the declaration.
        set_source_files_properties(src/simd_init.c PROPERTIES
            COMPILE_OPTIONS "/arch:AVX")
        # The AVX2 backend needs /arch:AVX2 for its intrinsics.
        set_source_files_properties(src/simd_avx2.c PROPERTIES
            COMPILE_OPTIONS "/arch:AVX2")
        # SSE4.2 is the MSVC default on x64; no flag needed.
    else()
        set_source_files_properties(src/simd_init.c PROPERTIES
            COMPILE_OPTIONS "-mavx")
        set_source_files_properties(src/simd_sse42.c PROPERTIES
            COMPILE_OPTIONS "-msse4.2")
        set_source_files_properties(src/simd_avx2.c PROPERTIES
            COMPILE_OPTIONS "-mavx2;-mfma")
    endif()
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(lib_simd PUBLIC Threads::Threads)
endif()

if(MSVC)
    target_compile_options(lib_simd PRIVATE /W4 /permissive-)
else()
    target_compile_options(lib_simd PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wconversion -Wsign-conversion)
endif()