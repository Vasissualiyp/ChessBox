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
  /// Cap the redraw rate, in frames per second. 0 means "as fast as vsync allows". A cap
  /// matters when vsync is off and the screen is otherwise static: the field and the
  /// overtures animate forever, so without one the loop would spin a core.
  int frameCap{0};

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
  /// Draw the play board as the shape its geometry describes - a cylinder, a torus, a
  /// Moebius band - instead of a square diagram (M17). A presentation choice, so it never
  /// enters `VariantId`; offered only for a glued 2-D variant, which is the only geometry
  /// with a surface to become.
  bool geometryView{false};
  /// Where the board sits on its own surface, in cells. Sliding one along the files puts
  /// a1 where b1 was; keep going and the board comes all the way round, which on a
  /// Moebius band takes two laps and arrives mirrored. It is the one way to feel a gluing
  /// rather than be told about it.
  float geometrySlideU{0.0f};
  float geometrySlideV{0.0f};
  /// How far the geometry view has been turned through itself, 0 to 1: a torus pulled
  /// inside out through its own hole, a cylinder rolled back over itself like a sock.
  ///
  /// All three are a pose of the same board - the cells, the moves and the position are
  /// untouched - and each is a pure function of its number, so a capture reproduces.
  float geometryEvert{0.0f};
  /// The INVERT button's *target*: true asks the front end to ease `geometryEvert` to 1,
  /// false back to 0. The ramp lives in the front end, not in the pose, so a pose stays a
  /// pure function of its number and a still stays reproducible (M17.7).
  bool geometryInvert{false};
  /// How opaque the geometry view's board mesh is, 1 fully opaque down to `kGhostMin` as
  /// a ghost, so the far side of the shape and the pieces on it show through. The pieces
  /// stay opaque; the point is to see them (M17.10).
  float geometryGhost{1.0f};
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
  /// Multiplier on how fast the library screen's overtures play: their forward and
  /// reverse sweeps and their dwells. Higher is faster. Independent of `animationSpeed`,
  /// which is about moves during a game, not the New Game screen.
  float overtureSpeed{1.0f};

  // ---- camera -------------------------------------------------------------
  float orbitSensitivity{1.0f};
  float zoomSensitivity{1.0f};
  bool invertOrbitY{false};
  /// How the camera follows a move: "off", "piece" or "route". Off is the default and the
  /// pre-M11 behaviour. A name rather than an enum because this layer is below the view.
  std::string cameraMode{"off"};
  /// How strongly the move camera leads the player's own orbit, 0 to 1.
  float followStrength{0.6f};

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
