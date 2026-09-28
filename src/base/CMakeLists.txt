# SPDX-License-Identifier: GPL-3.0-or-later
cb_layer(chessbox_base LEVEL 0
  SOURCES result.cpp zobrist.cpp
  DEPS)
target_link_libraries(chessbox_base PUBLIC cb_flags)
