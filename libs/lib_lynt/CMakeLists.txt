add_library(lib_lynt STATIC
    src/lynt_api.c
)

target_include_directories(lib_lynt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(lib_lynt
    PUBLIC  lib_m
    PRIVATE lib_simd
)