# SPDX-License-Identifier: GPL-3.0-or-later
#
# Presentation, not engine: this is the first layer allowed to use floating point,
# because world-space positions are inherently continuous. The arch test's no-float
# rule covers the core layers only, and that boundary is the point.
cb_layer(chessbox_view LEVEL 60
  SOURCES layout.cpp snapshot.cpp camera.cpp
  DEPS chessbox_position)
