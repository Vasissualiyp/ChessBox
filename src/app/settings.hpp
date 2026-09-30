// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string>

#include "base/result.hpp"

namespace cb::app {

/// Everything the player can change, and where it is kept between sessions.
///
/// Deliberately a plain struct of values with sane defaults: a settings file that fails
/// to parse, or comes from a newer build, must never stop the game starting. Unknown
/// keys are ignored and missing ones keep their default.
struct Settings {
  // ---- display ------------------------------------------------------------
  /// Interface scale. Fonts are rebuilt at the new size rather than stretched, so text
  /// stays crisp - which is the whole point of the option on a high-density display.
  float guiScale{1.0f};
  bool fullscreen{false};
  /// Frame pacing. The game redraws on input, so this only bounds the idle loop.
  bool vsync{true};

  // ---- board --------------------------------------------------------------
  bool showLegalMoves{true};
  bool showLastMove{true};
  bool showCheck{true};
  /// Draw cyan edges where the board is glued to itself. Off only for a player who
  /// already knows the geometry by heart.
  bool showSeams{true};
  bool showCoordinates{true};
  /// How much taller a valuable piece stands. 0 makes every piece the same height.
  float pieceHeightScale{1.0f};
  /// Draw the board flat and straight down, with pieces as lettered tokens. Cheaper to
  /// draw than the models, and on a slow machine that is the difference between a game
  /// and a slideshow.
  bool flatView{false};
  /// Which flat-piece icon set to draw: "faceted" or "primitive". A name rather than an
  /// enum because this layer is below the renderer, where the sets are defined, and a
  /// settings file should stay readable anyway.
  std::string pieceIcons{"faceted"};
  /// Which palette to draw with: "manifold" (the shipped look) or "console". A name for
  /// the same reason as `pieceIcons` - this layer is below the renderer, and a settings
  /// file should stay readable.
  std::string theme{"manifold"};
  /// Slide pieces along the route they actually took, through portals and off walls.
  bool animateMoves{true};
  /// Multiplier on how fast that happens. Higher is faster.
  float animationSpeed{1.0f};

  // ---- camera -------------------------------------------------------------
  float orbitSensitivity{1.0f};
  float zoomSensitivity{1.0f};
  bool invertOrbitY{false};

  // ---- gameplay -----------------------------------------------------------
  /// Ask before playing a move rather than playing it on the second click.
  bool confirmMoves{false};
  /// Two players share one keyboard, one half each: player one types squares with
  /// qwertasdfgzxcvb, player two with the other half.
  bool hotSeat{false};
  /// Piece name to promote to without asking; empty means always ask.
  std::string autoPromoteTo;
  std::string lastVariant{"standard"};

  // ---- audio --------------------------------------------------------------
  // The game has no sound yet. These are stored and shown disabled rather than hidden,
  // so the settings screen does not silently change shape when sound arrives.
  float volumeMaster{0.8f};
  float volumeMusic{0.6f};
  float volumeEffects{0.9f};

  /// Where settings live: /chessbox/settings.conf, or the platform's
  /// equivalent.
  static std::filesystem::path defaultPath();

  /// Read a settings file. A missing file is not an error - it means "defaults".
  static Settings load(const std::filesystem::path& path);
  Result<void> save(const std::filesystem::path& path) const;

  /// Clamp anything out of range. Called after loading, so a hand-edited file cannot
  /// produce an unusable interface.
  void sanitize();
};

}  // namespace cb::app
