// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace cb {

enum class ErrorCode : std::uint8_t {
  ParseError,       // malformed input text
  ValidationError,  // well-formed but semantically invalid
  OutOfRange,
  Unsupported,
  InvalidArgument,  // a supplied argument is not usable
  BudgetExceeded,   // variant exceeds a configured resource budget
  Internal,         // a broken invariant; should never reach a user
};

std::string_view toString(ErrorCode c) noexcept;

/// An error carrying a human-readable message. `line` is 1-based when the error
/// originates in a text file, 0 otherwise.
struct Error {
  ErrorCode code{ErrorCode::Internal};
  std::string message;
  int line{0};

  [[nodiscard]] std::string format() const;
};

/// The engine's error channel. The core is exception-free by convention
/// (AGENTS.md); exceptions appear only at the IO boundary.
template <class T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message,
                                                 int line = 0) {
  return std::unexpected(Error{code, std::move(message), line});
}

}  // namespace cb
