// SPDX-License-Identifier: GPL-3.0-or-later
// A scriptable command-line front end. Deliberately line-oriented so the golden
// tests can drive it in batch mode, and so a human can reproduce any engine
// behaviour a test reports.
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "game/game.hpp"
#include "io/ascii_board.hpp"
#include "io/fen.hpp"
#include "io/game_file.hpp"
#include "io/notation.hpp"
#include "io/variant_toml.hpp"
#include "movegen/movegen.hpp"
#include "variant/standard.hpp"

namespace {

using namespace cb;

/// Variant, position and generator together.
///
/// Position and MoveGen hold a pointer to the VariantSpec, so the spec must not
/// move once they exist. A Session is therefore neither copyable nor movable and is
/// always held behind a unique_ptr - replacing a loaded variant creates a new
/// Session rather than assigning over the old one, which would move the spec out
/// from under the two objects referring to it.
struct Session {
  VariantSpec variant;
  Position start;
  Position position;
  MoveGen gen;

  explicit Session(VariantSpec v)
      : variant(std::move(v)),
        start(Position::startPosition(variant)),
        position(start),
        gen(variant) {}

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Session(Session&&) = delete;
  Session& operator=(Session&&) = delete;
};

void printHelp() {
  std::cout << "commands:\n"
               "  load <name|path>     load a variant (bare name looks in variants/)\n"
               "  board                print the position\n"
               "  fen [<fen>]          print, or set, the position\n"
               "  moves                list legal moves\n"
               "  move <text>          play a move, e.g. e2e4 or e7e8q\n"
               "  undo                 take back the last move\n"
               "  save <file>          write the game (variant, start, moves)\n"
               "  open <file>          load a game file, replacing the current game\n"
               "  perft <depth>        count leaf nodes\n"
               "  divide <depth>       perft split by first move\n"
               "  info                 variant summary\n"
               "  quit\n";
}

}  // namespace

int main(int argc, char** argv) {
  auto initial = makeStandardChess();
  if (!initial.has_value()) {
    std::cerr << "internal: " << initial.error().format() << '\n';
    return 1;
  }
  auto s = std::make_unique<Session>(std::move(*initial));
  std::vector<std::pair<Move, Undo>> history;

  std::vector<std::string> queued;
  for (int i = 1; i < argc; ++i) queued.emplace_back(argv[i]);

  std::string line;
  std::size_t queueIndex = 0;
  // In batch mode an error must be fatal: a failed `load` followed by a `perft`
  // would otherwise silently report numbers for the previous variant.
  const bool batch = !queued.empty();
  int exitCode = 0;
  const auto reportError = [&](const std::string& message) {
    std::cout << "error: " << message << '\n';
    exitCode = 1;
  };
  for (;;) {
    if (queueIndex < queued.size()) {
      line = queued[queueIndex++];
    } else if (!queued.empty()) {
      break;  // batch mode: stop when the arguments run out
    } else if (!std::getline(std::cin, line)) {
      break;
    }

    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    if (cmd.empty() || cmd == "#") continue;

    if (cmd == "quit" || cmd == "exit") break;
    if (cmd == "help") {
      printHelp();
    } else if (cmd == "load") {
      std::string what;
      in >> what;
      const std::filesystem::path direct(what);
      const std::filesystem::path path =
          what.find('/') == std::string::npos && !std::filesystem::exists(direct)
              ? std::filesystem::path("variants") / (what + ".toml")
              : direct;
      auto v = loadVariantFile(path);
      if (!v.has_value()) {
        reportError(v.error().format());
        if (batch) break;
        continue;
      }
      s = std::make_unique<Session>(std::move(*v));
      history.clear();
      std::cout << "loaded " << s->variant.name << '\n';
    } else if (cmd == "board") {
      std::cout << renderBoard(s->position);
    } else if (cmd == "fen") {
      std::string rest;
      std::getline(in, rest);
      const auto firstNonSpace = rest.find_first_not_of(' ');
      if (firstNonSpace == std::string::npos) {
        std::cout << toFen(s->position) << '\n';
      } else {
        auto p = fromFen(s->variant, rest.substr(firstNonSpace));
        if (!p.has_value()) {
          reportError(p.error().format());
          if (batch) break;
          continue;
        }
        // The position a game is set to is where it *begins*, so a save records it.
        s->start = *p;
        s->position = *p;
        history.clear();
      }
    } else if (cmd == "save") {
      std::string file;
      in >> file;
      if (file.empty()) {
        reportError("usage: save <file>");
        if (batch) break;
        continue;
      }
      GameFile gf;
      gf.variant = s->variant.name;
      gf.variantId = s->variant.variantId();
      gf.startFen = toFen(s->start);
      for (const auto& [m, u] : history) {
        (void)u;
        gf.moves.push_back(moveText(s->variant, m));
      }
      std::ofstream out(file, std::ios::trunc);
      if (!out) {
        reportError("cannot write '" + file + "'");
        if (batch) break;
        continue;
      }
      out << gameFileText(gf);
      if (!out) {
        reportError("writing '" + file + "' failed");
        if (batch) break;
        continue;
      }
      std::cout << "saved " << file << " (" << history.size() << " moves)\n";
    } else if (cmd == "open") {
      std::string file;
      in >> file;
      std::ifstream inFile(file);
      if (!inFile) {
        reportError("cannot read '" + file + "'");
        if (batch) break;
        continue;
      }
      std::ostringstream ss;
      ss << inFile.rdbuf();
      auto gf = parseGameFile(ss.str());
      if (!gf.has_value()) {
        reportError(gf.error().format());
        if (batch) break;
        continue;
      }
      // A game file names its variant; load it if it is not already the current one.
      if (gf->variant != s->variant.name) {
        auto v =
            loadVariantFile(std::filesystem::path("variants") / (gf->variant + ".toml"));
        if (!v.has_value()) {
          reportError(v.error().format());
          if (batch) break;
          continue;
        }
        s = std::make_unique<Session>(std::move(*v));
      }
      if (gf->variantId != 0 && gf->variantId != s->variant.variantId()) {
        reportError("game was played on a different build of '" + gf->variant + "'");
        if (batch) break;
        continue;
      }
      auto start = fromFen(s->variant, gf->startFen);
      if (!start.has_value()) {
        reportError(start.error().format());
        if (batch) break;
        continue;
      }
      s->start = *start;
      s->position = *start;
      history.clear();
      bool ok = true;
      for (const std::string& text : gf->moves) {
        MoveList legal(s->variant.moveUpperBound());
        s->gen.generateLegal(s->position, legal);
        const Move* found = nullptr;
        for (const Move& m : legal) {
          if (moveText(s->variant, m) == text) {
            found = &m;
            break;
          }
        }
        if (found == nullptr) {
          reportError("move '" + text + "' is not legal in the loaded game");
          ok = false;
          break;
        }
        Undo u;
        s->position.make(*found, u);
        history.emplace_back(*found, u);
      }
      if (ok) {
        std::cout << "opened " << file << " (" << history.size() << " moves)\n";
      }
    } else if (cmd == "moves") {
      MoveList legal(s->variant.moveUpperBound());
      s->gen.generateLegal(s->position, legal);
      std::vector<std::string> texts;
      for (const Move& m : legal) texts.push_back(moveText(s->variant, m));
      std::sort(texts.begin(), texts.end());
      for (const std::string& t : texts) std::cout << t << '\n';
      std::cout << texts.size() << " legal move(s)\n";
    } else if (cmd == "move") {
      std::string text;
      in >> text;
      MoveList legal(s->variant.moveUpperBound());
      s->gen.generateLegal(s->position, legal);
      bool played = false;
      for (const Move& m : legal) {
        if (moveText(s->variant, m) == text) {
          Undo u;
          s->position.make(m, u);
          history.emplace_back(m, u);
          played = true;
          break;
        }
      }
      if (played) {
        std::cout << "ok\n";
      } else {
        reportError("no such legal move: '" + text + "'");
        if (batch) break;
      }
    } else if (cmd == "undo") {
      if (history.empty()) {
        reportError("nothing to undo");
      } else {
        s->position.unmake(history.back().first, history.back().second);
        history.pop_back();
        std::cout << "ok\n";
      }
    } else if (cmd == "perft" || cmd == "divide") {
      int depth = 1;
      in >> depth;
      const auto t0 = std::chrono::steady_clock::now();
      if (cmd == "perft") {
        const std::uint64_t nodes = s->gen.perft(s->position, depth);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        std::cout << "perft(" << depth << ") = " << nodes << "  in " << ms << " ms";
        if (ms > 0)
          std::cout << "  (" << (nodes / static_cast<std::uint64_t>(ms)) << " knps)";
        std::cout << '\n';
      } else {
        auto parts = s->gen.perftDivide(s->position, depth);
        std::sort(parts.begin(), parts.end(), [&](const auto& a, const auto& b) {
          return moveText(s->variant, a.move) < moveText(s->variant, b.move);
        });
        std::uint64_t total = 0;
        for (const auto& e : parts) {
          std::cout << moveText(s->variant, e.move) << ' ' << e.nodes << '\n';
          total += e.nodes;
        }
        std::cout << "moves " << parts.size() << "  nodes " << total << '\n';
      }
    } else if (cmd == "info") {
      const VariantSpec& v = s->variant;
      std::cout << "variant   " << v.name << "  id 0x" << std::hex << v.variantId()
                << std::dec << "\naxes      ";
      for (std::uint8_t a = 0; a < v.dims.dims(); ++a) {
        std::cout << v.dims.name(a) << '[' << v.dims.extent(a) << "]("
                  << toString(v.dims.kind(a)) << ") ";
      }
      std::cout << "\ncells     " << v.dims.cellCount() << "\ndirections "
                << v.dirTable.size() << "\ngeometry  "
                << (v.geom.isBox() ? "box" : "identified")
                << (v.geom.isOrientable() ? ", orientable" : ", NON-ORIENTABLE") << '\n';
      for (std::size_t i = 1; i < v.pieces.size(); ++i) {
        std::cout << "  " << v.pieces[i].symbol << ' ' << v.pieces[i].name
                  << (v.pieces[i].royal ? " (royal)" : "") << ": ";
        for (const MoveAtom& a : v.pieces[i].atoms) std::cout << a.toString() << "  ";
        std::cout << '\n';
      }
    } else {
      reportError("unknown command '" + cmd + "' (try 'help')");
      if (batch) break;
    }
  }
  return exitCode;
}
