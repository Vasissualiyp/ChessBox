# SPDX-License-Identifier: GPL-3.0-or-later
find_package(tomlplusplus REQUIRED)

cb_layer(chessbox_io LEVEL 90
  SOURCES notation.cpp fen.cpp variant_toml.cpp variant_doc.cpp ascii_board.cpp rule_parse.cpp
  DEPS chessbox_movegen chessbox_view chessbox_game chessbox_rules
  SYSTEM_DEPS tomlplusplus::tomlplusplus)
