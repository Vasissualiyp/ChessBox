// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/variant_doc.hpp"

#include <sstream>

#include <toml++/toml.hpp>

namespace cb {

struct VariantDoc::Impl {
  toml::table table;
};

VariantDoc::VariantDoc() = default;
VariantDoc::VariantDoc(VariantDoc&&) noexcept = default;
VariantDoc& VariantDoc::operator=(VariantDoc&&) noexcept = default;
VariantDoc::~VariantDoc() = default;

Result<VariantDoc> VariantDoc::parse(std::string_view text, std::string_view sourceName) {
  VariantDoc doc;
  doc.impl_ = std::make_unique<Impl>();
  try {
    doc.impl_->table = toml::parse(text, sourceName);
  } catch (const toml::parse_error& e) {
    return fail(ErrorCode::ParseError, std::string(e.description()),
                static_cast<int>(e.source().begin.line));
  }
  return doc;
}

std::string VariantDoc::serialize() const {
  std::ostringstream out;
  // toml++'s own formatter, so the writer and the parser agree by construction rather
  // than by a hand-rolled emitter that drifts from the dialect the loader accepts.
  out << toml::toml_formatter{impl_->table};
  return out.str();
}

}  // namespace cb
