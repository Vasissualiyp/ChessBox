# SPDX-License-Identifier: GPL-3.0-or-later
# Layers are added bottom-up; cb_layer() rejects an upward dependency, so the
# layer map in docs/ARCHITECTURE.md is enforced at configure time.
add_subdirectory(base)
add_subdirectory(diag)
add_subdirectory(space)
add_subdirectory(geometry)
add_subdirectory(pieces)
add_subdirectory(variant)
add_subdirectory(position)
add_subdirectory(movegen)
add_subdirectory(io)
add_subdirectory(cli)
