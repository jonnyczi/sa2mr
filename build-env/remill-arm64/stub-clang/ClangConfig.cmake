# Stand-in for clang's CMake package, for build.sh. remill's cmake/BCCompiler.cmake requires
# find_package(Clang) to compile its semantics bitcode, but build.sh never builds that bitcode
# (it is taken from upstream's x86_64 release; see sources.lock), and only builds the
# remill-lift-14 target. The configure-time "can it emit bitcode" probe runs fake-clang.
set(_stub "${CMAKE_CURRENT_LIST_DIR}/fake-clang")
if(NOT TARGET clang)
  add_executable(clang IMPORTED)
  set_target_properties(clang PROPERTIES IMPORTED_LOCATION "${_stub}" LOCATION "${_stub}")
endif()
if(NOT TARGET llvm-link)
  add_executable(llvm-link IMPORTED)
  set_target_properties(llvm-link PROPERTIES IMPORTED_LOCATION "${_stub}" LOCATION "${_stub}")
endif()
set(Clang_FOUND TRUE)
