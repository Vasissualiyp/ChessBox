// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app/hotseat.hpp"
#include "game/game.hpp"
#include "view/camera.hpp"
#include "view/layout.hpp"
#include "view/move_anim.hpp"
#include "view/move_camera.hpp"
#include "view/seams.hpp"
#include "view/snapshot.hpp"

namespace cb {
struct GameFile;  // io/game_file.hpp; only a reference is needed here
}

namespace cb::app {

enum class ActionKind : std::uint8_t {
  None,
  ClickCell,  ///< the user clicked a cell (already resolved from a pixel)
  Undo,
  Reset,
  Orbit,          ///< dx, dy in radians
  Pan,            ///< dx, dy: slide the look-at point in the view plane
  Zoom,           ///< dx as a multiplier
  SetScreenAxes,  ///< text is a comma-separated axis name list
  SetPromotion,   ///< text is a piece name
  Confirm,        ///< play the move that is waiting for confirmation
  Cancel,         ///< discard it
  Resign,         ///< the side to move concedes
  AgreeDraw,      ///< the players agree a draw
};

struct Action {
  ActionKind kind{ActionKind::None};
  CellId cell{kInvalidCell};
  float dx{0};
  float dy{0};
  std::string text;
  /// For SetScreenAxes: lay the sub-board grid down the screen instead of across it.
  bool gridVertical{false};
};

/// Everything a front end needs, with no front end in it.
///
/// Input arrives as actions and the result is a snapshot; nothing here knows about SDL,
/// Vulkan, or a window. That is what lets a scripted action log drive the whole
/// interaction path in a test - selection, illegal clicks, promotion, undo - with no
/// display and no screenshots (M4.6).
class Session {
 public:
  /// Takes the variant by value and keeps it at a stable address: Game, Position and
  /// MoveGen all hold pointers into it.
  static Result<std::unique_ptr<Session>> create(VariantSpec variant);

  [[nodiscard]] const VariantSpec& variant() const noexcept { return *variant_; }
  [[nodiscard]] const Game& game() const noexcept { return *game_; }
  [[nodiscard]] const view::PositionView& snapshot() const noexcept { return snapshot_; }
  [[nodiscard]] const view::ViewConfig& viewConfig() const noexcept { return viewCfg_; }
  /// The camera to draw and to pick with.
  ///
  /// Returned by value because it is not simply the stored one: pausing steps the view
  /// back off the board, and *everything* that places something on screen has to agree
  /// about that. Handing out the stored camera is how the board once pulled away while
  /// the pieces drawn on top of it stayed put.
  [[nodiscard]] view::OrbitCamera camera() const noexcept;

  /// `camera()`'s own blend, against an arbitrary placement set and its bounds instead of
  /// the session's flat layout - what the geometry view passes, built from
  /// `render::PlaySurface::seats()` (M17.16). One definition either way, so the move
  /// camera cannot say something different from what `camera()` already promises.
  [[nodiscard]] view::OrbitCamera cameraOver(
      const std::vector<view::Placement>& placements, const view::Bounds& scene,
      const view::ViewConfig& cfg) const noexcept;

  /// The player's own camera with no move-shot contribution, adjusted for pull-back.
  /// The geometry view follows on top of this so it can ease in and out at the ends of a
  /// move; `cameraOver` folds the shot in, so it cannot be used as the base for a *new*
  /// shot (M17.16).
  [[nodiscard]] view::OrbitCamera playerCamera() const noexcept;

  /// How far the view has stepped back off the board, 0 to 1. Pause is the player
  /// looking up from the position, not a panel landing on top of it.
  /// Camera pull-back, 0 at the board and growing as the shell steps away from it: 1 at
  /// pause, 2 one step further into a pause panel.
  void setPullBack(float t) noexcept { pullBack_ = std::clamp(t, 0.0f, 2.0f); }
  [[nodiscard]] float pullBack() const noexcept { return pullBack_; }
  /// Where the board is glued to itself, and in what colour. Built once per variant.
  [[nodiscard]] const view::SeamMap& seams() const noexcept { return seams_; }
  /// The multiverse's branches, in lattice terms, for the view to draw connectors. Empty
  /// for a variant with no timelines.
  [[nodiscard]] std::vector<view::TimelineLink> timelineLinks() const;
  /// The palette the seams and the move animation are coloured from. Held here rather
  /// than read from the renderer so a session can be driven, and tested, with no
  /// renderer at all.
  [[nodiscard]] const view::Theme& theme() const noexcept { return theme_; }
  void setTheme(const view::Theme& t);
  /// The move currently being drawn, if any.
  [[nodiscard]] const view::MoveAnimation& animation() const noexcept { return anim_; }

  /// How the camera follows a move: "off" (the player's own orbit), "piece" or "route".
  /// Set from `Settings::cameraMode`; anything unrecognised means "off", which is the
  /// shipped behaviour and keeps every pre-M11 pose.
  void setCameraMode(std::string_view mode) noexcept;
  /// 0..1 how strongly the move camera leads the player's orbit. Zero is a no-op.
  void setFollowStrength(float strength) noexcept {
    followStrength_ = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
  }
  [[nodiscard]] float followStrength() const noexcept { return followStrength_; }
  /// Whether the move camera follows at all: a follow mode is set and the strength is not
  /// zero. Independent of whether a move is in flight, which is what the shape-follow
  /// scheduler needs to budget a move's choreography (M12.6).
  [[nodiscard]] bool followsMove() const noexcept {
    return cameraPolicy_.follow != view::FollowMode::Off && followStrength_ > 0.0f;
  }
  /// True while a move's camera shot owns the view, which is exactly when the board is
  /// interaction-locked.
  [[nodiscard]] bool shotInFlight() const noexcept;
  /// Move the animation on. Returns true while something is still moving, which is how
  /// the front end knows to keep drawing frames.
  bool advanceAnimation(float dt);
  /// Pin the running move at progress `t` in [0,1], for a capture that states its own
  /// time. Does nothing when no move is animating.
  void setMoveProgress(float t) noexcept { anim_.setProgress(t); }
  [[nodiscard]] const std::vector<view::Placement>& placements() const noexcept {
    return placements_;
  }
  [[nodiscard]] CellId selected() const noexcept { return selected_; }
  /// The last thing that happened, in words, for a status bar.
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] std::string statusLine() const;

  Result<void> apply(const Action& a);

  /// Tell the session how wide the board's drawing area is relative to its height, so
  /// the default camera frames the board in the space the interface leaves it.
  void setBoardAspect(float aspect);

  /// Frame the camera on a box of world space, keeping the angle the player is looking
  /// from. The geometry view needs this: the shape is a different size from the flat
  /// board and sits somewhere else entirely, so framing on the layout's own bounds
  /// leaves the board off in a corner of the window.
  ///
  /// `headroom` is how far above the box the camera should look - a piece's crown must
  /// not be clipped. A caller whose bounds already include room for what stands on them
  /// (the play surface pads for its pieces) passes 0, so the look-at stays on the box's
  /// centre instead of being lifted off it (M17.11).
  void frameOn(const view::Bounds& b, float headroom = 1.4f);

  /// Look straight down with no perspective, for a machine that would rather not draw
  /// the scene in three dimensions - and for a player who simply prefers a diagram.
  void setFlatView(bool flat);
  [[nodiscard]] bool flatView() const noexcept { return flat_; }

  /// Two players, one keyboard. When on, the front end routes key presses through
  /// feedHotSeat so each half enters its own moves.
  void setHotSeat(bool on) noexcept { hotSeatEnabled_ = on; }
  [[nodiscard]] bool hotSeat() const noexcept { return hotSeatEnabled_; }
  /// Feed one key, as the unshifted character it produces. True when it belonged to a
  /// player and was consumed - so the front end can skip its own shortcuts. A completed
  /// square is applied as a click.
  bool feedHotSeat(char key);
  [[nodiscard]] const HotSeat& hotSeatKeys() const noexcept { return hotSeatKeys_; }

  /// Whether this cell's board exists yet (always true for a non-temporal variant). The
  /// temporal lattice is mostly unfilled space, so the view draws only live boards.
  [[nodiscard]] bool boardVisible(CellId c) const;

  /// How long a move takes to draw, per cell travelled. Zero turns animation off, and
  /// a move then simply appears where it landed.
  void setAnimationSeconds(float secondsPerCell) { animSeconds_ = secondsPerCell; }

  /// A move the player has committed to except for what the pawn becomes.
  ///
  /// The session stops and asks rather than choosing silently: a variant may promote to
  /// pieces nobody expects, so "queen by default" is a guess, not a rule. While a
  /// promotion is pending the board is frozen - clicks do nothing until it is resolved.
  struct PendingPromotion {
    bool active{false};
    CellId from{kInvalidCell};
    CellId to{kInvalidCell};
    std::vector<PieceTypeId> choices;
  };
  [[nodiscard]] const PendingPromotion& pendingPromotion() const noexcept {
    return pending_;
  }
  Result<void> choosePromotion(PieceTypeId piece);
  void cancelPromotion();

  /// A move the player has chosen but not yet played, when "confirm before moving" is on.
  /// The board is frozen while it waits, exactly as it is for a promotion: the player has
  /// committed to the move and owes only a yes or a no.
  struct PendingMove {
    bool active{false};
    CellId from{kInvalidCell};
    CellId to{kInvalidCell};
  };
  [[nodiscard]] const PendingMove& pendingMove() const noexcept { return pendingMove_; }
  Result<void> confirmMove();
  void cancelMove();
  void setConfirmMoves(bool on) noexcept { confirmMoves_ = on; }
  [[nodiscard]] bool confirmMoves() const noexcept { return confirmMoves_; }

  /// Replace the current position from FEN-N, discarding history. Used for setting up a
  /// study or a test position, and by the front end's position entry.
  Result<void> loadFen(std::string_view fen);

  /// Write this game (variant, start position, move history) to a game file (M12.7).
  Result<void> saveGame(const std::filesystem::path& path) const;
  /// Replay a parsed game file into this session. Fails if the file names a different
  /// variant; a front end starts that variant first.
  Result<void> loadGame(const GameFile& file);

  /// Play the legal move named in the engine's notation, starting its animation. The
  /// M12.6 runner steps a saved game this way, one move at a time, so each can be
  /// animated - unlike `loadGame`, which replays them all at once. Fails, naming the
  /// move, when it is not legal in the current position.
  Result<void> playMoveText(std::string_view text);

  /// Resolve a pixel to a cell and act on it. Returns kInvalidCell if the click missed
  /// the board, which is not an error - it deselects, like clicking off a board does.
  CellId clickPixel(float px, float py, float width, float height);

  /// Run a newline-separated action script. Written for tests, and equally the basis of
  /// a replay format.
  Result<void> applyScript(std::string_view script);

 private:
  Session() = default;
  void refreshSnapshot();
  void refreshView();
  /// Re-lay the temporal view after a move changed which boards exist, keeping the
  /// player's camera angle and zoom.
  void refreshTemporalView();
  void applyViewMode();
  /// The view actually laid out: the player's choice, adjusted for the flat mode, which
  /// cannot show a depth axis because a top-down camera would stack the boards unseen.
  [[nodiscard]] view::ViewConfig effectiveViewConfig() const;
  [[nodiscard]] const Move* findMove(CellId from, CellId to) const;
  [[nodiscard]] std::vector<PieceTypeId> promotionChoicesFor(CellId from,
                                                             CellId to) const;
  Result<void> playChecked(const Move& m);

  std::unique_ptr<VariantSpec> variant_;
  std::unique_ptr<Game> game_;
  view::PositionView snapshot_;
  /// What the player asked for; `viewCfg_` is this with the flat-mode adjustment applied.
  view::ViewConfig chosenCfg_;
  view::ViewConfig viewCfg_;
  view::OrbitCamera camera_;
  std::vector<view::Placement> placements_;
  view::SeamMap seams_;
  view::Theme theme_{view::Theme::manifold()};
  view::MoveAnimation anim_;
  /// The route the running animation is following, kept so `camera()` can plan a shot
  /// from the same trace the animation draws. Valid only while `anim_` is active.
  view::MovePath lastPath_;
  bool pathValid_{false};
  /// Off until a setting says otherwise: `CameraPolicy`'s own default follows the route,
  /// but the shipped look is the player's own camera, so the session starts there and
  /// `Shell::applySettings` moves it. Aggregate init keeps the rest of the record's
  /// defaults.
  view::CameraPolicy cameraPolicy_{view::FollowMode::Off};
  float followStrength_{0.6f};
  float animSeconds_{0.085f};
  bool flat_{false};
  bool hotSeatEnabled_{false};
  HotSeat hotSeatKeys_;
  CellId selected_{kInvalidCell};
  float boardAspect_{1.3f};
  float pullBack_{0.0f};
  bool framedOnce_{false};
  PendingPromotion pending_;
  PendingMove pendingMove_;
  bool confirmMoves_{false};
  std::string promotionPreference_;
  std::string message_;
};

/// Parse one line of an action script: "click e4", "undo", "reset", "orbit 0.1 0",
/// "zoom 1.2", "axes file,rank,level", "promote queen".
Result<Action> parseAction(const VariantSpec& v, std::string_view line);

}  // namespace cb::app
