# SPDX-License-Identifier: GPL-3.0-or-later
cb_layer(chessbox_cli LEVEL 110 DEPS chessbox_io)

add_executable(chessbox main.cpp)
target_link_libraries(chessbox PRIVATE chessbox_io chessbox_view cb_flags)

install(TARGETS chessbox RUNTIME DESTINATION bin)
