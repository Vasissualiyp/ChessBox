# SPDX-License-Identifier: GPL-3.0-or-later
find_package(tomlplusplus REQUIRED)

cb_layer(chessbox_io LEVEL 90
  SOURCES notation.cpp fen.cpp variant_toml.cpp ascii_board.cpp
  DEPS chessbox_movegen
  SYSTEM_DEPS tomlplusplus::tomlplusplus)
