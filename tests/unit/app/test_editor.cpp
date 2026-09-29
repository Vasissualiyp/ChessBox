// SPDX-License-Identifier: GPL-3.0-or-later
//
// M7.0: the editor as a headless document - open, edit, undo/redo, save - driven and
// tested exactly like app::Session, with no window in sight.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "app/editor.hpp"
#include "io/variant_toml.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

MoveAtom knightLeap() {
  MoveAtom a;
  a.mags.push(1);
  a.mags.push(2);
  a.maxK = 1;
  a.mode = MoveMode::Leap;
  auto canon = MoveAtom::canonicalize(a);
  return canon.has_value() ? *canon : a;
}

}  // namespace

TEST_CASE("the editor tracks dirt and undoes an edit", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  CHECK_FALSE(editor->dirty());
  CHECK_FALSE(editor->canUndo());
  CHECK_FALSE(editor->canRedo());

  auto rook = editor->pieceAtoms("rook");
  REQUIRE(rook.has_value());
  auto atoms = *rook;
  atoms.push_back(knightLeap());
  REQUIRE(editor->setPieceAtoms("rook", atoms).has_value());

  CHECK(editor->dirty());
  CHECK(editor->canUndo());
  const auto edited = loadVariantToml(editor->serialize(), "standard");
  const auto original =
      loadVariantToml(readFile(test::variantPath("standard")), "standard");
  REQUIRE(edited.has_value());
  REQUIRE(original.has_value());
  CHECK(edited->variantId() != original->variantId());

  // Undo returns exactly to the shipped game, not merely to something that loads.
  editor->undo();
  CHECK_FALSE(editor->dirty());
  CHECK(editor->canRedo());
  const auto back = loadVariantToml(editor->serialize(), "standard");
  REQUIRE(back.has_value());
  CHECK(back->variantId() == original->variantId());

  // Redo puts the edit back.
  editor->redo();
  const auto again = loadVariantToml(editor->serialize(), "standard");
  REQUIRE(again.has_value());
  CHECK(again->variantId() == edited->variantId());
}

TEST_CASE("a new edit after an undo drops the redo branch", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  editor->setDescription("first");
  editor->undo();
  CHECK(editor->canRedo());
  editor->setDescription("second");
  CHECK_FALSE(editor->canRedo());
  CHECK(editor->description() == "second");
}

TEST_CASE("the editor saves to a file and reopens it", "[unit][app]") {
  auto editor = app::Editor::open(readFile(test::variantPath("standard")), "standard");
  REQUIRE(editor.has_value());
  editor->setDescription("saved from the editor");
  CHECK(editor->dirty());

  const auto path =
      std::filesystem::temp_directory_path() / "chessbox-editor-roundtrip.toml";
  REQUIRE(editor->saveFile(path).has_value());
  CHECK_FALSE(editor->dirty());

  auto reopened = app::Editor::openFile(path);
  REQUIRE(reopened.has_value());
  CHECK(reopened->name() == "standard");
  CHECK(reopened->description() == "saved from the editor");
  CHECK_FALSE(reopened->dirty());
  CHECK_FALSE(reopened->canUndo());

  std::error_code ec;
  std::filesystem::remove(path, ec);
}
