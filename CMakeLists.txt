# Specify the minimum CMake version and the project name
cmake_minimum_required(VERSION 3.10)
project(CustomAllocDemo VERSION 1.0 LANGUAGES CXX)

# Specify the C++ standard (e.g., C++17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)


include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY "https://github.com/google/googletest.git"
    GIT_TAG "main"
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

set(SRCDIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(TESTDIR ${CMAKE_CURRENT_SOURCE_DIR}/test)
# List your source files
set(SOURCES
    ${SRCDIR}/lk_alloc.cpp
)

# google tests

set(TESTS
    lk_alloc_test
)

enable_testing()
foreach(test ${TESTS})
    add_executable(${test} ${TESTDIR}/${test}.cpp )
    target_link_libraries(${test} GTest::gtest_main GTest::gmock_main)
    target_include_directories(${test} PRIVATE ${CMAKE_SOURCE_DIR}/inc)
    gtest_discover_tests(${test})
endforeach(test ${TESTS})

# Add an executable target based on the source files
add_executable(${PROJECT_NAME} ${SOURCES})

# Optionally, add include directories if your project requires them
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/inc)

# Optionally, add compiler definitions or options
# target_compile_definitions(${PROJECT_NAME} PRIVATE MY_DEFINE=1)
