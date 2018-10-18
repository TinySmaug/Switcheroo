set(LLVM_LINK_COMPONENTS support)

add_clang_executable(switcheroo
  Switcheroo.cpp
  )
target_link_libraries(switcheroo
  clangTooling
  clangBasic
  clangASTMatchers
  )
