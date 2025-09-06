cmake_minimum_required(VERSION 3.22)

project(solution)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------- Library with your utils ----------------
# can_utils.cpp implements the functions declared in include/can_utils.hpp
add_library(daq_can STATIC
    src/can_utils.cpp
)

# Public so consumers (answer, tests) get the headers automatically
target_include_directories(daq_can PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# dbcppp comes from the parent CMake via add_subdirectory(external/dbcppp)
# Link it PUBLIC so its include dirs propagate to anything using daq_can
target_link_libraries(daq_can PUBLIC dbcppp)


# Executable
add_executable(answer main.cpp)
target_link_libraries(answer PRIVATE daq_can)

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

  add_executable(answer_tests
    tests/test_sample.cpp
  )

  # Link Catch2's main (provides test runner)
  target_link_libraries(answer_tests
    PRIVATE
      daq_can
      Catch2::Catch2WithMain
  )

  # Define an absolute path to your dbc-files for tests
  target_compile_definitions(answer_tests
  PRIVATE
    DBC_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../dbc-files"
)


  include(CTest)
  include(Catch)
  catch_discover_tests(answer_tests)
endif()
