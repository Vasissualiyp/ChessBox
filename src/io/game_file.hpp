// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "base/result.hpp"
#include "game/game.hpp"
#include "variant/variant.hpp"

namespace cb {

/// A saved game: the variant it was played on, the position it began from, and the moves
/// in order, in the engine's own long-algebraic notation (`moveText`).
///
/// Presentation - the camera, the theme, the shape's pose - is deliberately not saved: a
/// loaded game looks however the player's settings say, the way a `VariantId` never
/// carries a model. It is the user-facing form of the replay corpus (ARCH §11) and the
/// input the notation runner (M12.6) and the clip exporter already want.
struct GameFile {
  std::string variant;             ///< the variant name the file was written for
  std::uint64_t variantId{0};      ///< the spec's id, so a rules change is detected
  std::string startFen;            ///< the position the game began from (FEN-N)
  std::vector<std::string> moves;  ///< long-algebraic move text, in order
};

/// The game-file form of a played game. `variant` must be the spec `game` runs on.
[[nodiscard]] GameFile toGameFile(const VariantSpec& variant, const Game& game);

/// Serialize a game file as text. The struct already carries the variant name and id and
/// the moves as text, so no spec is needed.
[[nodiscard]] std::string gameFileText(const GameFile& file);

/// Serialize a played game in one call.
[[nodiscard]] std::string toGameFileText(const VariantSpec& variant, const Game& game);

/// Parse game-file text. Resolves nothing and replays nothing; the caller owns the
/// variant.
[[nodiscard]] Result<GameFile> parseGameFile(std::string_view text);

/// Check that the file's recorded `VariantId` (when it has one) still matches `variant`,
/// so a rules change since the save is refused rather than silently misapplied. Shared by
/// `applyGameFile` and the M12.6 runner so both report the same mismatch.
[[nodiscard]] Result<void> verifyGameFileVariant(const GameFile& file,
                                                 const VariantSpec& variant);

/// The message `applyGameFile` and the M12.6 runner both report for a move the engine
/// rejects: names the 1-based ply and the move text. One definition, so a runner can
/// never drift from the loader's wording.
[[nodiscard]] std::string gameFileIllegalMove(std::size_t ply, std::string_view text);

/// Replay a parsed game into `game`, which must already be the resolved variant. Checks
/// the recorded `variantId` (a rules change is an error, not a silent misreplay), sets
/// the start position, and plays each move. Fails naming the first move the engine
/// rejects.
[[nodiscard]] Result<void> applyGameFile(const GameFile& file, Game& game);

}  // namespace cb
