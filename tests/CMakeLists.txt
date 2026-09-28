# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Catch2 3 REQUIRED)

file(GLOB_RECURSE CB_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/property/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/golden/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/perft/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/arch/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/oracle/*.cpp)

add_executable(chessbox_tests ${CB_TEST_SOURCES})
target_link_libraries(chessbox_tests PRIVATE
  Catch2::Catch2WithMain cb_flags ${CB_ALL_LAYER_TARGETS})
target_include_directories(chessbox_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(chessbox_tests PRIVATE
  CB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
  CB_BINARY_DIR="${CMAKE_BINARY_DIR}")

# Aggregate, label-addressable suites: `ctest -L unit` is the fast loop.
# Labels are added as the first test of that kind lands, so an empty label can
# never silently "pass". golden/perft join in M1.
foreach(label unit property arch)
  add_test(NAME ${label} COMMAND chessbox_tests "[${label}]")
  set_tests_properties(${label} PROPERTIES LABELS ${label})
endforeach()
set_tests_properties(property PROPERTIES TIMEOUT 900)
