cmake_minimum_required(VERSION 3.22)

project(solution)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Executable
add_executable(answer main.cpp)
target_link_libraries(answer PRIVATE dbcppp)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DDEBUG)
endif()


# -------------------- Catch2 + test target --------------------
option(ENABLE_TESTS "Build unit tests" ON)

if(ENABLE_TESTS)
  include(FetchContent)

  # Fetch Catch2 v3 (no git submodule needed)
  FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
  )
  FetchContent_MakeAvailable(catch2)

  enable_testing()

  # Your first test target (explicit file for now)
  add_executable(answer_tests
    tests/test_sample.cpp
  )

  # If your tests need to include headers from solution/
  target_include_directories(answer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

  # Link Catch2's main (provides test runner)
  target_link_libraries(answer_tests
    PRIVATE
      Catch2::Catch2WithMain
  )

  include(CTest)
  include(Catch)
  catch_discover_tests(answer_tests)
endif()
