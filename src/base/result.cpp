// SPDX-License-Identifier: GPL-3.0-or-later
#include "base/result.hpp"

namespace cb {

std::string_view toString(ErrorCode c) noexcept {
  switch (c) {
    case ErrorCode::ParseError: return "parse error";
    case ErrorCode::ValidationError: return "validation error";
    case ErrorCode::OutOfRange: return "out of range";
    case ErrorCode::Unsupported: return "unsupported";
    case ErrorCode::BudgetExceeded: return "budget exceeded";
    case ErrorCode::Internal: return "internal error";
  }
  return "unknown error";
}

std::string Error::format() const {
  std::string out;
  out += toString(code);
  if (line > 0) {
    out += " at line ";
    out += std::to_string(line);
  }
  out += ": ";
  out += message;
  return out;
}

}  // namespace cb
