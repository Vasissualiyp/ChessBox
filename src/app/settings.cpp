// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/settings.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace cb::app {
namespace {

std::string trim(std::string_view s) {
  while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) != 0))
    s.remove_prefix(1);
  while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) != 0))
    s.remove_suffix(1);
  return std::string(s);
}

bool asBool(const std::string& v) {
  return v == "true" || v == "1" || v == "yes";
}

float asFloat(const std::string& v, float fallback) {
  try {
    return std::stof(v);
  } catch (...) {
    return fallback;
  }
}

const char* boolText(bool b) {
  return b ? "true" : "false";
}

}  // namespace

std::filesystem::path Settings::defaultPath() {
  if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path(xdg) / "chessbox" / "settings.conf";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path(home) / ".config" / "chessbox" / "settings.conf";
  }
  return std::filesystem::path("chessbox-settings.conf");
}

Settings Settings::load(const std::filesystem::path& path) {
  Settings s;
  std::ifstream in(path);
  if (!in) return s;  // no file yet: defaults, not an error

  std::string line;
  while (std::getline(in, line)) {
    const auto hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;  // a malformed line is skipped, not fatal
    const std::string key = trim(std::string_view(line).substr(0, eq));
    const std::string value = trim(std::string_view(line).substr(eq + 1));
    if (key.empty()) continue;

    if (key == "gui_scale")
      s.guiScale = asFloat(value, s.guiScale);
    else if (key == "fullscreen")
      s.fullscreen = asBool(value);
    else if (key == "vsync")
      s.vsync = asBool(value);
    else if (key == "show_legal_moves")
      s.showLegalMoves = asBool(value);
    else if (key == "show_last_move")
      s.showLastMove = asBool(value);
    else if (key == "show_check")
      s.showCheck = asBool(value);
    else if (key == "show_seams")
      s.showSeams = asBool(value);
    else if (key == "show_coordinates")
      s.showCoordinates = asBool(value);
    else if (key == "piece_height_scale")
      s.pieceHeightScale = asFloat(value, s.pieceHeightScale);
    else if (key == "piece_icons")
      s.pieceIcons = value;
    else if (key == "theme")
      s.theme = value;
    else if (key == "flat_view")
      s.flatView = asBool(value);
    else if (key == "animate_moves")
      s.animateMoves = asBool(value);
    else if (key == "animation_speed")
      s.animationSpeed = asFloat(value, s.animationSpeed);
    else if (key == "overture_speed")
      s.overtureSpeed = asFloat(value, s.overtureSpeed);
    else if (key == "orbit_sensitivity")
      s.orbitSensitivity = asFloat(value, s.orbitSensitivity);
    else if (key == "zoom_sensitivity")
      s.zoomSensitivity = asFloat(value, s.zoomSensitivity);
    else if (key == "invert_orbit_y")
      s.invertOrbitY = asBool(value);
    else if (key == "camera_mode")
      s.cameraMode = value;
    else if (key == "follow_strength")
      s.followStrength = asFloat(value, s.followStrength);
    else if (key == "confirm_moves")
      s.confirmMoves = asBool(value);
    else if (key == "hot_seat")
      s.hotSeat = asBool(value);
    else if (key == "auto_promote_to")
      s.autoPromoteTo = value;
    else if (key == "last_variant")
      s.lastVariant = value;
    else if (key == "volume_master")
      s.volumeMaster = asFloat(value, s.volumeMaster);
    else if (key == "volume_music")
      s.volumeMusic = asFloat(value, s.volumeMusic);
    else if (key == "volume_effects")
      s.volumeEffects = asFloat(value, s.volumeEffects);
    // An unrecognised key is ignored: a file written by a newer build must still load.
  }
  s.sanitize();
  return s;
}

Result<void> Settings::save(const std::filesystem::path& path) const {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream out(path, std::ios::trunc);
  if (!out) {
    return fail(ErrorCode::Internal, "cannot write settings to '" + path.string() + "'");
  }
  out << "# ChessBox settings. Edited by the game; safe to edit by hand.\n";
  out << "gui_scale = " << guiScale << '\n';
  out << "fullscreen = " << boolText(fullscreen) << '\n';
  out << "vsync = " << boolText(vsync) << '\n';
  out << "show_legal_moves = " << boolText(showLegalMoves) << '\n';
  out << "show_last_move = " << boolText(showLastMove) << '\n';
  out << "show_check = " << boolText(showCheck) << '\n';
  out << "show_seams = " << boolText(showSeams) << '\n';
  out << "show_coordinates = " << boolText(showCoordinates) << '\n';
  out << "piece_height_scale = " << pieceHeightScale << '\n';
  out << "piece_icons = " << pieceIcons << '\n';
  out << "theme = " << theme << '\n';
  out << "flat_view = " << boolText(flatView) << '\n';
  out << "animate_moves = " << boolText(animateMoves) << '\n';
  out << "animation_speed = " << animationSpeed << '\n';
  out << "overture_speed = " << overtureSpeed << '\n';
  out << "orbit_sensitivity = " << orbitSensitivity << '\n';
  out << "zoom_sensitivity = " << zoomSensitivity << '\n';
  out << "invert_orbit_y = " << boolText(invertOrbitY) << '\n';
  out << "camera_mode = " << cameraMode << '\n';
  out << "follow_strength = " << followStrength << '\n';
  out << "confirm_moves = " << boolText(confirmMoves) << '\n';
  out << "hot_seat = " << boolText(hotSeat) << '\n';
  out << "auto_promote_to = " << autoPromoteTo << '\n';
  out << "last_variant = " << lastVariant << '\n';
  out << "volume_master = " << volumeMaster << '\n';
  out << "volume_music = " << volumeMusic << '\n';
  out << "volume_effects = " << volumeEffects << '\n';
  if (!out) return fail(ErrorCode::Internal, "writing settings failed part-way");
  return {};
}

void Settings::sanitize() {
  guiScale = std::clamp(guiScale, 0.6f, 3.0f);
  pieceHeightScale = std::clamp(pieceHeightScale, 0.0f, 2.0f);
  animationSpeed = std::clamp(animationSpeed, 0.25f, 4.0f);
  overtureSpeed = std::clamp(overtureSpeed, 0.25f, 4.0f);
  orbitSensitivity = std::clamp(orbitSensitivity, 0.1f, 4.0f);
  zoomSensitivity = std::clamp(zoomSensitivity, 0.1f, 4.0f);
  volumeMaster = std::clamp(volumeMaster, 0.0f, 1.0f);
  volumeMusic = std::clamp(volumeMusic, 0.0f, 1.0f);
  volumeEffects = std::clamp(volumeEffects, 0.0f, 1.0f);
  followStrength = std::clamp(followStrength, 0.0f, 1.0f);
  if (cameraMode != "off" && cameraMode != "piece" && cameraMode != "route") {
    cameraMode = "off";
  }
  if (lastVariant.empty()) lastVariant = "standard";
  if (pieceIcons != "faceted" && pieceIcons != "primitive") pieceIcons = "faceted";
  if (theme != "manifold" && theme != "console") theme = "manifold";
}

}  // namespace cb::app
