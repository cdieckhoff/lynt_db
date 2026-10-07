# libs/lib_m/CMakeLists.txt
#
# The _m universal OS layer. Self-contained, no dependency on other lynt
# libraries. Could be lifted into any project as-is.

add_library(lib_m STATIC
    src/_m_error.c
    src/_m_open.c
    src/_m_close.c
    src/_m_truncate.c
    src/_m_size.c
    src/_m_sync.c    
    src/_m_read.c
    src/_m_write.c
    src/_m_pread.c
    src/_m_pwrite.c
    src/_m_seek.c
    src/_m_map.c            # contains _m_map, _m_unmap, _m_msync, _m_mapping_addr
    src/_m_punch_hole.c
    src/_m_is_hole.c
    src/_m_allocated_size.c
    src/_m_alloc.c
    src/_m_page.c
 src/_m_mark_sparse.c
)

target_include_directories(lib_m PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ---- Error message capture ----
# Default: ON in Debug, OFF in Release.
# Override with -DLYNT_ERROR_MESSAGES=OFF at configure time.
option(LYNT_ERROR_MESSAGES "Capture human-readable error messages" $<IF:$<CONFIG:Debug>,ON,OFF>)

# Default: OFF. Turn on for a diagnostic build that prints errors to stderr.
option(LYNT_ERROR_LOGGING "Print errors to stderr immediately" OFF)

if(LYNT_ERROR_MESSAGES)
    target_compile_definitions(lib_m PUBLIC _M_ENABLE_ERROR_MESSAGES=1)
endif()

if(LYNT_ERROR_LOGGING)
    target_compile_definitions(lib_m PUBLIC _M_ENABLE_ERROR_LOGGING=1)
endif()

# ---- Platform libraries ----
if(UNIX)
    # pthread is used implicitly by _Thread_local on some toolchains.
    # On Linux, -lpthread is required for the TLS implementation.
    # On macOS, pthread is part of libSystem and this is a no-op.
    find_package(Threads REQUIRED)
    target_link_libraries(lib_m PUBLIC Threads::Threads)
endif()

# ---- Compiler warnings ----
if(MSVC)
    target_compile_options(lib_m PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(lib_m PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
    )
endif()