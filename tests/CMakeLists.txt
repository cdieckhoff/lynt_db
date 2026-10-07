# tests/CMakeLists.txt
#
# One executable per library plus one for the whole DB. Each links the
# libraries it exercises and gets their PUBLIC include directories
# transitively. No test reaches into another library's src/ directory.

# Helper: define a test executable and register it with CTest.
function(lynt_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE ${LYNT_TEST_LIBS})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 30
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endfunction()

# test_m: only needs lib_m
set(LYNT_TEST_LIBS lib_m)
lynt_add_test(test_m test_m.c)

# test_simd: only needs lib_simd
set(LYNT_TEST_LIBS lib_simd)
lynt_add_test(test_simd test_simd.c)

# test_lynt: will need lib_lynt (and its transitive deps)
set(LYNT_TEST_LIBS lib_lynt)
lynt_add_test(test_lynt test_lynt.c)

# test_db: end-to-end
set(LYNT_TEST_LIBS lib_lynt)
lynt_add_test(test_db test_db.c)