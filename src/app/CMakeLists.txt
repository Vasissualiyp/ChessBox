# SPDX-License-Identifier: GPL-3.0-or-later
#
# Interaction logic with no front end in it: input is actions, output is a snapshot. The
# SDL window and the tests drive exactly the same object.
cb_layer(chessbox_app LEVEL 95
  SOURCES session.cpp
  DEPS chessbox_io chessbox_game chessbox_view)

# The playable window, built only when both Vulkan and SDL3 are available. The engine,
# the CLI and the whole test suite do not depend on it.
if(TARGET chessbox_render AND CB_HAVE_SDL)
  add_executable(chessbox_gui gui_main.cpp)
  target_link_libraries(chessbox_gui PRIVATE chessbox_app chessbox_render cb_flags)
  install(TARGETS chessbox_gui RUNTIME DESTINATION bin)
endif()
