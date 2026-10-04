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
  foreach(screen board welcome menu newgame quit pause settings editor info pieces)
    add_test(NAME gui-${screen}
      COMMAND chessbox_gui standard
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-${screen}.ppm
              --screen ${screen} --script "click e2"
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-${screen} PROPERTIES LABELS "render;gui" TIMEOUT 120
      ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
  # The play board as its own shape (M17): a glued variant drawn on its surface, still
  # validation-clean and still a picture. `standard --geometry` is a no-op, checked in the
  # scene tests rather than here because it is about geometry, not about the window.
  add_test(NAME gui-geometry
    COMMAND chessbox_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-geometry.ppm
            --geometry --script "click h1"
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-geometry PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # The eversion flag at its halfway point: with the revised invert this is the rest
  # shape (the swap happens past 0.5), so it is a plain validation capture of the pose.
  add_test(NAME gui-evert
    COMMAND chessbox_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-evert.ppm
            --geometry --evert 0.5
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-evert PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # And slid round itself: half a cell along each axis, which is where no square sits
  # where the unposed board put it and every one of them is still on the surface.
  add_test(NAME gui-slide
    COMMAND chessbox_gui klein
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-slide.ppm
            --geometry --slide 2.5 --slide-v 1.5
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-slide PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # Fully inside out: what was the hole is now the outside, and every piece stands on the
  # outside with it (M17.7). Validation-clean and a picture, not a blank or a tangle.
  add_test(NAME gui-invert
    COMMAND chessbox_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-invert.ppm
            --geometry --evert 1
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-invert PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # A ghosted board: the far side of the ring shows through the near side (M17.10).
  add_test(NAME gui-ghost
    COMMAND chessbox_gui torus
            --shot ${CMAKE_BINARY_DIR}/render-captures/gui-ghost.ppm
            --geometry --ghost 0.4
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(gui-ghost PROPERTIES LABELS "render;gui" TIMEOUT 120
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  # The shapes above two dimensions (M17.12): `torus3d`'s nested shells and `hyper4`'s
  # tesseract, validation-clean and a picture.
  foreach(shape torus3d hyper4)
    add_test(NAME gui-geometry-${shape}
      COMMAND chessbox_gui ${shape}
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-geometry-${shape}.ppm
              --geometry
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-geometry-${shape} PROPERTIES LABELS "render;gui"
      TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
  # M17.22: slide, invert and ghost now reach the D >= 3 stacked shapes too. Each is a
  # validation-clean frame of the documented behaviour stated through the ordinary flags.
  foreach(case torus3d-slide torus3d-invert torus3d-ghost hyper4-invert hyper4-ghost)
    string(REPLACE "-" ";" parts "${case}")
    list(GET parts 0 shape)
    list(GET parts 1 pose)
    if(pose STREQUAL "slide")
      set(poseFlags --slide 1 --slide-v 2)
    elseif(pose STREQUAL "invert")
      set(poseFlags --evert 1)
    else()  # ghost
      set(poseFlags --ghost 0.4)
    endif()
    add_test(NAME gui-geometry-${case}
      COMMAND chessbox_gui ${shape}
              --shot ${CMAKE_BINARY_DIR}/render-captures/gui-geometry-${case}.ppm
              --geometry ${poseFlags}
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(gui-geometry-${case} PROPERTIES LABELS "render;gui"
      TIMEOUT 120 ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
  endforeach()
endif()
