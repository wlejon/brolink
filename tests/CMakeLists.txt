enable_testing()

# Each test is a plain executable whose exit status is non-zero on any failed
# check (check.h); a watchdog in each turns a hang into a failure.
function(brolink_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE brolink)
    if(WIN32)
        target_compile_definitions(${name} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 300)
endfunction()

brolink_test(test_wire)
brolink_test(test_lanes)
brolink_test(test_link)
