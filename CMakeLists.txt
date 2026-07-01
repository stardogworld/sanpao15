cmake_minimum_required(VERSION 3.20)

project(sanpao15 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sanpao15_core
    core/src/bitboard.cpp
    core/src/move.cpp
    core/src/movegen.cpp
    core/src/notation.cpp
    core/src/position.cpp
    core/src/rules.cpp
)

target_include_directories(sanpao15_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/core/include
)

add_library(sanpao15_solver
    solver/src/combinadic.cpp
    solver/src/dense_index.cpp
    solver/src/dense_successor.cpp
    solver/src/dense_table.cpp
    solver/src/edge_probe.cpp
    solver/src/external_closure.cpp
    solver/src/external_keyset.cpp
    solver/src/layered.cpp
    solver/src/lowk_tablebase.cpp
    solver/src/partitioned_keyset.cpp
    solver/src/retrograde.cpp
    solver/src/solver.cpp
    solver/src/table.cpp
)

target_include_directories(sanpao15_solver
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/solver/include
)

target_link_libraries(sanpao15_solver PUBLIC sanpao15_core)

add_executable(sanpao15_cli cli/src/main.cpp)
target_link_libraries(sanpao15_cli PRIVATE sanpao15_core sanpao15_solver)

enable_testing()

add_executable(sanpao15_tests
    tests/test_bitboard.cpp
    tests/test_combinadic.cpp
    tests/test_csr.cpp
    tests/test_dense_index.cpp
    tests/test_dense_successor.cpp
    tests/test_dense_table.cpp
    tests/test_external_closure.cpp
    tests/test_external_keyset.cpp
    tests/test_edge_probe.cpp
    tests/test_layered.cpp
    tests/test_lowk_tablebase.cpp
    tests/test_main.cpp
    tests/test_movegen.cpp
    tests/test_notation.cpp
    tests/test_partitioned_keyset.cpp
    tests/test_solver_small.cpp
    tests/test_stats.cpp
    tests/test_table.cpp
    tests/test_terminal.cpp
)

target_link_libraries(sanpao15_tests PRIVATE sanpao15_core sanpao15_solver)

add_test(NAME sanpao15_tests COMMAND sanpao15_tests)
