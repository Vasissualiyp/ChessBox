# SPDX-License-Identifier: GPL-3.0-or-later
#
# The playable window. Added last because it needs both the interaction layer and the
# renderer to exist, and it is skipped entirely when there is no graphics stack.
if(TARGET chessbox_render AND CB_HAVE_GUI)
  add_executable(chessbox_gui main.cpp)
  target_link_libraries(chessbox_gui PRIVATE chessbox_render chessbox_app cb_flags)
  install(TARGETS chessbox_gui RUNTIME DESTINATION bin)
endif()
