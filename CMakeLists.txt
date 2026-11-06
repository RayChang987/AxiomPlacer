cmake_minimum_required(VERSION 3.14)
project(AxiomPlacer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AXIOM_BUILD_TESTS "Build unit tests" ON)

add_library(axiom
  src/problem.cpp
  src/placement.cpp
  src/skyline.cpp
  src/bstar_tree.cpp
  src/floorplan.cpp
  src/annealer.cpp
  src/report.cpp
)
target_include_directories(axiom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(axiom PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(axiom-placer apps/axiom_placer.cpp)
target_link_libraries(axiom-placer PRIVATE axiom)

if(AXIOM_BUILD_TESTS)
  enable_testing()
  add_executable(axiom-tests tests/unit_tests.cpp)
  target_link_libraries(axiom-tests PRIVATE axiom)
  add_test(NAME unit COMMAND axiom-tests)

  find_package(Python3 COMPONENTS Interpreter)
  file(GLOB AXIOM_BENCHMARKS ${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/*.txt)
  foreach(bench ${AXIOM_BENCHMARKS})
    get_filename_component(name ${bench} NAME_WE)
    set(out ${CMAKE_CURRENT_BINARY_DIR}/test-${name}.out)
    add_test(NAME place-${name}
             COMMAND axiom-placer ${bench} ${out} --iters 20000 --seed 1 --quiet)
    if(Python3_FOUND)
      add_test(NAME verify-${name}
               COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify.py ${bench} ${out})
      set_tests_properties(verify-${name} PROPERTIES DEPENDS place-${name})
    endif()
  endforeach()
endif()
