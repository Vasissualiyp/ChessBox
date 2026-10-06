// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/shell.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include "app/embed.hpp"
#include "io/fen.hpp"
#include "io/game_file.hpp"
#include "io/variant_toml.hpp"
#include "view/morph.hpp"

namespace cb::app {

std::string_view screenName(Screen s) noexcept {
  switch (s) {
    case Screen::Welcome:
      return "welcome";
    case Screen::MainMenu:
      return "main menu";
    case Screen::NewGame:
      return "new game";
    case Screen::Watch:
      return "watch a game";
    case Screen::Game:
      return "game";
    case Screen::Paused:
      return "paused";
    case Screen::Settings:
      return "settings";
    case Screen::Editor:
      return "editor";
    case Screen::GameInfo:
      return "game mode";
    case Screen::PieceMoves:
      return "piece moves";
    case Screen::QuitConfirm:
    case Screen::PauseQuitConfirm:
      return "quit";
  }
  return "?";
}

int screenDepth(Screen s) noexcept {
  switch (s) {
    case Screen::Welcome:
    case Screen::MainMenu:
      return 0;
    case Screen::NewGame:
    case Screen::Watch:
    case Screen::Settings:
    case Screen::Editor:
    case Screen::QuitConfirm:
      return 1;
    case Screen::Game:
      return 2;
    // Pause and the reference screens are one rung off the game, still in its context
    // rather than out at the shell's own menus.
    case Screen::Paused:
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return 2;
  }
  return 0;
}

std::unique_ptr<Shell> Shell::create(std::vector<std::string> library,
                                     std::filesystem::path settingsPath) {
  auto shell = std::unique_ptr<Shell>(new Shell());
  shell->library_ = std::move(library);
  shell->settingsPath_ =
      settingsPath.empty() ? Settings::defaultPath() : std::move(settingsPath);
  shell->settings_ = Settings::load(shell->settingsPath_);
  // The shape morph's live value starts settled at its target (M18.6): a loaded
  // `geometry_view` means the fully-formed shape, its absence means the flat board. The
  // transient value is never written to disk, so it cannot come in mid-morph; deriving it
  // here is what stops a glued variant opening with a spurious morph on the first frame.
  shell->settings_.geometryFormed = shell->settings_.geometryView ? 1.0f : 0.0f;
  shell->currentVariant_ = shell->settings_.lastVariant;
  // The shell always opens on the main menu. The curated first-run sequence is now the
  // on-demand Tutorial row rather than something the game opens onto by itself.
  shell->screen_ = Screen::MainMenu;
  // A loaded setting should take effect immediately; the library overtures have their own
  // clock and are not waiting for `applySettings` (which a fresh shell has no reason to
  // call). `applySettings` keeps it in step once the player moves a control.
  shell->overtures_.setSpeed(shell->settings_.overtureSpeed);
  // The camera track (M13.9 Part E), if one has been recorded before. Its absence is the
  // ordinary case - every shell starts with no track - so a missing or unreadable file is
  // not an error, just an empty track.
  if (std::ifstream in{shell->cameraTrackPath()}; in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    if (auto track = parseCameraTrack(ss.str()); track.has_value()) {
      shell->cameraTrack_ = std::move(*track);
    }
  }
  return shell;
}

const std::vector<CuratedVariant>& curatedVariants() {
  // The first-run path: the board a newcomer knows, then one legible step at a time to
  // the board they cannot picture. A front door, not a gate - the full library stays
  // available.
  static const std::vector<CuratedVariant> kPath{
      {"standard", "the board you already know"},
      {"cylinder", "the a-file and the h-file become the same edge"},
      {"torus", "and then the rank edges close too: a board with no edges"},
      {"cube5", "the board grows a third axis - five boards stacked"},
      {"5d", "and then time: a move can land on a board from a past turn"},
  };
  return kPath;
}

const std::vector<GlossaryEntry>& glossary() {
  // Longest term first: a scan that replaces terms in a sentence has to match "Mobius
  // band" before it matches "band".
  static const std::vector<GlossaryEntry> kGlossary{
      {"time travel",
       "a variant where a piece may move onto a board from an earlier or later turn"},
      {"Klein bottle",
       "a board whose ends are joined with a twist that reverses one axis; it has only "
       "one "
       "face"},
      {"multiverse",
       "the axis along which the many boards of a time-travel game are laid out, one per "
       "branch of play"},
      {"Mobius band",
       "a board whose ends are joined with a half-twist, so riding it twice comes home "
       "mirrored"},
      {"dimension", "one axis of a board - files, ranks, and any axes above them"},
      {"cylinder",
       "a board whose left and right edges are joined, so the files form a loop"},
      {"timeline",
       "the axis along which the past and future turns of a time-travel game "
       "are laid out"},
      {"vector", "how far a piece moves along each axis, written as one number per axis"},
      {"pitch", "how many cells one unit of a piece's move covers along an axis"},
      {"glued",
       "two edges of a board are joined, so a piece crossing one arrives through the "
       "other"},
      {"slice", "one board taken out of an N-dimensional one and shown on its own"},
      {"seam", "a joined edge of a glued board; its colour says where crossing it lands"},
      {"torus",
       "a board whose edges are all joined into loops - it has no edge to fall off"},
  };
  return kGlossary;
}

std::string_view glossaryFor(std::string_view term) {
  for (const GlossaryEntry& e : glossary()) {
    if (e.term == term) return e.definition;
  }
  return {};
}

void Shell::dismissWelcome() {
  settings_.seenWelcome = true;
  applySettings();  // persist, so the sequence plays once
  if (screen_ == Screen::Welcome) screen_ = Screen::MainMenu;
}

std::string Shell::surpriseVariant() {
  if (library_.empty()) return "standard";
  // Rotate, skipping standard so "surprise" is always one of the strange ones.
  for (std::size_t i = 0; i < library_.size(); ++i) {
    const std::string& name = library_[(surpriseCursor_ + i) % library_.size()];
    if (name != "standard") {
      surpriseCursor_ = (surpriseCursor_ + i + 1) % library_.size();
      return name;
    }
  }
  return library_.front();
}

bool Shell::showsBoard() const noexcept {
  // The pause and reference screens sit over the position: the board stays behind them,
  // pulled back and blurred, which is the whole look of stepping away from a game.
  switch (screen_) {
    case Screen::Game:
    case Screen::Paused:
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return session_ != nullptr;
    case Screen::Settings:
      return session_ != nullptr && previous_ != Screen::MainMenu;
    default:
      return false;
  }
}

bool Shell::showsCheckWarning() const noexcept {
  // The warning belongs to the board. Every screen that keeps the board behind it gets
  // it; a position left behind on the main menu - a game abandoned but kept for a later
  // resume - does not, because it is not a warning about the screen the player is looking
  // at. The trailer now runs on the game screen like any watched game, so a checked beat
  // would show it too (M13.9 revision, Part G).
  if (screen_ == Screen::MainMenu) return false;
  return showsBoard() && session_ != nullptr && session_->game().inCheck();
}

float Shell::boardPullBack() const noexcept {
  if (session_ == nullptr) return 0.0f;
  switch (screen_) {
    case Screen::Game:
      return 0.0f;
    case Screen::Paused:
      return 1.0f;
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return 2.0f;
    case Screen::Settings:
      return previous_ != Screen::MainMenu ? 2.0f : 0.0f;
    default:
      return 0.0f;
  }
}

void Shell::go(Screen s) {
  if (s == screen_) return;
  // Opening the editor with nothing loaded edits the current variant, or standard as a
  // base. A failure leaves the screen with no document, which the screen reports rather
  // than trapping the player on a menu.
  if (s == Screen::Editor && editor_ == nullptr) {
    (void)openEditor(currentVariant_.empty() ? "standard" : currentVariant_);
  }
  // Only remember a screen worth returning to: bouncing between two overlays should
  // still take you back to the game.
  if (screen_ != Screen::Settings && screen_ != Screen::GameInfo &&
      screen_ != Screen::PieceMoves && screen_ != Screen::Editor) {
    previous_ = screen_;
  }
  screen_ = s;
}

void Shell::back() {
  switch (screen_) {
    case Screen::Settings:
    case Screen::Editor:
    case Screen::NewGame:
    case Screen::Watch:
    case Screen::QuitConfirm:
    case Screen::PauseQuitConfirm:
      screen_ = previous_;
      break;
    case Screen::GameInfo:
    case Screen::PieceMoves:
      screen_ = session_ != nullptr ? Screen::Paused : Screen::MainMenu;
      break;
    case Screen::Paused:
      screen_ = Screen::Game;
      break;
    case Screen::Game:
      screen_ = Screen::Paused;
      break;
    case Screen::MainMenu:
      break;
    case Screen::Welcome:
      // The tutorial is a screen off the menu now, so backing out returns there.
      screen_ = Screen::MainMenu;
      break;
  }
}

void Shell::setVariantLoader(VariantLoader loader) {
  loader_ = std::move(loader);
  difficulty_.clear();
  // Difficulty first, then name. Ties are alphabetical, so the order is stable and
  // predictable rather than whatever the filesystem handed over.
  std::stable_sort(library_.begin(), library_.end(),
                   [this](const std::string& a, const std::string& b) {
                     const Difficulty da = difficultyOf(a);
                     const Difficulty db = difficultyOf(b);
                     if (da != db) return static_cast<int>(da) < static_cast<int>(db);
                     return a < b;
                   });
}

Difficulty Shell::difficultyOf(const std::string& name) const {
  // A running game already holds the spec, and it is the same one.
  if (session_ != nullptr && currentVariant_ == name)
    return session_->variant().difficulty;
  if (const auto it = difficulty_.find(name); it != difficulty_.end()) return it->second;
  Difficulty d = Difficulty::Other;
  if (loader_) {
    auto v = loader_(name);
    if (v.has_value()) d = v->difficulty;
  }
  difficulty_.emplace(name, d);
  return d;
}

Result<void> Shell::openEditor(const std::string& variantName) {
  if (!sourceLoader_) {
    return fail(ErrorCode::Internal, "no variant source loader is installed");
  }
  auto text = sourceLoader_(variantName);
  if (!text.has_value()) return fail(text.error().code, text.error().message);
  auto editor = Editor::open(*text, variantName);
  if (!editor.has_value()) {
    return fail(editor.error().code, editor.error().message, editor.error().line);
  }
  editor_ = std::make_unique<Editor>(std::move(*editor));
  editorVariant_ = variantName;
  editorPath_ = pathResolver_ ? pathResolver_(variantName) : std::filesystem::path{};
  return {};
}

Result<void> Shell::saveEditor() {
  if (editor_ == nullptr) {
    return fail(ErrorCode::Internal, "nothing is open in the editor");
  }
  if (editorPath_.empty()) {
    return fail(ErrorCode::Internal, "no save path is installed for the editor");
  }
  return editor_->saveFile(editorPath_);
}

Result<void> Shell::startGame(const std::string& variantName) {
  if (!loader_) return fail(ErrorCode::Internal, "no variant loader is installed");
  auto variant = loader_(variantName);
  if (!variant.has_value()) {
    message_ = variant.error().format();
    return fail(variant.error().code, variant.error().message);
  }
  auto session = Session::create(std::move(*variant));
  if (!session.has_value()) {
    message_ = session.error().format();
    return fail(session.error().code, session.error().message);
  }
  session_ = std::move(*session);
  currentVariant_ = variantName;
  settings_.lastVariant = variantName;
  message_.clear();
  applySettings();
  screen_ = Screen::Game;
  previous_ = Screen::MainMenu;
  return {};
}

std::filesystem::path Shell::gamesDir() const {
  return settingsPath_.parent_path() / "games";
}

namespace {
/// A name safe to turn into a file name: no separators, no traversal, nothing exotic.
bool validGameName(const std::string& name) {
  if (name.empty() || name.size() > 64) return false;
  for (const char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}
}  // namespace

Result<void> Shell::saveGame(const std::string& name) {
  if (session_ == nullptr)
    return fail(ErrorCode::ValidationError, "there is no game to save");
  if (!validGameName(name)) {
    message_ = "a game name can use letters, digits, spaces, '-' and '_'";
    return fail(ErrorCode::ValidationError, message_);
  }
  std::error_code ec;
  std::filesystem::create_directories(gamesDir(), ec);
  const auto path = gamesDir() / (name + ".cbgame");
  if (auto ok = session_->saveGame(path); !ok.has_value()) {
    message_ = ok.error().message;
    return ok;
  }
  message_ = "saved '" + name + "'";
  return {};
}

Result<void> Shell::loadGame(const std::string& name) {
  if (!validGameName(name)) {
    message_ = "a game name can use letters, digits, spaces, '-' and '_'";
    return fail(ErrorCode::ValidationError, message_);
  }
  std::ifstream in(gamesDir() / (name + ".cbgame"));
  if (!in) {
    message_ = "no saved game called '" + name + "'";
    return fail(ErrorCode::ValidationError, message_);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  auto file = parseGameFile(ss.str());
  if (!file.has_value()) {
    message_ = file.error().format();
    return fail(file.error().code, file.error().message);
  }
  // The file names its variant; start it first if it is not the one already loaded.
  if (session_ == nullptr || file->variant != currentVariant_) {
    if (auto ok = startGame(file->variant); !ok.has_value()) return ok;
  }
  if (auto ok = session_->loadGame(*file); !ok.has_value()) {
    message_ = ok.error().message;
    return ok;
  }
  message_ = "loaded '" + name + "'";
  screen_ = Screen::Game;
  previous_ = Screen::Paused;
  return {};
}

std::vector<std::string> Shell::savedGames() const {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(gamesDir(), ec)) {
    if (e.is_regular_file() && e.path().extension() == ".cbgame") {
      names.push_back(e.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

Result<void> Shell::startWatching(const std::string& name) {
  const WatchGame* found = nullptr;
  for (const WatchGame& g : bundled_) {
    if (g.name == name) {
      found = &g;
      break;
    }
  }
  if (found == nullptr) {
    message_ = "no bundled game called '" + name + "'";
    return fail(ErrorCode::ValidationError, message_);
  }
  if (auto ok = startGame(found->file.variant); !ok.has_value()) return ok;
  if (auto ok = session_->startWatching(found->file, settings_.watchDwell);
      !ok.has_value()) {
    message_ = ok.error().format();
    return ok;
  }
  screen_ = Screen::Game;
  previous_ = Screen::MainMenu;
  return {};
}

void Shell::stopWatching() {
  if (session_ != nullptr) session_->stopWatching();
  go(Screen::MainMenu);
}

bool Shell::watching() const noexcept {
  return session_ != nullptr && session_->watching();
}

void Shell::advanceWatching(float dt) {
  if (session_ == nullptr || !session_->watching()) return;
  if (!session_->advanceWatching(dt)) go(Screen::MainMenu);
}

AttractSequence defaultAttractSequence() {
  // Standard -> cylinder -> torus -> Klein bottle -> (Klein eases back to a torus) -> the
  // 8x8x4 3-D torus, then round again. Six beats on five shapes: the hypercube was
  // dropped (M13.9 revision, Part I); `hyper4_8x8.toml` and `embedOnto` remain,
  // independently tested but unreferenced here. A Mobius beat was tried here too
  // (standard -> Mobius -> cylinder -> ...) and then dropped: Mobius has no pawn piece
  // type, and carrying a position through it for real continuity would drop the pawns
  // permanently with no way to bring them back for any later beat - a real, lasting cost
  // for a shape this sequence does not need. Every line is real, legal play, verified
  // through the engine's own legality: `startWatching` replays it move by move and a bad
  // move fails the load.
  //
  // Every cut between shapes is a real, continuous geometry morph, not a hard swap - the
  // whole point of the first half of this revision. Cylinder's own end state
  // (`formed = 1`, an open tube) is bit-for-bit the torus morph's own `kTorusRollEnd`
  // waypoint (`view/morph.hpp`), so the torus beat starts there and eases the ring
  // closed. The torus beat's own end state is bit-for-bit the Klein morph's own
  // `kKleinRingEnd` waypoint - a plain ring, no fold, no twist - so the Klein beat starts
  // there and eases the fold, then the twist. A second, move-free Klein beat eases
  // `formed` back down to that same `kKleinRingEnd` waypoint - undoing the fold and the
  // twist, "morph to torus temporarily" - before the cut into the 3-D torus, which opens
  // with every level collapsed onto that one ring (`spreadFrom = 0`) and spreads them
  // apart over the beat - "copy the middle torus a few times." Only the very first beat
  // (`standard`) has nothing to morph from.
  //
  // The sequence opens on the literal `standard` board, then hard-cuts into the cylinder
  // beat already part-way up its roll. This is the documented workaround for the M18.6
  // rank-mirrored flat board at `geometryFormed = 0`: the reveal's flat end would show
  // black nearest the camera and white far off, the reverse of a normal board, so the
  // trailer starts on a clean board and picks the roll up in motion instead. See the
  // status note in `docs/plan/M13-overtures.md`.
  //
  // Part F, resolved in full: every beat but the loop's own re-entry point continues the
  // *same* game (`continuesPosition`, `embedOnto`) - one real position, cut after cut,
  // from the literal standard start all the way to the 3-D torus. Klein and
  // `torus3d_8x8` field only king, rook and knight at their own declared start (no
  // bishop, no queen, no pawn) because - their own files say so - a bishop or queen gives
  // check from almost anywhere once every rank and file (and, on the 3-D torus, every
  // level) wraps; embedding one onto either reliably produces exactly that, confirmed
  // directly with the CLI's own `embed` command, not assumed. The fix is not to stop
  // continuing there, as an earlier revision of this sequence did - it is to make sure
  // every bishop and both queens are actually gone, via real captures, by the time the
  // game reaches Klein. Standard and cylinder trade down hard (both bishop pairs, both
  // queens - some of it finishing as the very first move after a cut, a king recapturing
  // under check on the shape it just arrived on, which is a perfectly legal way for a
  // beat to open); by the end of torus nothing is left but kings, rooks, a knight and
  // pawns, and the pawns themselves are silently dropped at the Klein cut the same way
  // `embedOnto` always drops a piece type the target does not declare - no special
  // handling needed, because nothing downstream of Klein ever needs them back. Klein's
  // own two beats carry a position between themselves too (`continuesPosition` on the
  // fold-out beat), since nothing is cut there - it is the same variant both beats stand
  // on, so the pieces should not visibly jump at a fold that is not a shape cut.
  //
  // The loop's own re-entry beat (cylinder, `loopEntryIndex()`) is the one place this
  // cannot extend to. `continuesPosition` is still set on it - true for the *one* real
  // predecessor it has, `standard`, reached once at the very start - but
  // `Shell::startAttractStep` falls back to a fresh start there on every wrap after the
  // first (`attractLooped_`): a looping sequence replays the same fixed `moves` list
  // every lap, and by the second lap the previous one has usually traded down to a
  // near-bare king (the shipped line's own `torus3d_8x8` beat ends with white down to a
  // lone king), against which cylinder's own scripted ten plies are not a legal
  // continuation at all. Continuing the wrap would make the sequence correct for exactly
  // one lap and broken forever after. See the status note in `docs/plan/M13-overtures.md`
  // for the fuller version of this trade-off.
  //
  // Every line below is ten plies (five moves a side) of one continuous game, long enough
  // to show real development and not just an opening tag. Picking them was not just "any
  // legal line reaches ten plies, trades the right pieces": transplanting an opening's
  // armies onto a rank-periodic board is its own hazard independent of piece roster -
  // `torus.toml`'s own comment explains why its start array sits four ranks apart rather
  // than in the usual corners ("rank 1 and rank 8 are *adjacent*... White is already in
  // an inescapable check"), and the same effect reaches any piece still near the board's
  // own back rank, not just the kings. Cylinder's own ten plies end with both kings
  // stepped one square off their home rank for exactly that reason - confirmed
  // empirically, the same discipline as everywhere else in this file, after a first
  // attempt that left both kings at home folded to one legal move the instant it was
  // embedded onto the rank-periodic torus. Torus itself declares no castling at all (its
  // own file again: "with the files glued, between the king and the rook is ambiguous"),
  // so the king steps there are plain moves, not castling - castling, where it is used
  // earlier (cylinder supports it), only ever changes a king's file, never its rank, so
  // it would not have solved this anyway.
  AttractSequence seq;
  seq.push_back({.variant = "standard",
                 .moves = {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5c6", "d7c6",
                           "d2d4", "e5d4"},
                 .formedFrom = 1.0f});
  seq.push_back({.variant = "cylinder",
                 .moves = {"f3d4", "d8d4", "d1d4", "f8c5", "d4c5", "b7b6", "c5e3", "c8e6",
                           "b1c3", "g8f6"},
                 .formedFrom = 0.30f,
                 // The loop's own re-entry line, every lap after the first: `standard`'s
                 // own ten plies followed by these same ten, replayed fresh from
                 // cylinder's own start rather than continued onto it. Cylinder and
                 // standard share an army and a start array, and none of these twenty
                 // plies go near the file seam, so replaying them fresh here reaches the
                 // same board the continuation path reaches by embedding - confirmed
                 // directly, not assumed - which is what lets every beat after this one
                 // keep its own single `moves` list regardless of which path cylinder
                 // took to get there.
                 .resetMoves = {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5c6",
                                "d7c6", "d2d4", "e5d4", "f3d4", "d8d4", "d1d4", "f8c5",
                                "d4c5", "b7b6", "c5e3", "c8e6", "b1c3", "g8f6"},
                 .continuesPosition = true});
  seq.push_back({.variant = "torus",
                 .moves = {"e1e2", "e8d7", "c1d2", "f6g4", "e3f4", "a8d8", "f4g4", "d8d2",
                           "e2d2", "d7e7"},
                 .formedFrom = view::kTorusRollEnd,
                 .continuesPosition = true});
  // Klein hides its far side, so it ghosts and draws flat icon pucks throughout both of
  // its beats (M13.9 revision, Part H), not just the fold-in. Its first move is the
  // queen capturing the last bishop left on the board, under check from its own king -
  // the king recaptures next move, clearing the last queen too, finishing the trade-down
  // this beat opens on rather than closes.
  seq.push_back({.variant = "klein",
                 .moves = {"g4e6", "e7e6", "a1a6", "h8a6", "c3e4", "a6a2", "d2e3", "e6e5",
                           "h1d1", "e5f5"},
                 .ghostTarget = 0.35f,
                 .formedFrom = view::kKleinRingEnd,
                 .flatPieces = true,
                 .continuesPosition = true});
  seq.push_back({.variant = "klein",
                 .moves = {},
                 .formedTarget = view::kKleinRingEnd,
                 .ghostTarget = 0.35f,
                 .flatPieces = true,
                 .continuesPosition = true});
  seq.push_back({.variant = "torus3d_8x8",
                 .moves = {"(3,0,0)-(3,0,1)", "(0,1,0)-(0,1,1)", "(4,3,0)-(6,4,0)",
                           "(5,4,0)-(5,5,0)", "(6,4,0)-(7,4,2)", "(0,1,1)-(0,2,1)",
                           "(7,4,2)-(7,6,3)", "(5,5,0)-(5,6,1)", "(7,6,3)-(7,5,1)",
                           "(5,6,1)-(6,6,1)"},
                 .ghostTarget = 0.35f,
                 .formedFrom = 1.0f,
                 .flatPieces = true,
                 .spreadFrom = 0.0f,
                 .continuesPosition = true});
  return seq;
}

Result<void> Shell::startAttractStep(std::size_t index) {
  if (index >= attract_.size()) {
    return fail(ErrorCode::OutOfRange, "attract step out of range");
  }
  const AttractStep step = attract_[index];
  // The loop's own re-entry beat continues only from its *one* real predecessor, reached
  // once at the very start - not from whatever the previous lap happened to end on, which
  // its `moves` was never authored against (`loopEntryIndex`'s own doc comment).
  const bool continuing =
      step.continuesPosition && !(index == loopEntryIndex() && attractLooped_);
  // Continuity (M13.9 Part F): carry the previous beat's own end-of-game position onto
  // this beat's board. Computed - FEN and all - entirely *before* `startGame` below
  // replaces `session_`, and against a freshly loaded `VariantSpec` of our own rather
  // than the new session's eventual one: `Position` holds a raw pointer to its variant
  // (so does the `Position` `embedOnto` returns), and `startGame` loads the target
  // variant a second time into its own session regardless, so there is no way to hand
  // this call a `toVariant` that both exists yet and will still be `session_->variant()`
  // afterwards. Finishing the whole computation - through `toFen`, down to a plain string
  // with no pointers left in it - while the old session and this local variant are both
  // still alive is what the CLI's own `embed` command had to learn the hard way (see its
  // own comment, `src/cli/main.cpp`): building the embedding against a `VariantSpec` that
  // is about to move or be destroyed corrupts it the instant that happens, not safely
  // before.
  std::optional<std::string> embeddedFen;
  if (continuing && session_ != nullptr) {
    auto targetVariant = loader_ ? loader_(step.variant) : fail(ErrorCode::Internal, "");
    if (targetVariant.has_value()) {
      embeddedFen = toFen(
          embedOnto(session_->game().position(), session_->variant(), *targetVariant));
    }
  }
  if (auto ok = startGame(step.variant); !ok.has_value()) return ok;
  // Build the game file from the session's own started position, so its FEN always
  // matches the variant and no author has to keep one in sync (M13.9).
  GameFile file = toGameFile(session_->variant(), session_->game());
  if (embeddedFen.has_value()) file.startFen = *embeddedFen;
  // `step.continuesPosition` asked to continue but this particular visit could not
  // (the loop's own re-entry beat, past the first lap): play `resetMoves` - a line legal
  // from this variant's own fresh start - instead of `moves`, which is only legal against
  // the real continuation this visit does not have.
  file.moves = continuing || !step.continuesPosition ? step.moves : step.resetMoves;
  if (auto ok = session_->startWatching(std::move(file), settings_.watchDwell);
      !ok.has_value()) {
    message_ = ok.error().format();
    return fail(ok.error().code, ok.error().message);
  }
  // A revealing beat starts part-way or from flat; any other beat keeps the pose the last
  // one left, so the shape cuts rather than re-rolling every time.
  if (step.formedFrom >= 0.0f) settings_.geometryFormed = step.formedFrom;
  if (step.spreadFrom >= 0.0f) settings_.geometryShellSpread = step.spreadFrom;
  // The piece style is a discrete per-beat choice, so it snaps at the cut rather than
  // easing like the pose (M13.9 revision, Part H).
  settings_.shapePieceCylinders = step.flatPieces;
  // `startGame` landed us on the ordinary game screen; a trailer beat is a real watch
  // run, so it stays there rather than compositing behind the menu (M13.9 revision, Part
  // G).
  attractIndex_ = index;
  return {};
}

void Shell::startAttract(AttractSequence sequence) {
  stopAttract();
  if (sequence.empty()) return;
  attract_ = std::move(sequence);
  attractIndex_ = 0;
  attractLooped_ = false;
  // The beats own their starting pose (`formedFrom`); the first one is the flat-to-shape
  // reveal. Clear the ghost so a beat before the first ghosting one starts opaque.
  settings_.geometryGhost = 1.0f;
  if (auto ok = startAttractStep(0); !ok.has_value()) attract_.clear();
}

void Shell::stopAttract() {
  if (attract_.empty()) return;
  attract_.clear();
  attractIndex_ = 0;
  if (session_ != nullptr) session_->stopWatching();
  // The Klein and 3-D beats ghost the board; restore the opaque default so whatever the
  // player does next does not inherit the trailer's pose. The flat icon pucks snap on for
  // those same two beats, so they are cleared here too (M13.9 revision, Part H). The 3-D
  // beat's own level spread is cleared the same way, so a game started afterwards opens
  // on the shipped, fully-spread shape rather than the trailer's collapsed one.
  settings_.geometryGhost = 1.0f;
  settings_.shapePieceCylinders = false;
  settings_.geometryShellSpread = 1.0f;
  // The trailer now runs on its own screen, so leaving it returns to the ordinary menu,
  // exactly as `stopWatching()` does (M13.9 revision, Part G).
  go(Screen::MainMenu);
}

bool Shell::advanceAttract(float dt) {
  if (attract_.empty() || session_ == nullptr) return false;
  const AttractStep& step = attract_[attractIndex_];
  if (session_->watching()) session_->advanceWatching(dt);
  settings_.geometryFormed = view::advanceFormMorph(
      settings_.geometryFormed, step.formedTarget, dt, settings_.geometryFormSpeed);
  settings_.geometryGhost = view::advanceFormMorph(
      settings_.geometryGhost, step.ghostTarget, dt, settings_.geometryFormSpeed);
  settings_.geometryShellSpread = view::advanceFormMorph(
      settings_.geometryShellSpread, step.spreadTarget, dt, settings_.geometryFormSpeed);
  constexpr float kSettle = 1e-3f;
  const bool settled =
      std::abs(settings_.geometryFormed - step.formedTarget) <= kSettle &&
      std::abs(settings_.geometryGhost - step.ghostTarget) <= kSettle &&
      std::abs(settings_.geometryShellSpread - step.spreadTarget) <= kSettle;
  // A beat ends only when its game has run out *and* the pose has stopped moving, so a
  // cut never lands mid-roll.
  if (session_->watching() || !settled) return false;
  // The first beat is the trailer's cold open (the literal `standard` board). Once it has
  // played, the shape beats loop among themselves, so the last beat cuts back to the
  // cylinder rather than replaying the opener (M13.9 revision, Part I).
  // Whether this cut wraps - not simply whether `next` equals `loopEntryIndex()`, which
  // the sequence's very first forward step into that same beat also satisfies (the
  // opener is only ever one beat ahead of it). `attractLooped_` has to stay false for
  // that first, real arrival so the loop's own re-entry beat still gets its one genuine
  // continuation, and only flips once the sequence has actually run off its own end.
  const bool wrapping = attractIndex_ + 1 >= attract_.size();
  const std::size_t next = wrapping ? loopEntryIndex() : attractIndex_ + 1;
  if (wrapping) attractLooped_ = true;
  if (auto ok = startAttractStep(next); !ok.has_value()) {
    attract_.clear();
    return false;
  }
  return true;
}

CameraCue Shell::currentCue() const {
  if (session_ == nullptr) return {};
  return {currentVariant_, session_->game().moveHistory().size()};
}

std::filesystem::path Shell::cameraTrackPath() const {
  return settingsPath_.parent_path() / "camera_track.txt";
}

void Shell::persistCameraTrack() const {
  std::ofstream out(cameraTrackPath(), std::ios::trunc);
  if (!out) return;  // a camera track is a convenience, not something play should halt on
  out << cameraTrackText(cameraTrack_);
}

void Shell::recordCameraKeyframe(float speed) {
  if (session_ == nullptr) return;
  CameraKeyframe kf;
  kf.cue = currentCue();
  kf.camera = session_->camera();
  kf.formed = settings_.geometryFormed;
  kf.ghost = settings_.geometryGhost;
  kf.shellSpread = settings_.geometryShellSpread;
  kf.slideU = settings_.geometrySlideU;
  kf.slideV = settings_.geometrySlideV;
  kf.flatPieces = settings_.shapePieceCylinders;
  kf.morphSpeed = speed;
  cameraTrack_.set(std::move(kf));
  persistCameraTrack();
}

bool Shell::eraseCameraKeyframe() {
  if (session_ == nullptr) return false;
  const bool removed = cameraTrack_.erase(currentCue());
  if (removed) persistCameraTrack();
  return removed;
}

void Shell::advanceCameraTrack(float dt) {
  if (session_ == nullptr || cameraTrack_.empty()) return;
  const CameraCue cue = currentCue();
  if (!cameraTrackCue_.has_value() || !(*cameraTrackCue_ == cue)) {
    cameraTrackCue_ = cue;
    if (cameraTrack_.find(cue) != nullptr) {
      // A new cue with a keyframe: capture the view exactly as it stands right now as the
      // blend's own start, so the ease is from wherever play actually is, not from the
      // *previous* keyframe's own endpoint (the player may have kept orbiting since).
      cameraTrackFrom_ = session_->camera();
      cameraTrackFromFormed_ = settings_.geometryFormed;
      cameraTrackFromGhost_ = settings_.geometryGhost;
      cameraTrackFromShellSpread_ = settings_.geometryShellSpread;
      cameraTrackFromSlideU_ = settings_.geometrySlideU;
      cameraTrackFromSlideV_ = settings_.geometrySlideV;
      cameraTrackT_ = 0.0f;
    } else {
      cameraTrackT_ = 1.0f;  // nothing to ease toward at this cue - leave the view alone
    }
  }
  if (cameraTrackT_ >= 1.0f) return;
  const CameraKeyframe* kf = cameraTrack_.find(cue);
  if (kf == nullptr) {  // the keyframe was erased mid-blend
    cameraTrackT_ = 1.0f;
    return;
  }
  cameraTrackT_ = std::min(1.0f, cameraTrackT_ + dt * std::max(0.01f, kf->morphSpeed));
  const float t = cameraTrackT_;
  session_->setOrbitCamera(view::slerpCamera(cameraTrackFrom_, kf->camera, t));
  settings_.geometryFormed =
      cameraTrackFromFormed_ + (kf->formed - cameraTrackFromFormed_) * t;
  settings_.geometryGhost =
      cameraTrackFromGhost_ + (kf->ghost - cameraTrackFromGhost_) * t;
  settings_.geometryShellSpread =
      cameraTrackFromShellSpread_ + (kf->shellSpread - cameraTrackFromShellSpread_) * t;
  settings_.geometrySlideU =
      cameraTrackFromSlideU_ + (kf->slideU - cameraTrackFromSlideU_) * t;
  settings_.geometrySlideV =
      cameraTrackFromSlideV_ + (kf->slideV - cameraTrackFromSlideV_) * t;
  settings_.shapePieceCylinders = kf->flatPieces;
}

void Shell::pause() {
  if (session_ != nullptr) go(Screen::Paused);
}

std::string Shell::variantDescription(const std::string& name) const {
  // Asking the running session first saves reloading a file the game already holds.
  if (session_ != nullptr && currentVariant_ == name) {
    return session_->variant().description;
  }
  if (!loader_) return {};
  auto v = loader_(name);
  return v.has_value() ? v->description : std::string{};
}

const VariantSpec* Shell::preview(const std::string& name) const {
  if (name.empty()) return nullptr;
  // A running game already holds the spec, and it is the same one.
  if (session_ != nullptr && currentVariant_ == name) return &session_->variant();
  if (previewName_ == name) return preview_.get();
  if (!loader_) return nullptr;
  auto v = loader_(name);
  previewName_ = name;
  preview_ = v.has_value() ? std::make_unique<VariantSpec>(std::move(*v)) : nullptr;
  return preview_.get();
}

void Shell::resume() {
  if (session_ != nullptr) go(Screen::Game);
}

void Shell::applySettings() {
  settings_.sanitize();
  // The library overtures have their own clock, so their speed is applied to the player
  // rather than to the session - and it applies whether or not a game is loaded.
  overtures_.setSpeed(settings_.overtureSpeed);
  if (session_ != nullptr) {
    session_->setFlatView(settings_.flatView);
    session_->setHotSeat(settings_.hotSeat);
    session_->setConfirmMoves(settings_.confirmMoves);
    session_->setCameraMode(settings_.cameraMode);
    session_->setFollowStrength(settings_.followStrength);
    session_->setTheme(view::themeFromName(settings_.theme));
    // Seconds per cell travelled. Nought-point-nought-eight-five at speed 1 is about
    // as fast as a move can be read; switching animation off means zero, not "instant",
    // so the session has one rule for "do not animate" rather than two.
    session_->setAnimationSeconds(
        settings_.animateMoves ? 0.085f / settings_.animationSpeed : 0.0f);
    Action a;
    a.kind = ActionKind::SetPromotion;
    a.text = settings_.autoPromoteTo;
    // An empty preference means "always ask", which the session expresses by having no
    // preference at all rather than by a sentinel piece.
    if (!settings_.autoPromoteTo.empty()) (void)session_->apply(a);
  }
  if (auto ok = settings_.save(settingsPath_); !ok.has_value()) {
    message_ = ok.error().message;
  }
}

}  // namespace cb::app
