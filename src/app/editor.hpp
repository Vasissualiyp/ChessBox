// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "base/result.hpp"
#include "io/variant_doc.hpp"

namespace cb::app {

/// The authoring document with the editing around it that a screen needs: dirty
/// tracking, undo/redo, and save. It owns no window and no Vulkan, so it is driven -
/// and tested - exactly like `app::Session`: edits in, state out.
///
/// Undo is snapshot-based: every accepted edit records the document's serialized text.
/// A variant file is small, correctness is worth more than the bytes, and it means undo
/// can never half-restore a structure the way a hand-written inverse would.
class Editor {
 public:
  static Result<Editor> open(std::string_view text, std::string_view sourceName);
  static Result<Editor> openFile(const std::filesystem::path& path);

  [[nodiscard]] const std::string& sourceName() const noexcept { return sourceName_; }
  [[nodiscard]] Result<void> saveFile(const std::filesystem::path& path);

  [[nodiscard]] bool dirty() const noexcept { return cursor_ != savedCursor_; }

  [[nodiscard]] bool canUndo() const noexcept { return cursor_ > 0; }
  [[nodiscard]] bool canRedo() const noexcept { return cursor_ + 1 < history_.size(); }
  void undo();
  void redo();

  [[nodiscard]] std::string name() const;
  void setName(std::string name);
  [[nodiscard]] std::string description() const;
  void setDescription(std::string text);

  [[nodiscard]] std::vector<std::string> pieceNames() const;
  [[nodiscard]] Result<std::vector<MoveAtom>> pieceAtoms(std::string_view piece) const;
  [[nodiscard]] Result<void> setPieceAtoms(std::string_view piece,
                                           const std::vector<MoveAtom>& atoms);

  [[nodiscard]] std::string serialize() const;

 private:
  Editor(VariantDoc doc, std::string sourceName);

  /// Record the state before a mutation and, if it succeeded, the state after it, so a
  /// failed edit leaves the history untouched.
  template <class Fn>
  Result<void> edit(Fn&& fn);

  [[nodiscard]] Result<void> resetTo(const std::string& snapshot);

  VariantDoc doc_;
  std::string sourceName_;
  std::vector<std::string> history_;
  std::size_t cursor_{0};
  std::size_t savedCursor_{0};
};

}  // namespace cb::app
