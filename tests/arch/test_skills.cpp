// SPDX-License-Identifier: GPL-3.0-or-later
// Project skills are checked like code: a skill that names a command that does
// not exist, or a path that does not exist, is worse than no skill at all,
// because it sends its reader confidently in the wrong direction (M0.7).
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace fs = std::filesystem;

namespace {

const fs::path kRoot{CB_SOURCE_DIR};

std::string readFile(const fs::path& p) {
  std::ifstream in(p);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<fs::path> skillFiles() {
  std::vector<fs::path> out;
  const fs::path dir = kRoot / ".claude" / "skills";
  if (!fs::exists(dir)) return out;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (e.is_directory() && fs::exists(e.path() / "SKILL.md")) {
      out.push_back(e.path() / "SKILL.md");
    }
  }
  return out;
}

/// Extract a scalar field from the YAML frontmatter block.
std::string frontmatterField(const std::string& text, const std::string& key) {
  if (!text.starts_with("---")) return {};
  const std::size_t end = text.find("\n---", 3);
  if (end == std::string::npos) return {};
  const std::string fm = text.substr(0, end);
  const std::regex re(key + R"(:\s*(.+))");
  std::smatch m;
  if (std::regex_search(fm, m, re)) {
    std::string v = m[1].str();
    while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
    return v;
  }
  return {};
}

}  // namespace

TEST_CASE("at least the M0 skills exist", "[arch]") {
  const auto files = skillFiles();
  std::set<std::string> names;
  for (const fs::path& f : files) names.insert(f.parent_path().filename().string());
  for (const char* want : {"cb-tdd-step", "cb-gate", "cb-new-module", "cb-adr"}) {
    CAPTURE(want);
    REQUIRE(names.contains(want));
  }
}

TEST_CASE("every skill has valid frontmatter naming itself", "[arch]") {
  const auto files = skillFiles();
  REQUIRE_FALSE(files.empty());
  for (const fs::path& f : files) {
    const std::string dirName = f.parent_path().filename().string();
    const std::string text = readFile(f);
    CAPTURE(dirName);
    const std::string name = frontmatterField(text, "name");
    const std::string desc = frontmatterField(text, "description");
    REQUIRE(name == dirName);
    REQUIRE(desc.size() > 40);           // a description that says nothing helps nobody
    REQUIRE(dirName.starts_with("cb-")); // the prefix convention (docs/plan/skills.md)
  }
}

TEST_CASE("every repo path a skill names actually exists", "[arch]") {
  // The failure mode this prevents: a skill confidently telling its reader to
  // edit a file that was renamed six commits ago.
  const std::regex pathRe(R"((?:src|tests|tools|docs|bench|variants|cmake)/[A-Za-z0-9_./-]+)");
  std::vector<std::string> missing;
  for (const fs::path& f : skillFiles()) {
    const std::string text = readFile(f);
    for (auto it = std::sregex_iterator(text.begin(), text.end(), pathRe);
         it != std::sregex_iterator(); ++it) {
      std::string p = it->str();
      // Skip placeholders ("src/<layer>/", "docs/adr/NNNN-", "test_") and paths
      // that are really suffixes of a build-directory path.
      if (p.find('<') != std::string::npos) continue;
      const auto pos = static_cast<std::size_t>(it->position(0));
      if (pos > 0 && text[pos - 1] == '/') continue;
      while (!p.empty() && (p.back() == '.' || p.back() == ',')) p.pop_back();
      if (p.ends_with('_') || p.ends_with('-')) continue;
      if (!fs::exists(kRoot / p)) {
        missing.push_back(f.parent_path().filename().string() + " -> " + p);
      }
    }
  }
  CAPTURE(missing);
  REQUIRE(missing.empty());
}

TEST_CASE("every CMake preset a skill names is declared", "[arch]") {
  const std::string presets = readFile(kRoot / "CMakePresets.json");
  REQUIRE_FALSE(presets.empty());
  const std::regex presetRe(R"(--preset ([a-z-]+))");
  std::vector<std::string> unknown;
  for (const fs::path& f : skillFiles()) {
    const std::string text = readFile(f);
    for (auto it = std::sregex_iterator(text.begin(), text.end(), presetRe);
         it != std::sregex_iterator(); ++it) {
      const std::string name = (*it)[1].str();
      if (presets.find("\"" + name + "\"") == std::string::npos) {
        unknown.push_back(f.parent_path().filename().string() + " -> " + name);
      }
    }
  }
  CAPTURE(unknown);
  REQUIRE(unknown.empty());
}
