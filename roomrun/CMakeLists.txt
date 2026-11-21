cmake_minimum_required(VERSION 3.16)
project(roomrun CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Course bytes must be reproducible across compilers/CPUs: no -ffast-math, and no FMA
# contraction (GCC fuses a*b+c by default on aarch64, which would change results there).
add_compile_options(-Wall -Wextra -Wno-unused-parameter -ffp-contract=off)

# Embed the default module library so the library works without data files.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/data/modules.json)
file(READ ${CMAKE_SOURCE_DIR}/data/modules.json MODULES_JSON)
configure_file(src/builtin_modules.cpp.in ${CMAKE_BINARY_DIR}/builtin_modules.cpp @ONLY)

add_library(roomrun STATIC
  src/json.cpp src/geom.cpp src/room.cpp src/field.cpp src/skeleton.cpp src/profile.cpp
  src/modules.cpp src/course.cpp src/assembler.cpp src/sim.cpp src/validate.cpp
  src/generator.cpp src/export.cpp src/playmesh.cpp ${CMAKE_BINARY_DIR}/builtin_modules.cpp)
target_include_directories(roomrun PUBLIC include)

find_package(Threads REQUIRED)

add_executable(roomrun-gen tools/roomrun_gen.cpp)
target_link_libraries(roomrun-gen roomrun)

add_executable(roomrun-batch tools/roomrun_batch.cpp)
target_link_libraries(roomrun-batch roomrun Threads::Threads)

enable_testing()
add_executable(roomrun-tests tests/test_main.cpp)
target_link_libraries(roomrun-tests roomrun)
add_test(NAME roomrun-tests COMMAND roomrun-tests)
