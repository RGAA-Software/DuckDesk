cmake_minimum_required(VERSION 3.22)
find_package(GTest CONFIG REQUIRED)

function(add_ft_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE px_ft_engine GTest::gtest GTest::gtest_main)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_ft_test(test_ft_path_security test_ft_path_security.cpp)
add_ft_test(test_ft_compress test_ft_compress.cpp)
add_ft_test(test_ft_transfer_job test_ft_transfer_job.cpp)
add_ft_test(test_ft_engine test_ft_engine.cpp)
add_ft_test(test_ft_send_contract test_ft_send_contract.cpp)
add_ft_test(test_ft_two_phase_send test_ft_two_phase_send.cpp)
add_ft_test(test_ft_async_session test_ft_async_session.cpp)
add_ft_test(test_ft_integrity test_ft_integrity.cpp)

add_executable(test_ft_terminal test_ft_terminal.cpp)
target_link_libraries(test_ft_terminal PRIVATE
    px_ft_engine px_cp_message px_rp_message GTest::gtest GTest::gtest_main)
add_test(NAME test_ft_terminal COMMAND test_ft_terminal)
