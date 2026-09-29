# SPDX-License-Identifier: GPL-3.0-or-later
# Layers are added bottom-up; cb_layer() rejects an upward dependency, so the
# layer map in docs/ARCHITECTURE.md is enforced at configure time.
#
# render comes before app even though it sits higher in the map: the windowed
# executable in app/ is only built when the render target exists, so render has to be
# declared first for `if(TARGET chessbox_render)` to see it.
add_subdirectory(base)
add_subdirectory(diag)
add_subdirectory(space)
add_subdirectory(geometry)
add_subdirectory(pieces)
add_subdirectory(variant)
add_subdirectory(position)
add_subdirectory(rules)
add_subdirectory(movegen)
add_subdirectory(view)
add_subdirectory(game)
add_subdirectory(io)
add_subdirectory(cli)
add_subdirectory(render)
add_subdirectory(app)
