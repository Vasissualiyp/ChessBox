# SPDX-License-Identifier: GPL-3.0-or-later
cb_layer(chessbox_cli LEVEL 100 DEPS chessbox_io)

add_executable(chessbox main.cpp)
target_link_libraries(chessbox PRIVATE chessbox_io cb_flags)
