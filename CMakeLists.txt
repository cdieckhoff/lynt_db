# CMakeLists.txt — lynt_db project root
cmake_minimum_required(VERSION 3.20)

# Enable Hot Reload for MSVC if supported (kept from the VS template)
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project(lynt_db VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default build type when the IDE doesn't specify one
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ---- Options ----
option(LYNT_BUILD_TESTS "Build tests" ON)
option(LYNT_BUILD_TOOLS "Build CLI tools" OFF)
option(LYNT_WERROR      "Treat warnings as errors" OFF)

# ---- Platform definitions ----
if(WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
elseif(APPLE)
    add_compile_definitions(_DARWIN_C_SOURCE)
elseif(UNIX)
    add_compile_definitions(_GNU_SOURCE)
endif()

# ---- Libraries (leaves first) ----
add_subdirectory(libs/lib_m)
add_subdirectory(libs/lib_simd)
add_subdirectory(libs/lib_lynt)

# ---- Main executable ----
add_executable(lynt_db
    src/main.c
)
target_include_directories(lynt_db PRIVATE include)
target_link_libraries(lynt_db PRIVATE lib_lynt)

# ---- Tests ----
if(LYNT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()