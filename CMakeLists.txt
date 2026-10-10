cmake_minimum_required(VERSION 3.10)
project(cpp-stl CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 严格警告：零告警是硬性要求
add_compile_options(-Wall -Wextra -Werror)

# ---- 库：string / memory_pool（vector、智能指针为 header-only）----
add_library(mystl STATIC
    src/memory_pool.cpp
    src/string.cpp
)
target_include_directories(mystl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# 库以 -O2 编译：基准测试才有意义（测试断言逻辑本身不依赖优化）
target_compile_options(mystl PRIVATE -O2)

# ---- 单元测试：独立可执行文件，断言全开（不注入 -DNDEBUG）----
enable_testing()

set(MYSTL_TESTS
    test_vector
    test_string
    test_memory_pool
    test_smart_ptr
)
foreach(test_name IN LISTS MYSTL_TESTS)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE mystl)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# ---- 基准测试：-O2 单独开 ----
set(MYSTL_BENCHS
    bench_vector
    bench_pool
)
foreach(bench_name IN LISTS MYSTL_BENCHS)
    add_executable(${bench_name} benchs/${bench_name}.cpp)
    target_link_libraries(${bench_name} PRIVATE mystl)
    target_compile_options(${bench_name} PRIVATE -O2)
endforeach()
