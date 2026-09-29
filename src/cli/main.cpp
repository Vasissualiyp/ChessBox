// SPDX-License-Identifier: GPL-3.0-or-later
// A scriptable command-line front end. Deliberately line-oriented so the golden
// tests can drive it in batch mode, and so a human can reproduce any engine
// behaviour a test reports.
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "io/fen.hpp"
#include "io/notation.hpp"
#include "io/variant_toml.hpp"
#include "movegen/movegen.hpp"
#include "variant/standard.hpp"

namespace {

using namespace cb;

struct Session {
  VariantSpec variant;
  Position position;
  MoveGen gen;

  explicit Session(VariantSpec v)
      : variant(std::move(v)), position(Position::startPosition(variant)), gen(variant) {}
};

/// ASCII board. For boards with more than two axes, one 2-D slice per
/// combination of the remaining axes, labelled with its coordinates - the same
/// projection idea the renderer uses, in text (M2.5).
void printBoard(const Position& p) {
  const VariantSpec& v = p.variant();
  const DimSpec& d = v.dims;
  const std::uint8_t n = d.dims();

  std::vector<std::int16_t> outer(n, 0);
  const auto printSlice = [&] {
    if (n > 2) {
      std::cout << "slice";
      for (std::uint8_t a = 2; a < n; ++a) {
        std::cout << ' ' << d.name(a) << '=' << outer[a];
      }
      std::cout << '\n';
    }
    for (int y = d.extent(1) - 1; y >= 0; --y) {
      std::cout << (d.extent(1) <= 9 ? std::to_string(y + 1) : std::to_string(y)) << ' ';
      for (int x = 0; x < d.extent(0); ++x) {
        Coord c(n);
        c.c[0] = static_cast<std::int16_t>(x);
        c.c[1] = static_cast<std::int16_t>(y);
        for (std::uint8_t a = 2; a < n; ++a) c.c[a] = outer[a];
        const Piece piece = p.at(d.toCell(c));
        char ch = '.';
        if (!piece.empty()) {
          const char sym = v.pieces[piece.type].symbol;
          ch = piece.colorOf() == Color::White ? static_cast<char>(std::toupper(sym))
                                               : static_cast<char>(std::tolower(sym));
        }
        std::cout << ch << ' ';
      }
      std::cout << '\n';
    }
    std::cout << "  ";
    for (int x = 0; x < d.extent(0); ++x) {
      std::cout << (d.extent(0) <= 26 ? static_cast<char>('a' + x) : '?') << ' ';
    }
    std::cout << '\n';
  };

  if (n <= 2) {
    printSlice();
  } else {
    // Odometer over the axes beyond the first two.
    for (;;) {
      printSlice();
      std::uint8_t a = 2;
      for (;; ++a) {
        if (a >= n) return;
        if (outer[a] + 1 < d.extent(a)) {
          ++outer[a];
          break;
        }
        outer[a] = 0;
      }
      std::cout << '\n';
    }
  }
}

void printHelp() {
  std::cout << "commands:\n"
               "  load <name|path>     load a variant (bare name looks in variants/)\n"
               "  board                print the position\n"
               "  fen [<fen>]          print, or set, the position\n"
               "  moves                list legal moves\n"
               "  move <text>          play a move, e.g. e2e4 or e7e8q\n"
               "  undo                 take back the last move\n"
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
  Session s(std::move(*initial));
  std::vector<std::pair<Move, Undo>> history;

  std::vector<std::string> queued;
  for (int i = 1; i < argc; ++i) queued.emplace_back(argv[i]);

  std::string line;
  std::size_t queueIndex = 0;
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
        std::cout << "error: " << v.error().format() << '\n';
        continue;
      }
      s = Session(std::move(*v));
      history.clear();
      std::cout << "loaded " << s.variant.name << '\n';
    } else if (cmd == "board") {
      printBoard(s.position);
    } else if (cmd == "fen") {
      std::string rest;
      std::getline(in, rest);
      const auto firstNonSpace = rest.find_first_not_of(' ');
      if (firstNonSpace == std::string::npos) {
        std::cout << toFen(s.position) << '\n';
      } else {
        auto p = fromFen(s.variant, rest.substr(firstNonSpace));
        if (!p.has_value()) {
          std::cout << "error: " << p.error().format() << '\n';
          continue;
        }
        s.position = std::move(*p);
        history.clear();
      }
    } else if (cmd == "moves") {
      MoveList legal(s.variant.moveUpperBound());
      s.gen.generateLegal(s.position, legal);
      std::vector<std::string> texts;
      for (const Move& m : legal) texts.push_back(moveText(s.variant, m));
      std::sort(texts.begin(), texts.end());
      for (const std::string& t : texts) std::cout << t << '\n';
      std::cout << texts.size() << " legal move(s)\n";
    } else if (cmd == "move") {
      std::string text;
      in >> text;
      MoveList legal(s.variant.moveUpperBound());
      s.gen.generateLegal(s.position, legal);
      bool played = false;
      for (const Move& m : legal) {
        if (moveText(s.variant, m) == text) {
          Undo u;
          s.position.make(m, u);
          history.emplace_back(m, u);
          played = true;
          break;
        }
      }
      std::cout << (played ? "ok" : "error: no such legal move") << '\n';
    } else if (cmd == "undo") {
      if (history.empty()) {
        std::cout << "error: nothing to undo\n";
      } else {
        s.position.unmake(history.back().first, history.back().second);
        history.pop_back();
        std::cout << "ok\n";
      }
    } else if (cmd == "perft" || cmd == "divide") {
      int depth = 1;
      in >> depth;
      const auto t0 = std::chrono::steady_clock::now();
      if (cmd == "perft") {
        const std::uint64_t nodes = s.gen.perft(s.position, depth);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - t0)
                            .count();
        std::cout << "perft(" << depth << ") = " << nodes << "  in " << ms << " ms";
        if (ms > 0) std::cout << "  (" << (nodes / static_cast<std::uint64_t>(ms)) << " knps)";
        std::cout << '\n';
      } else {
        auto parts = s.gen.perftDivide(s.position, depth);
        std::sort(parts.begin(), parts.end(), [&](const auto& a, const auto& b) {
          return moveText(s.variant, a.move) < moveText(s.variant, b.move);
        });
        std::uint64_t total = 0;
        for (const auto& e : parts) {
          std::cout << moveText(s.variant, e.move) << ' ' << e.nodes << '\n';
          total += e.nodes;
        }
        std::cout << "moves " << parts.size() << "  nodes " << total << '\n';
      }
    } else if (cmd == "info") {
      const VariantSpec& v = s.variant;
      std::cout << "variant   " << v.name << "  id 0x" << std::hex << v.variantId() << std::dec
                << "\naxes      ";
      for (std::uint8_t a = 0; a < v.dims.dims(); ++a) {
        std::cout << v.dims.name(a) << '[' << v.dims.extent(a) << "]("
                  << toString(v.dims.kind(a)) << ") ";
      }
      std::cout << "\ncells     " << v.dims.cellCount() << "\ndirections " << v.dirTable.size()
                << "\ngeometry  " << (v.geom.isBox() ? "box" : "identified")
                << (v.geom.isOrientable() ? ", orientable" : ", NON-ORIENTABLE") << '\n';
      for (std::size_t i = 1; i < v.pieces.size(); ++i) {
        std::cout << "  " << v.pieces[i].symbol << ' ' << v.pieces[i].name
                  << (v.pieces[i].royal ? " (royal)" : "") << ": ";
        for (const MoveAtom& a : v.pieces[i].atoms) std::cout << a.toString() << "  ";
        std::cout << '\n';
      }
    } else {
      std::cout << "error: unknown command '" << cmd << "' (try 'help')\n";
    }
  }
  return 0;
}
