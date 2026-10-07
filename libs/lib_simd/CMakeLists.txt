add_library(lib_simd STATIC
    src/simd_dispatch.c
)

target_include_directories(lib_simd PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)