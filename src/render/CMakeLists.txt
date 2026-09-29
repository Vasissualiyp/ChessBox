# SPDX-License-Identifier: GPL-3.0-or-later
#
# The renderer is optional: the engine, the CLI and the whole test suite build and pass
# without Vulkan present. That is deliberate - a headless server or a CI runner with no
# graphics stack must still be able to build and verify the game.
find_package(Vulkan)
if(NOT Vulkan_FOUND)
  message(STATUS "Vulkan not found - skipping the renderer (engine and CLI are unaffected)")
  return()
endif()

find_program(CB_GLSLC glslc HINTS ${Vulkan_GLSLC_EXECUTABLE})
if(NOT CB_GLSLC)
  message(STATUS "glslc not found - skipping the renderer")
  return()
endif()

# Shaders are compiled to SPIR-V at build time and embedded as arrays: nothing is
# compiled at runtime and nothing has to be found on disk next to the binary.
set(CB_SPV_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders.hpp)
set(CB_SHADERS board.vert board.frag)
set(CB_SPV_FILES "")
foreach(shader IN LISTS CB_SHADERS)
  set(src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader})
  set(spv ${CMAKE_CURRENT_BINARY_DIR}/${shader}.spv)
  add_custom_command(OUTPUT ${spv}
    COMMAND ${CB_GLSLC} --target-env=vulkan1.3 -O -o ${spv} ${src}
    DEPENDS ${src}
    COMMENT "glslc ${shader}"
    VERBATIM)
  list(APPEND CB_SPV_FILES ${spv})
endforeach()

add_custom_command(OUTPUT ${CB_SPV_HEADER}
  COMMAND ${CMAKE_COMMAND}
          -DOUT=${CB_SPV_HEADER}
          "-DSPVS=${CB_SPV_FILES}"
          -P ${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake
  DEPENDS ${CB_SPV_FILES} ${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake
  COMMENT "embedding SPIR-V"
  VERBATIM)

set(CB_RENDER_SOURCES
  vulkan_context.cpp offscreen_target.cpp board_renderer.cpp image_io.cpp
  ${CB_SPV_HEADER})

# SDL3 supplies the window and the surface. Without it the renderer still builds and the
# headless tests still run - only the playable window is missing.
find_package(SDL3 CONFIG)
if(SDL3_FOUND)
  list(APPEND CB_RENDER_SOURCES window.cpp)
endif()

cb_layer(chessbox_render LEVEL 100
  SOURCES ${CB_RENDER_SOURCES}
  DEPS chessbox_view
  SYSTEM_DEPS Vulkan::Vulkan)
target_include_directories(chessbox_render PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(SDL3_FOUND)
  target_link_libraries(chessbox_render PUBLIC SDL3::SDL3)
  target_compile_definitions(chessbox_render PUBLIC CB_HAVE_SDL=1)
endif()
set(CB_HAVE_VULKAN ON CACHE INTERNAL "")
set(CB_HAVE_SDL ${SDL3_FOUND} CACHE INTERNAL "")
