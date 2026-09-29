# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Catch2 3 REQUIRED)

file(GLOB_RECURSE CB_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/render/*.cpp
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

# The renderer is optional, so its tests compile to nothing when Vulkan is absent - the
# rest of the suite still builds and passes on a machine with no graphics stack.
if(TARGET chessbox_render)
  target_compile_definitions(chessbox_tests PRIVATE CB_HAVE_VULKAN=1)
endif()

# Aggregate, label-addressable suites: `ctest -L unit` is the fast loop.
# Labels are added as the first test of that kind lands, so an empty label can
# never silently "pass". golden/perft joined in M1.
foreach(label unit property golden perft arch render)
  add_test(NAME ${label} COMMAND chessbox_tests "[${label}]~[slow]")
  set_tests_properties(${label} PROPERTIES LABELS ${label})
endforeach()

# The deep node counts are minutes of work in Release and over an hour in Debug, so
# they are their own `slow` label rather than a tax on every `ctest --preset dev`. The
# test presets exclude that label; ask for it with `ctest --preset release-slow`.
add_test(NAME perft-slow COMMAND chessbox_tests "[perft][slow]")
set_tests_properties(perft-slow PROPERTIES LABELS "slow;perft" TIMEOUT 3600)
add_test(NAME property-slow COMMAND chessbox_tests "[property][slow]")
set_tests_properties(property-slow PROPERTIES LABELS "slow;property" TIMEOUT 3600)
set_tests_properties(property PROPERTIES TIMEOUT 900)

# The interface, end to end: the real binary renders one frame - board, rails, ledger,
# promotion state - into an image with no display present, and fails if the Vulkan
# validation layers say anything. It is the only automated check that the panels and the
# board agree about where the board is, and it needs no compositor to run.
if(TARGET chessbox_gui)
  # Every screen, not just the board: a menu that fails to lay out, or a panel that makes
  # the validation layers complain, is caught here rather than by someone opening it.
  foreach(screen board menu newgame pause settings creator info pieces)
    add_test(NAME gui-${screen}
      COMMAND chessbox_gui standard
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-${screen}.ppm
              --screen ${screen} --script "click e2"
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-${screen} PROPERTIES LABELS "render;gui" TIMEOUT 120
      ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
endif()
