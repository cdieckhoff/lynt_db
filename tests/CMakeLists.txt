# tests/CMakeLists.txt

# Helper: define a test executable and register it with CTest
function(lynt_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE lib_lynt)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

lynt_add_test(test_m    test_m.c)
lynt_add_test(test_lynt test_lynt.c)
lynt_add_test(test_db   test_db.c)