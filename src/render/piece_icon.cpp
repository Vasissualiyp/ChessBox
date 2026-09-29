// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/piece_icon.hpp"

#include <array>

namespace cb::render {
namespace {

using P = IconPoint;

// ---------------------------------------------------------------------------
// Faceted. Straight segments throughout, with a shared base trapezoid so the whole
// set stands on the same line - which is most of what makes a set look like a set.
// ---------------------------------------------------------------------------
constexpr std::array<P, 4> kBaseWide{{{19, 93}, {81, 93}, {72, 80}, {28, 80}}};
constexpr std::array<P, 4> kBaseMid{{{20, 93}, {80, 93}, {71, 80}, {29, 80}}};
constexpr std::array<P, 4> kBaseNarrow{{{22, 93}, {78, 93}, {70, 80}, {30, 80}}};

constexpr std::array<P, 4> kDomeBody{{{31, 80}, {39, 55}, {61, 55}, {69, 80}}};
constexpr std::array<P, 4> kDomeCollar{{{35, 55}, {65, 55}, {61, 48}, {39, 48}}};
constexpr std::array<P, 8> kDomeHead{
    {{50, 18}, {60, 23}, {64, 33}, {60, 43}, {50, 48}, {40, 43}, {36, 33}, {40, 23}}};

constexpr std::array<P, 4> kTowerShaft{{{28, 80}, {33, 43}, {67, 43}, {72, 80}}};
constexpr std::array<P, 12> kTowerCrown{{{27, 43},
                                         {73, 43},
                                         {73, 20},
                                         {63, 20},
                                         {63, 30},
                                         {55, 30},
                                         {55, 20},
                                         {45, 20},
                                         {45, 30},
                                         {37, 30},
                                         {37, 20},
                                         {27, 20}}};
constexpr std::array<P, 4> kTowerSlot{{{44, 84}, {56, 84}, {56, 72}, {44, 72}}};

constexpr std::array<P, 12> kWedgeHead{{{31, 80},
                                        {36, 58},
                                        {26, 50},
                                        {33, 35},
                                        {45, 25},
                                        {42, 14},
                                        {57, 19},
                                        {71, 30},
                                        {73, 45},
                                        {60, 53},
                                        {63, 66},
                                        {67, 80}}};
constexpr std::array<P, 4> kWedgeEye{{{54, 33}, {60, 36}, {55, 39}, {51, 36}}};

constexpr std::array<P, 4> kSpireShaft{{{33, 80}, {40, 52}, {60, 52}, {67, 80}}};
constexpr std::array<P, 4> kSpireCollar{{{36, 52}, {64, 52}, {60, 45}, {40, 45}}};
constexpr std::array<P, 5> kSpireMitre{{{50, 12}, {63, 33}, {59, 45}, {41, 45}, {37, 33}}};

constexpr std::array<P, 4> kCrownShaft{{{32, 80}, {35, 53}, {65, 53}, {68, 80}}};
constexpr std::array<P, 4> kCrownCollar{{{30, 53}, {70, 53}, {70, 45}, {30, 45}}};
constexpr std::array<P, 9> kCrownPoints{{{29, 45},
                                         {71, 45},
                                         {75, 19},
                                         {64, 32},
                                         {58, 15},
                                         {50, 29},
                                         {42, 15},
                                         {36, 32},
                                         {25, 19}}};

constexpr std::array<P, 4> kMonoShaft{{{33, 80}, {36, 53}, {64, 53}, {67, 80}}};
constexpr std::array<P, 4> kMonoBlock{{{31, 53}, {69, 53}, {65, 25}, {35, 25}}};
constexpr std::array<P, 4> kMonoCrossV{{{46, 4}, {54, 4}, {54, 21}, {46, 21}}};
constexpr std::array<P, 4> kMonoCrossH{{{37, 9}, {63, 9}, {63, 16}, {37, 16}}};

constexpr std::array<P, 4> kHornShaft{{{34, 80}, {41, 56}, {59, 56}, {66, 80}}};
constexpr std::array<P, 5> kHornSpike{{{50, 9}, {62, 47}, {56, 56}, {44, 56}, {38, 47}}};
constexpr std::array<P, 4> kHornRing{{{41, 38}, {59, 38}, {58, 33}, {42, 33}}};

constexpr std::array<P, 4> kSlabShaft{{{33, 80}, {38, 22}, {62, 22}, {67, 80}}};
constexpr std::array<P, 4> kSlabBand{{{36, 40}, {64, 40}, {64, 34}, {36, 34}}};

// ---------------------------------------------------------------------------
// Primitive. What made the old icons ugly was proportion, not economy: a knight was a
// bare triangle as wide as the whole square. These keep the economy - two or three
// convex shapes, eight points at most - and fix the proportions.
// ---------------------------------------------------------------------------
constexpr std::array<P, 4> kPBaseWide{{{24, 90}, {76, 90}, {68, 74}, {32, 74}}};
constexpr std::array<P, 4> kPBaseNarrow{{{26, 90}, {74, 90}, {66, 74}, {34, 74}}};

constexpr std::array<P, 5> kPDome{{{50, 26}, {64, 40}, {58, 66}, {42, 66}, {36, 40}}};
constexpr std::array<P, 12> kPTower{{{30, 74},
                                     {30, 26},
                                     {40, 26},
                                     {40, 38},
                                     {46, 38},
                                     {46, 26},
                                     {54, 26},
                                     {54, 38},
                                     {60, 38},
                                     {60, 26},
                                     {70, 26},
                                     {70, 74}}};
constexpr std::array<P, 6> kPWedge{
    {{34, 74}, {30, 42}, {52, 18}, {72, 32}, {62, 52}, {66, 74}}};
constexpr std::array<P, 5> kPSpire{{{50, 16}, {68, 62}, {58, 74}, {42, 74}, {32, 62}}};
constexpr std::array<P, 7> kPCrown{
    {{32, 74}, {28, 26}, {40, 44}, {50, 22}, {60, 44}, {72, 26}, {68, 74}}};
constexpr std::array<P, 4> kPMonoBlock{{{34, 74}, {34, 34}, {66, 34}, {66, 74}}};
constexpr std::array<P, 4> kPMonoCrossV{{{45, 10}, {55, 10}, {55, 30}, {45, 30}}};
constexpr std::array<P, 4> kPMonoCrossH{{{36, 16}, {64, 16}, {64, 24}, {36, 24}}};
constexpr std::array<P, 5> kPHorn{{{50, 12}, {62, 58}, {56, 74}, {44, 74}, {38, 58}}};
constexpr std::array<P, 4> kPSlab{{{36, 74}, {38, 24}, {62, 24}, {64, 74}}};

// Tables of outlines, one entry per archetype that is a piece.
constexpr std::array<IconPoly, 4> kFDome{kBaseNarrow, kDomeBody, kDomeCollar, kDomeHead};
constexpr std::array<IconPoly, 3> kFTower{kBaseWide, kTowerShaft, kTowerCrown};
constexpr std::array<IconPoly, 1> kFTowerCut{kTowerSlot};
constexpr std::array<IconPoly, 2> kFWedge{kBaseNarrow, kWedgeHead};
constexpr std::array<IconPoly, 1> kFWedgeCut{kWedgeEye};
constexpr std::array<IconPoly, 4> kFSpire{kBaseNarrow, kSpireShaft, kSpireCollar,
                                          kSpireMitre};
constexpr std::array<IconPoly, 4> kFCrown{kBaseMid, kCrownShaft, kCrownCollar,
                                          kCrownPoints};
constexpr std::array<IconPoly, 5> kFMono{kBaseMid, kMonoShaft, kMonoBlock, kMonoCrossV,
                                         kMonoCrossH};
constexpr std::array<IconPoly, 3> kFHorn{kBaseNarrow, kHornShaft, kHornSpike};
constexpr std::array<IconPoly, 1> kFHornCut{kHornRing};
constexpr std::array<IconPoly, 3> kFSlab{kBaseNarrow, kSlabShaft, kSlabBand};

constexpr std::array<IconPoly, 2> kPDomeSet{kPBaseNarrow, kPDome};
constexpr std::array<IconPoly, 2> kPTowerSet{kPBaseWide, kPTower};
constexpr std::array<IconPoly, 2> kPWedgeSet{kPBaseWide, kPWedge};
constexpr std::array<IconPoly, 2> kPSpireSet{kPBaseNarrow, kPSpire};
constexpr std::array<IconPoly, 2> kPCrownSet{kPBaseWide, kPCrown};
constexpr std::array<IconPoly, 4> kPMonoSet{kPBaseWide, kPMonoBlock, kPMonoCrossV,
                                            kPMonoCrossH};
constexpr std::array<IconPoly, 2> kPHornSet{kPBaseNarrow, kPHorn};
constexpr std::array<IconPoly, 2> kPSlabSet{kPBaseNarrow, kPSlab};

}  // namespace

std::string_view iconStyleName(IconStyle s) noexcept {
  return s == IconStyle::Primitive ? "primitive" : "faceted";
}

IconStyle iconStyleFromName(std::string_view name) noexcept {
  return name == "primitive" ? IconStyle::Primitive : IconStyle::Faceted;
}

PieceIcon pieceIcon(IconStyle style, Archetype shape) {
  const bool plain = style == IconStyle::Primitive;
  switch (shape) {
    case Archetype::Dome:
      return plain ? PieceIcon{kPDomeSet, {}} : PieceIcon{kFDome, {}};
    case Archetype::Tower:
      return plain ? PieceIcon{kPTowerSet, {}} : PieceIcon{kFTower, kFTowerCut};
    case Archetype::Wedge:
      return plain ? PieceIcon{kPWedgeSet, {}} : PieceIcon{kFWedge, kFWedgeCut};
    case Archetype::Spire:
      return plain ? PieceIcon{kPSpireSet, {}} : PieceIcon{kFSpire, {}};
    case Archetype::Crown:
      return plain ? PieceIcon{kPCrownSet, {}} : PieceIcon{kFCrown, {}};
    case Archetype::Monolith:
      return plain ? PieceIcon{kPMonoSet, {}} : PieceIcon{kFMono, {}};
    case Archetype::Horn:
      return plain ? PieceIcon{kPHornSet, {}} : PieceIcon{kFHorn, kFHornCut};
    case Archetype::Cell:
    case Archetype::Portal:
    case Archetype::Count:
      return {};
  }
  // An archetype nobody anticipated still gets a piece rather than nothing.
  return plain ? PieceIcon{kPSlabSet, {}} : PieceIcon{kFSlab, {}};
}

}  // namespace cb::render
