// SPDX-License-Identifier: GPL-3.0-or-later
//
// The library screen's twelve overtures.
//
// Each is a pure function of progress, which is the one decision the rest of this file
// follows from: reverse playback is not a second animation but a falling `t`, and a
// screenshot of a given `t` is the same picture every time. Nothing here reads a clock,
// keeps state between frames, or asks the engine anything - the pieces are authored
// coordinates, because a menu that called movegen to draw itself would make choosing a
// variant depend on the variant being playable.
#include "render/overture_scene.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

#include "position/position.hpp"
#include "render/quintic.hpp"
#include "render/ui_widgets.hpp"
#include "variant/variant.hpp"
#include "view/layout.hpp"
#include "view/move_anim.hpp"
#include "view/seams.hpp"

namespace cb::render {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kTau = 2.0f * kPi;
/// The board's world extent. A cell is one unit, always - that is what makes a cut a
/// cut rather than a rescale.
constexpr float kW = 8.0f;
constexpr float kH = 8.0f;

/// Which Mobius overture plays, and how hard the strip version stretches the board.
///
/// `true`  the board becomes a long ribbon that is bent and given a half-turn into the
///         classical strip: the shape reads as one.
/// `false` the original: the files first roll into a tube, the tube fails to close the
///         rank flip, and it opens out into a band. The band's loop is only as wide as
///         the board, so it reads as a fat ring rather than a ribbon.
///
/// Both are topologically the same gluing. Flip and rebuild to compare them.
constexpr bool kMobiusStrip = true;
/// How much longer than wide the ribbon becomes. Area is preserved - the file axis is
/// multiplied by this and the rank axis divided by it - so the cells become long
/// rectangles, which is the price of a legible strip and is paid deliberately. Not larger
/// than this: the loop radius is `8 * kMobiusStretch / (2 pi)`, and once the ribbon is
/// much narrower than that the twist stops being visible from any camera.
constexpr float kMobiusStretch = 3.0f;

float clampf(float v, float a, float b) {
  return v < a ? a : (v > b ? b : v);
}
float lerpf(float a, float b, float t) {
  return a + (b - a) * t;
}
/// Progress of a sub-phase: 0 before `a`, 1 after `b`. Every beat in every overture is
/// carved out of [0,1] with this, so no scene can accidentally depend on frame order.
float seg(float t, float a, float b) {
  return clampf((t - a) / (b - a), 0.0f, 1.0f);
}
float ease(float t) {
  return t * t * (3.0f - 2.0f * t);
}
float easeIn(float t) {
  return t * t;
}
float fi(int i) {
  return static_cast<float>(i);
}

/// sin(x)/x, with the removable singularity filled in.
///
/// Here for one job: the mean of `cos` over a symmetric arc. Bending a board into an arc
/// of angle `A` and radius `R` leaves the shape's centroid `R * (1 - sinc(A/2))` away
/// from where the flat board's centre was, so subtracting exactly that keeps the *shape*
/// in the middle of the pane instead of the point it grew from - the reason a torus
/// used to form off to one side. It has to be this and not a constant, because the
/// correction must vanish as the arc flattens, and `R` runs to infinity at exactly the
/// rate the angle runs to zero.
float sinc(float x) {
  return std::abs(x) < 1e-4f ? 1.0f : std::sin(x) / x;
}

OvVec3 add(OvVec3 a, OvVec3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
OvVec3 sub(OvVec3 a, OvVec3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
OvVec3 mul(OvVec3 a, float s) {
  return {a.x * s, a.y * s, a.z * s};
}
OvVec3 mix(OvVec3 a, OvVec3 b, float t) {
  return {lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t)};
}
OvVec3 cross(OvVec3 a, OvVec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
OvVec3 normalise(OvVec3 a) {
  const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
  return l < 1e-6f ? OvVec3{0.0f, 1.0f, 0.0f} : mul(a, 1.0f / l);
}

/// A surface, as a map from normalised lattice coordinates. `u` runs along the files and
/// `v` along the ranks, both 0 to 1, so the same builder serves every geometry.
using Pos = std::function<OvVec3(float, float)>;

// ---------------------------------------------------------------------------
// The warps.
// ---------------------------------------------------------------------------

/// Options for `tube`, the workhorse: cylinder, torus and the atomic torus are all this
/// function with different arguments, which is the same "the next case is data" claim
/// the engine makes about variants.
struct TubeOpt {
  float th{0};     ///< 0..2pi, how far the files have rolled (2pi closes the tube)
  float ph{0};     ///< 0..2pi, how far the tube has bent into a ring
  float tau{0};    ///< half-turn of the cross-section around the ring
  float open{1};   ///< ring-radius multiplier; see the horn-torus note below
  float evert{0};  ///< 0..1, how far the surface has been turned through itself
};

/// Roll the files into a tube whose axis runs along the ranks, then optionally bend that
/// axis into a ring.
///
/// A flat 8x8 glued into a torus has equal circumferences, so the honest result is a
/// *horn* torus with no hole at all. `open` scales the ring radius to pull the hole
/// open, which is a legibility cheat and is stated rather than hidden: the real ratio is
/// 1 : 1.
OvVec3 tube(float u, float v, const TubeOpt& o) {
  const float xl = (u - 0.5f) * kW;
  float zl = (v - 0.5f) * kH;
  // An open tube cannot be turned inside out by any rotation - it has two rims and a
  // rotation keeps them where they are - so it is turned the way a sock is: the far rim
  // rolls back down over the outside, and the fold travels the whole length as `evert`
  // runs 0 to 1. `swell` is how far clear of the tube the returning sleeve stands, and
  // the softened absolute value is what makes the turn a fold rather than a crease.
  float swell = 0.0f;
  if (o.evert > 1e-4f && o.ph <= 1e-4f) {
    const float m = zl + kH * 0.5f;            // 0 at the near rim, kH at the far one
    const float fold = (1.0f - o.evert) * kH;  // where the material turns back
    const float k = 0.5f;                      // the fold's own radius
    const float d = m - fold;
    const float soft = std::sqrt(d * d + k * k) - k;
    zl = (fold - soft) - kH * 0.5f;
    swell = 0.62f * 0.5f * (1.0f + d / std::sqrt(d * d + k * k));
  }
  float a = xl;
  float b = 0.0f;
  if (o.th > 1e-4f) {
    const float R = kW / o.th;
    const float t = xl / R;
    a = (R + swell) * std::sin(t);
    // Centred on the tube rather than on the seam the tube was rolled from.
    b = (R + swell) * std::cos(t) - R * sinc(o.th * 0.5f);
  }
  if (o.ph > 1e-4f) {
    // A closed ring *can* be turned inside out by a rotation, and this is the one: the
    // ring radius is swept through zero to its own negative, which carries the cross
    // section round the axis and leaves what was the inner equator on the outside. The
    // way through is the spindle torus, where the hole has closed to a point - which is
    // exactly the moment a player sees the inside come out.
    const float base = kH / o.ph;
    const float Rr = base * o.open * (1.0f - 2.0f * o.evert);
    const float p = zl / base;
    const float chi = o.tau * p * 0.5f;
    const float a2 = a * std::cos(chi) - b * std::sin(chi);
    const float b2 = a * std::sin(chi) + b * std::cos(chi);
    // And centred on the ring, not on the point of it the flat board became.
    const float ringMid = Rr * (1.0f - sinc(o.ph * 0.5f));
    return {Rr * (1.0f - std::cos(p)) + a2 * std::cos(p) - ringMid, b2,
            Rr * std::sin(p) - a2 * std::sin(p)};
  }
  return {a, b, zl};
}

/// Bend the files into a loop in the board's own plane, with the rank extent sticking
/// out as the strip's width, optionally rotating that width as it travels round.
///
/// This is the only embedding that can absorb a *rank* flip. A tube's transverse
/// direction runs parallel to its axis and has no way to reverse itself, which is why
/// the Moebius overture has to leave the tube to close its seam - and why watching it
/// fail is the most useful thing in that overture.
OvVec3 band(float u, float v, float th, float tau, float evert = 0.0f) {
  const float xl = (u - 0.5f) * kW;
  const float zl = (v - 0.5f) * kH;
  if (th <= 1e-4f) return {xl, 0.0f, zl};
  const float R = kW / th;
  const float t = xl / R;
  const float Rp = R * (1.0f - 2.0f * evert);  // through the middle of its own loop
  const float psi = tau * t * 0.5f;
  return {Rp * std::sin(t) + zl * std::cos(psi) * std::sin(t), zl * std::sin(psi),
          Rp * std::cos(t) - Rp * sinc(th * 0.5f) + zl * std::cos(psi) * std::cos(t)};
}

/// The classical Mobius strip, as a long ribbon.
///
/// `band` is the same family, but its loop radius is fixed at `kW / th` while its width
/// stays `kH`, so on an 8 x 8 board the loop is no wider than the board and the result
/// reads as a fat ring. Here the two axes are given their own extents: `len` is the file
/// axis, stretched into the ribbon's length, and `wid` is the rank axis, narrowed into
/// its width. The ribbon is then bent (`th` sweeps 0 to 2pi) and given a distributed
/// half-turn
/// (`tau` sweeps 0 to 1), which is how a paper strip is made.
///
/// At `th = 2pi, tau = 1` the two ends meet with the width reversed - `stripSurface(0,
/// v)` and `stripSurface(1, 1 - v)` are the same point - which is this variant's gluing
/// exactly: the files periodic, the ranks flipped.
OvVec3 stripSurface(float u, float v, float len, float wid, float th, float tau,
                    float evert = 0.0f) {
  const float s = (u - 0.5f) * len;
  const float w = (v - 0.5f) * wid;
  if (th <= 1e-4f) return {s, 0.0f, w};  // still flat, before the bend
  const float R = len / th;
  const float t = s / R;
  // The loop radius is swept through zero to its negative while the *angle* keeps
  // running off the unchanged arc length, so the ribbon is drawn through the middle of
  // its own loop and comes back with its width on the other side. The band is one-sided
  // to begin with; what the turn moves is which way the ribbon faces the room.
  const float Rp = R * (1.0f - 2.0f * evert);
  const float psi = tau * t * 0.5f;
  const float cw = std::cos(psi);
  return {Rp * std::sin(t) + w * cw * std::sin(t), w * std::sin(psi),
          Rp * std::cos(t) - Rp * sinc(th * 0.5f) + w * cw * std::cos(t)};
}

/// Where on the figure-eight cross-section a file sits, as an angle.
///
/// (sin a, sin 2a) is a lemniscate, and a *very* unevenly parametrised one: the second
/// coordinate runs round twice while the first runs round once, so eight equally spaced
/// values of `a` land in pairs - the chord from one file to the next alternates between
/// under a third of a unit and over two. On a menu that is a wobble; on a board it is a
/// row of squares of two quite different sizes, which is what the geometry view made
/// impossible to ignore. Walking the curve at constant speed instead spaces the files
/// evenly and moves not one point of the shape: it changes only which parameter names
/// which point.
///
/// Both symmetries the gluing needs survive, which is the whole reason this is safe: the
/// speed is even in `a`, so the arc length is odd in `a`, so `u -> 1 - u` still maps to
/// `a -> -a` - the file reversal the rank seam closes with - and one lap of `u` is still
/// one lap of `a`.
float lemniscateAngle(float u) {
  // Cumulative arc length over a in [-pi, pi], built once. A table rather than a series:
  // the integrand has no elementary antiderivative, and 512 steps of it is exact to far
  // more than a pixel.
  static const std::array<float, 513> kArc = [] {
    std::array<float, 513> arc{};
    float total = 0.0f;
    for (std::size_t i = 1; i < arc.size(); ++i) {
      const float a0 = (static_cast<float>(i - 1) / 512.0f - 0.5f) * kTau;
      const float a1 = (static_cast<float>(i) / 512.0f - 0.5f) * kTau;
      const auto speed = [](float a) {
        const float dx = std::cos(a);
        const float dy = 2.0f * std::cos(2.0f * a);
        return std::sqrt(dx * dx + dy * dy);
      };
      total += 0.5f * (speed(a0) + speed(a1)) * (a1 - a0);
      arc[i] = total;
    }
    for (float& x : arc) x /= total;  // 0 at a = -pi, 1 at a = +pi
    return arc;
  }();
  // `u` is a fraction of the way round the curve; find the angle that far along it.
  const float target = u - std::floor(u);
  const auto it = std::lower_bound(kArc.begin(), kArc.end(), target);
  const std::size_t hi = std::max<std::size_t>(
      1, std::min<std::size_t>(kArc.size() - 1,
                               static_cast<std::size_t>(it - kArc.begin())));
  const float span = kArc[hi] - kArc[hi - 1];
  const float frac = span > 1e-9f ? (target - kArc[hi - 1]) / span : 0.0f;
  const float idx = (static_cast<float>(hi - 1) + frac) / 512.0f;
  return (idx - 0.5f) * kTau;
}

/// The Klein bottle, as the figure-eight immersion.
///
/// A circular cross-section **cannot** close this gluing. `klein` joins the ranks with
/// `flip = ["file"]`, so after one trip round the ring the cross-section has to come
/// back to itself *reflected*; rotating a circle by a half-turn brings it back shifted
/// by four files instead, which is a different surface and leaves the two rims visibly
/// failing to meet.
///
/// A figure-eight can. Rotating the lemniscate (sin a, sin 2a) by pi gives
/// (-sin a, -sin 2a) = (sin -a, sin -2a), which is exactly a -> -a: the file reversed.
/// So the cross-section pinches first, and then both seams close to machine precision.
OvVec3 kleinSurf(float u, float v, float th, float pinch, float ph, float tau, float open,
                 float evert = 0.0f) {
  const float xl = (u - 0.5f) * kW;
  const float zl = (v - 0.5f) * kH;
  float cr = xl;
  float ca = 0.0f;
  if (th > 1e-4f) {
    const float R = kW / th;
    const float a = xl / R;
    cr = R * std::sin(a);
    ca = R * std::cos(a) - R * sinc(th * 0.5f);
  }
  if (pinch > 1e-4f) {
    // Comfortably smaller than the ring it will travel round, or the bottle closes
    // into a disc and the self-intersection - the whole point - has nowhere to show.
    const float rho = (kW / kTau) * 1.05f;
    const float a = lemniscateAngle(u);
    cr = lerpf(cr, rho * std::sin(a), pinch);
    ca = lerpf(ca, rho * std::sin(2.0f * a), pinch);
  }
  if (ph > 1e-4f) {
    const float base = kH / ph;
    // Swept through zero to its negative, the bottle is pulled through its own neck and
    // comes back with the inside out - the same turn a torus makes, on a surface that
    // had no outside to begin with.
    const float Rr = base * open * (1.0f - 2.0f * evert);
    const float p = zl / base;
    const float chi = tau * p * 0.5f;
    const float r2 = cr * std::cos(chi) - ca * std::sin(chi);
    const float a2 = cr * std::sin(chi) + ca * std::cos(chi);
    const float ringMid = Rr * (1.0f - sinc(ph * 0.5f));
    return {Rr * (1.0f - std::cos(p)) + r2 * std::cos(p) - ringMid, a2,
            Rr * std::sin(p) - r2 * std::sin(p)};
  }
  return {cr, ca, zl};
}

/// `tube`, for a 4x4 board and with a thickness: one extra parameter, `shell`, which is
/// a level's HEIGHT above the board while the board is flat and its RADIUS once the board
/// is rolled. One function, therefore, for the whole of TORUS3D - no blending between two
/// embeddings, and the flat box falls out of it as th = ph = 0.
///
/// T^3 does not embed in three dimensions, and this is exactly how far it gets: two of
/// the three gluings give T^2, the level axis becomes the radial direction, and the four
/// levels come out as four *nested* shells about one core circle. The third gluing would
/// have to join the outermost shell to the innermost, which is not a thing space can do -
/// and that failure is the point of the beat.
OvVec3 shellTube(float u, float v, float th, float ph, float d, float open) {
  constexpr float kE = 4.0f;  // a 4 x 4 board
  const float xl = (u - 0.5f) * kE;
  const float zl = (v - 0.5f) * kE;
  float a = xl;
  float b = d;  // unrolled, `shell` is just a height
  if (th > 1e-4f) {
    const float R = kE / th;
    const float q = xl / R;
    a = (R + d) * std::sin(q);
    b = (R + d) * std::cos(q) - R * sinc(th * 0.5f);
  }
  if (ph > 1e-4f) {
    const float base = kE / ph;
    const float rr = base * open;
    const float p = zl / base;
    const float ringMid = rr * (1.0f - sinc(ph * 0.5f));
    return {rr * (1.0f - std::cos(p)) + a * std::cos(p) - ringMid, b,
            rr * std::sin(p) - a * std::sin(p)};
  }
  return {a, b, zl};
}

Pos flatBoard() {
  return [](float u, float v) -> OvVec3 { return {u * kW - 4.0f, 0.0f, v * kH - 4.0f}; };
}

/// The surface normal, by finite difference. Cheaper to write than to derive for four
/// warps, and a piece only needs to stand up straight.
///
/// The cross product is taken rank-tangent by file-tangent, in that order, and the order
/// is the whole of it: the other way round gives -Y for the flat board, which puts every
/// piece underneath the board it is standing on and makes the entire scene read as seen
/// from below.
OvVec3 normalAt(const Pos& pos, float u, float v) {
  constexpr float e = 0.004f;
  const OvVec3 o = pos(u, v);
  return normalise(cross(sub(pos(u, std::min(v + e, 1.0f)), o),
                         sub(pos(std::min(u + e, 1.0f), v), o)));
}

// ---------------------------------------------------------------------------
// Builders.
// ---------------------------------------------------------------------------

struct GridOpt {
  int nx{8};
  int nz{8};
  /// How many sub-quads a cell is broken into per side. One planar quad per cell facets
  /// a curved surface badly - a torus of 64 flat tiles reads as a polyhedron - so a
  /// warped geometry asks for 4 and gets a surface that bends. A cell keeps *one*
  /// colour across its sub-quads: the subdivision carries curvature, not a finer chequer.
  int sub{1};
  float fade{1.0f};
  float inset{0.03f};
  std::function<float(int, int)> keep;
  std::function<OvTone(int, int)> tone;
};

void addGrid(OvertureScene& s, const Pos& pos, const GridOpt& o) {
  for (int f = 0; f < o.nx; ++f) {
    for (int r = 0; r < o.nz; ++r) {
      const float k = o.keep ? o.keep(f, r) : 1.0f;
      if (k <= 0.001f) continue;
      const OvTone tn =
          o.tone ? o.tone(f, r) : (((f + r) % 2 != 0) ? OvTone::Dark : OvTone::Light);
      const float u0 = (fi(f) + o.inset) / fi(o.nx);
      const float u1 = (fi(f) + 1.0f - o.inset) / fi(o.nx);
      const float v0 = (fi(r) + o.inset) / fi(o.nz);
      const float v1 = (fi(r) + 1.0f - o.inset) / fi(o.nz);
      const int sd = std::max(1, o.sub);
      for (int a = 0; a < sd; ++a) {
        for (int b = 0; b < sd; ++b) {
          const float ua = lerpf(u0, u1, fi(a) / fi(sd));
          const float ub = lerpf(u0, u1, fi(a + 1) / fi(sd));
          const float va = lerpf(v0, v1, fi(b) / fi(sd));
          const float vb = lerpf(v0, v1, fi(b + 1) / fi(sd));
          OvQuad q;
          q.p[0] = pos(ua, va);
          q.p[1] = pos(ub, va);
          q.p[2] = pos(ub, vb);
          q.p[3] = pos(ua, vb);
          q.tone = tn;
          q.fade = o.fade * k;
          s.quads.push_back(q);
        }
      }
    }
  }
}

/// Where a cell's centre lands on a surface laid out over `nx` by `nz` cells.
OvVec3 cellCentre(const Pos& pos, int f, int r, int nx, int nz) {
  return pos((fi(f) + 0.5f) / fi(nx), (fi(r) + 0.5f) / fi(nz));
}

struct TokenOpt {
  int nx{8};
  int nz{8};
  float fade{1.0f};
  float height{1.0f};
  bool mirrored{false};
  bool at{false};  ///< true when `where` overrides the cell centre
  OvVec3 where;
  /// A piece placed somewhere other than a cell centre needs the normal of *that* place.
  /// Without this it took cell a1's, so a rook riding round a torus stood up as though
  /// it had never left the corner it started in.
  bool hasNormal{false};
  OvVec3 normal;
};

void addToken(OvertureScene& s, const Pos& pos, int f, int r, char glyph, bool white,
              const TokenOpt& o) {
  if (o.fade <= 0.02f) return;
  OvToken tk;
  tk.at = o.at ? o.where : cellCentre(pos, f, r, o.nx, o.nz);
  tk.normal = o.hasNormal
                  ? o.normal
                  : normalAt(pos, (fi(f) + 0.5f) / fi(o.nx), (fi(r) + 0.5f) / fi(o.nz));
  tk.glyph = glyph;
  tk.white = white;
  tk.height = o.height;
  tk.fade = o.fade;
  tk.mirrored = o.mirrored;
  s.tokens.push_back(tk);
}

constexpr char kBack[8]{'R', 'N', 'B', 'Q', 'K', 'B', 'N', 'R'};

/// The opening array, on any surface - so a pawn morphs with the cell it stands on
/// rather than being reprojected after the fact.
void addArmy(OvertureScene& s, const Pos& pos, float fade) {
  if (fade <= 0.02f) return;
  TokenOpt o;
  o.fade = fade;
  for (int f = 0; f < 8; ++f) {
    addToken(s, pos, f, 0, kBack[f], true, o);
    addToken(s, pos, f, 1, 'P', true, o);
    addToken(s, pos, f, 6, 'P', false, o);
    addToken(s, pos, f, 7, kBack[f], false, o);
  }
}

/// A polyline that follows a surface, lifted clear of it, so a trail crossing a seam
/// goes round the shape rather than through it.
OvTrail trailOn(const Pos& pos, float u0, float v0, float u1, float v1, int n,
                view::Rgba colour, float lift = 0.28f) {
  OvTrail tr;
  tr.colour = colour;
  tr.pts.reserve(static_cast<std::size_t>(n) + 1);
  for (int i = 0; i <= n; ++i) {
    const float s = fi(i) / fi(n);
    const float u = lerpf(u0, u1, s);
    const float v = lerpf(v0, v1, s);
    const float uw = u - std::floor(u);
    const float vw = v - std::floor(v);
    tr.pts.push_back(add(pos(uw, vw), mul(normalAt(pos, uw, vw), lift)));
  }
  return tr;
}

void addRim(OvertureScene& s, const Pos& pos, view::Rgba colour, float width,
            float fade) {
  OvTrail tr;
  tr.colour = colour;
  tr.width = width;
  tr.fade = fade;
  tr.pts = {pos(0.0f, 0.0f), pos(1.0f, 0.0f), pos(1.0f, 1.0f), pos(0.0f, 1.0f),
            pos(0.0f, 0.0f)};
  s.trails.push_back(tr);
}

/// A wire box as SIX polylines rather than twelve loose segments: two closed face loops
/// and four risers. At T6's tangle, where sixty-odd boxes interpenetrate, loose segments
/// read as a haze of parallel lines - and the one thing that picture has to say is that
/// these are boxes landing inside one another.
///
/// `lo` and `hi` are the fundamental domain's opposite corners; `c` is one translate
/// away. `full` adds the top loop and the risers, while a faint edge- or corner-neighbour
/// copy gets the bottom loop alone, which places it while keeping the draw count down.
void addWireBox(OvertureScene& s, const OvVec3& lo, const OvVec3& hi, const OvVec3& c,
                bool full, view::Rgba tone, float width, float fade) {
  static constexpr float kLoopLo[5][3]{
      {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {0, 0, 0}};
  static constexpr float kLoopHi[5][3]{
      {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}, {0, 1, 0}};
  static constexpr int kRisers[4][2]{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  const auto corner = [&](float x, float y, float z) {
    return OvVec3{x * hi.x + (1.0f - x) * lo.x, y * hi.y + (1.0f - y) * lo.y,
                  z * hi.z + (1.0f - z) * lo.z};
  };
  const auto loop = [&](const float (&pts)[5][3]) {
    OvTrail tr;
    tr.colour = tone;
    tr.width = width;
    tr.fade = fade;
    for (const auto& pt : pts) {
      tr.pts.push_back(add(corner(pt[0], pt[1], pt[2]), c));
    }
    s.trails.push_back(tr);
  };
  loop(kLoopLo);
  if (!full) return;
  loop(kLoopHi);
  for (const auto& rz : kRisers) {
    OvTrail tr;
    tr.colour = tone;
    tr.width = width;
    tr.fade = fade;
    tr.pts = {add(corner(fi(rz[0]), 0.0f, fi(rz[1])), c),
              add(corner(fi(rz[0]), 1.0f, fi(rz[1])), c)};
    s.trails.push_back(tr);
  }
}

view::Rgba seamColour(const view::Theme& th, int k, int n);

/// The six faces of the fundamental domain, as unit-cube corners: each pair, both ends of
/// one identification. Drawn, they are exactly what the geometry layer stores for a glued
/// box - three periodic identifications over a flat lattice - rather than a shape.
struct PortalFace {
  int k;
  float c[4][3];
};
constexpr PortalFace kPortalFaces[6]{
    {0, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}},
    {0, {{1, 0, 0}, {1, 0, 1}, {1, 1, 1}, {1, 1, 0}}},
    {1, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}},
    {1, {{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}}},
    {2, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}},
    {2, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}},
};

/// Each glued pair of faces takes one hue off the seam ramp: file, rank, level. `n` is
/// how many axes the board has, so the hue is the portal's own and not a fixed one.
void addPortalFaces(OvertureScene& s, const view::Theme& th, const OvVec3& lo,
                    const OvVec3& hi, int n, float fillFade, float edgeFade) {
  if (fillFade <= 0.02f && edgeFade <= 0.02f) return;
  for (const PortalFace& fc : kPortalFaces) {
    const view::Rgba c = seamColour(th, fc.k, n);
    OvQuad q;
    for (int j = 0; j < 4; ++j) {
      q.p[j] = {fc.c[j][0] > 0.5f ? hi.x : lo.x, fc.c[j][1] > 0.5f ? hi.y : lo.y,
                fc.c[j][2] > 0.5f ? hi.z : lo.z};
    }
    q.tone = OvTone::Light;
    q.hasColour = true;
    q.colour = c;
    q.fade = fillFade;
    s.quads.push_back(q);
    OvTrail tr;
    tr.colour = c;
    tr.width = 2.0f;
    tr.fade = edgeFade;
    tr.pts = {q.p[0], q.p[1], q.p[2], q.p[3], q.p[0]};
    s.trails.push_back(tr);
  }
}

/// A deterministic pseudo-random in [0,1). A still at a given `t` has to be the same
/// still every time it is taken, so the drifting figures on the quintic cannot use a
/// clock or a running seed - the same rule `deco.cpp` keeps with `nextFloat`.
float rndFloat(int i) {
  const float x = std::sin(fi(i) * 127.1f + 311.7f) * 43758.5453f;
  return x - std::floor(x);
}

/// One surface blended into another, for the T6 tangle's collapse onto the quintic: each
/// copy's own cell lattice is lerped onto the sheet it becomes, so the sheet is not a new
/// object arriving but the copy flattening.
Pos blendPos(const Pos& a, const Pos& b, float m) {
  return [a, b, m](float u, float v) -> OvVec3 { return mix(a(u, v), b(u, v), m); };
}

/// The exact bits of a `(u, v)` sample, as a cache key. Exact rather than quantised: the
/// sample lattice is deterministic, so the same corner recomputes the same floats every
/// frame and a hit is exact, not an approximation.
std::uint64_t uvBits(float u, float v) {
  const std::uint64_t hi = std::bit_cast<std::uint32_t>(u);
  const std::uint64_t lo = std::bit_cast<std::uint32_t>(v);
  return (hi << 32) | lo;
}

/// Patch (k1, k2) of the Calabi-Yau quintic as a surface in the overture's world units.
/// The mixed imaginary part is the vertical, which is the orientation the standard
/// picture is always shown in. Same formula as the main menu - `render/quintic.hpp` - so
/// the T6 overture lands on the object the player has been looking at since the game
/// started.
///
/// Memoised (M4.9). A patch is a fixed function of `(k1, k2, u, v)`; the samples repeat
/// across sub-quads within a frame and across frames, so the second evaluation of a point
/// is a lookup instead of two `pow` and two `atan2`. The cache cannot change a frame - it
/// returns the value the formula returns - which is why it is safe under the purity rule.
constexpr float kQuinticScale = 5.2f;
Pos quinPatch(int k1, int k2) {
  // One cache per patch, shared by every copy of the returned surface. Bounded by the
  // patch count and the fixed sample lattice `grid` walks.
  static std::array<std::unordered_map<std::uint64_t, OvVec3>, kQuinticN * kQuinticN>
      caches;
  auto& cache = caches[static_cast<std::size_t>(k1 * kQuinticN + k2)];
  return [&cache, k1, k2](float u, float v) -> OvVec3 {
    const std::uint64_t key = uvBits(u, v);
    const auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const std::array<float, 3> q = quinticPoint(k1, k2, u * kPi * 0.5f, -1.0f + 2.0f * v);
    const OvVec3 p{q[0] * kQuinticScale, q[2] * kQuinticScale, q[1] * kQuinticScale};
    cache.emplace(key, p);
    return p;
  };
}

// ---------------------------------------------------------------------------
// The cut.
//
// Dropping whole files and ranks, outermost first, alternating so the cuts can be
// counted. The survivors keep their own cell size and their own place in the lattice:
// scaling a board down to n units wide reads as someone laying a smaller board on top of
// the big one, which is not what happens to a board when a variant has fewer files. What
// is left is therefore the a1 corner block, and it is deliberately off-centre until it
// slides.
// ---------------------------------------------------------------------------

float cutFraction(int n, int f, int r, float t) {
  const int steps = (8 - n) * 2;
  if (steps <= 0) return 1.0f;
  int idx = -1;
  for (int k = 0; k < steps; ++k) {
    const int ring = 7 - k / 2;
    if (k % 2 == 0 ? (f == ring) : (r == ring)) {
      idx = k;
      break;
    }
  }
  if (idx < 0) return 1.0f;  // a survivor
  const float per = 1.0f / fi(steps);
  return 1.0f - clampf((t - fi(idx) * per) / per, 0.0f, 1.0f);
}

/// The surviving n x n block, in the original lattice's coordinates, slid towards the
/// centre by `shift` (0 = where the cells actually were, 1 = centred).
Pos cornerPos(int n, float shift, float y) {
  const float off = (4.0f - fi(n) * 0.5f) * shift;
  const float nf = fi(n);
  return [nf, off, y](float u, float v) -> OvVec3 {
    return {u * nf - 4.0f + off, y, v * nf - 4.0f + off};
  };
}

/// The doomed part of the board: the full 8 x 8 with the survivors suppressed, because
/// those are drawn by `cornerPos` and are the ones allowed to move.
void addCutGrid(OvertureScene& s, int n, float t) {
  GridOpt g;
  g.keep = [n, t](int f, int r) {
    return (f < n && r < n) ? 0.0f : cutFraction(n, f, r, t);
  };
  addGrid(s, flatBoard(), g);
}

/// The 8 x 8 army going with the cells it was standing on.
void addCutArmy(OvertureScene& s, int n, float t, const Pos& survivors, float leave) {
  const Pos flat = flatBoard();
  for (int f = 0; f < 8; ++f) {
    for (const int r : {0, 1, 6, 7}) {
      const bool doomed = !(f < n && r < n);
      const float k = doomed ? cutFraction(n, f, r, t) : leave;
      if (k <= 0.02f) continue;
      const char g = (r == 1 || r == 6) ? 'P' : kBack[f];
      const bool white = r <= 1;
      TokenOpt o;
      o.fade = k;
      if (doomed) {
        addToken(s, flat, f, r, g, white, o);
      } else {
        o.nx = n;
        o.nz = n;
        addToken(s, survivors, f, r, g, white, o);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Palette access. Seam hues come from the theme's own ramp, so the overtures cannot
// drift out of agreement with the board about what a portal looks like.
// ---------------------------------------------------------------------------

/// The pose every overture opens and closes on.
///
/// Not a default - a *guarantee*. Each overture reaches its own camera by t = 0.12 and
/// comes back to this one, so the flat 8 x 8 that all of them pass through is the same
/// picture from the same angle. Without it, leaving one overture and arriving at the
/// next is a cut rather than a hand-over, and the cycle stops reading as a loop.
constexpr OvCamera kOpenCam{-0.06f, 1.44f, 8.2f, 0.10f};

view::Rgba seamColour(const view::Theme& th, int k, int n) {
  return view::seamRampColor(th, n <= 1 ? 0.0f : fi(k) / fi(n - 1));
}

// ===========================================================================
// The twelve.
// ===========================================================================

OvertureScene sceneStandard(const view::Theme& th, float t, bool intro) {
  OvertureScene s;
  s.cam = {lerpf(-0.12f, 0.34f, ease(t)), lerpf(1.45f, 0.70f, ease(t)),
           lerpf(8.2f, 9.0f, ease(t)), 0.10f};
  const Pos pos = flatBoard();
  // On a later cycle the board is already there and the whole range is the game.
  const float build = intro ? seg(t, 0.0f, 0.35f) : 1.0f;
  const float rise = intro ? seg(t, 0.35f, 0.58f) : 1.0f;
  const float mv = intro ? seg(t, 0.58f, 1.0f) : t;

  if (intro) {
    GridOpt g;
    g.keep = [build](int f, int r) {
      return clampf(build * 2.2f - fi(f + r) / 14.0f, 0.0f, 1.0f);
    };
    addGrid(
        s,
        [build](float u, float v) -> OvVec3 {
          const int f = static_cast<int>(u * 8.0f);
          const int r = static_cast<int>(v * 8.0f);
          const float d = clampf(build * 2.05f - fi(f + r) / 14.0f, 0.0f, 1.0f);
          return {u * kW - 4.0f, (1.0f - ease(d)) * 7.0f, v * kH - 4.0f};
        },
        g);
  } else {
    addGrid(s, pos, GridOpt{});
  }

  // 1. e4 e5 2. Nf3 - authored coordinates, never generated.
  struct Glide {
    float f{0}, r{0}, lift{0};
  };
  const auto glide = [mv](float f0, float r0, float f1, float r1, float a, float b,
                          bool arc) {
    const float u = ease(seg(mv, a, b));
    return Glide{lerpf(f0, f1, u), lerpf(r0, r1, u),
                 arc ? std::sin(u * kPi) * 1.5f : 0.0f};
  };
  const Glide e2 = glide(4, 1, 4, 3, 0.05f, 0.32f, false);
  const Glide e7 = glide(4, 6, 4, 4, 0.36f, 0.63f, false);
  const Glide g1 = glide(6, 0, 5, 2, 0.67f, 0.95f, true);
  const float up = clampf(rise * 1.8f, 0.0f, 1.0f);

  TokenOpt o;
  o.fade = up;
  for (int f = 0; f < 8; ++f) {
    if (f != 4) {
      addToken(s, pos, f, 1, 'P', true, o);
      addToken(s, pos, f, 6, 'P', false, o);
    }
    if (f != 6) addToken(s, pos, f, 0, kBack[f], true, o);
    addToken(s, pos, f, 7, kBack[f], false, o);
  }
  const auto moved = [&](const Glide& g, char glyph, bool white) {
    TokenOpt m;
    m.fade = up;
    m.at = true;
    m.where = {g.f + 0.5f - 4.0f, g.lift, g.r + 0.5f - 4.0f};
    addToken(s, pos, 0, 0, glyph, white, m);
  };
  moved(e2, 'P', true);
  moved(e7, 'P', false);
  moved(g1, 'N', true);
  (void)th;  // the standard board spends no seam or event colour: it is the reference
  s.caption = !intro      ? "1. e4 e5 2. Nf3"
              : t < 0.35f ? "sixty-four cells"
              : t < 0.58f ? "the army stands"
                          : "1. e4 e5 2. Nf3";
  return s;
}

OvertureScene sceneCylinder(const view::Theme& th, float t) {
  OvertureScene s;
  s.cam = {lerpf(-0.10f, 0.62f, ease(t)), lerpf(1.40f, 0.42f, ease(t)),
           lerpf(8.2f, 6.4f, ease(t)), 0.12f};
  const float roll = ease(seg(t, 0.16f, 0.74f));
  TubeOpt o;
  o.th = roll * kTau;
  const Pos pos = [o](float u, float v) { return tube(u, v, o); };
  GridOpt g;
  g.sub = roll > 0.02f ? 4 : 1;
  addGrid(s, pos, g);
  addArmy(s, pos, 1.0f);

  // The two ends of one identification share a hue: a seam is coloured by the portal it
  // belongs to, not by being a seam, which is what makes a cylinder read as one gradient
  // repeated.
  const float name = seg(t, 0.0f, 0.16f);
  if (name > 0.01f) {
    const view::Rgba c = seamColour(th, 0, 1);
    for (const float u : {0.0005f, 0.9995f}) {
      OvTrail tr;
      tr.colour = c;
      tr.width = 3.2f;
      tr.fade = name;
      for (int i = 0; i <= 8; ++i) tr.pts.push_back(pos(u, fi(i) / 8.0f));
      s.trails.push_back(tr);
    }
  }
  // The rank axis keeps its walls, so the rims stay drawn and unglued - that is the
  // whole difference between this and the torus.
  for (const float v : {0.0005f, 0.9995f}) {
    OvTrail tr;
    tr.colour = th.boneFaint;
    tr.width = 1.6f;
    tr.fade = 0.55f + 0.45f * roll;
    for (int i = 0; i <= 24; ++i) tr.pts.push_back(pos(fi(i) / 24.0f, v));
    s.trails.push_back(tr);
  }
  const float ride = seg(t, 0.74f, 1.0f);
  if (ride > 0.0f) {
    const float u = 0.5f + ease(ride) * 0.999f;
    OvTrail tr = trailOn(pos, 0.5f, 0.44f, u, 0.44f, 40, seamColour(th, 0, 1));
    tr.width = 2.6f;
    s.trails.push_back(tr);
    const float uw = u - std::floor(u);
    TokenOpt tk;
    tk.at = true;
    tk.where = pos(uw, 0.44f);
    tk.hasNormal = true;
    tk.normal = normalAt(pos, uw, 0.44f);
    addToken(s, pos, 0, 0, 'R', true, tk);
  }
  s.caption = t < 0.16f   ? "one portal, two ends"
              : t < 0.74f ? "the files roll into a loop"
                          : "off h, onto a, all the way round";
  return s;
}

OvertureScene sceneTorus(const view::Theme& th, float t) {
  OvertureScene s;
  s.cam = {lerpf(-0.08f, 0.72f, ease(t)), lerpf(1.42f, 0.60f, ease(t)),
           lerpf(8.2f, 5.6f, ease(seg(t, 0.35f, 1.0f))), 0.12f};
  const float pr = ease(seg(t, 0.48f, 0.86f));
  TubeOpt o;
  o.th = ease(seg(t, 0.12f, 0.48f)) * kTau;
  o.ph = pr * kTau;
  o.open = lerpf(1.0f, 2.2f, pr);
  const Pos pos = [o](float u, float v) { return tube(u, v, o); };
  GridOpt g;
  g.sub = std::max(ease(seg(t, 0.12f, 0.48f)), pr) > 0.02f ? 4 : 1;
  addGrid(s, pos, g);
  addArmy(s, pos, 1.0f);

  const float name = seg(t, 0.0f, 0.12f) * (1.0f - ease(seg(t, 0.5f, 0.86f)));
  if (name > 0.01f) {
    for (int axis = 0; axis < 2; ++axis) {
      const view::Rgba c = seamColour(th, axis, 2);
      for (const float e : {0.0005f, 0.9995f}) {
        OvTrail tr;
        tr.colour = c;
        tr.width = 3.0f;
        tr.fade = name;
        for (int i = 0; i <= 16; ++i) {
          const float q = fi(i) / 16.0f;
          tr.pts.push_back(axis == 1 ? pos(q, e) : pos(e, q));
        }
        s.trails.push_back(tr);
      }
    }
  }
  const float ride = seg(t, 0.86f, 1.0f);
  if (ride > 0.0f) {
    const float q = ease(ride);
    OvTrail tr =
        trailOn(pos, 0.19f, 0.19f, 0.19f + q, 0.19f + q, 60, seamColour(th, 0, 2));
    tr.width = 2.6f;
    s.trails.push_back(tr);
    const float u = std::fmod(0.19f + q, 1.0f);
    TokenOpt tk;
    tk.at = true;
    tk.where = pos(u, u);
    tk.hasNormal = true;
    tk.normal = normalAt(pos, u, u);
    addToken(s, pos, 0, 0, 'Q', true, tk);
    // The four old corner cells, becoming ordinary interior cells: the plainest
    // statement of "no corners".
    for (const auto& c : {std::pair{0.06f, 0.06f}, std::pair{0.94f, 0.06f},
                          std::pair{0.06f, 0.94f}, std::pair{0.94f, 0.94f}}) {
      s.bursts.push_back({pos(c.first, c.second), 0.5f + std::sin(q * kPi) * 0.35f,
                          std::sin(q * kPi) * 0.8f, seamColour(th, 1, 2), false});
    }
  }
  s.caption = t < 0.12f   ? "two portals, four edges"
              : t < 0.48f ? "the files close"
              : t < 0.86f ? "the ranks close"
                          : "a loop home, without turning round";
  return s;
}

/// The original: a tube that cannot close the rank flip, so it opens into a band that
/// can.
OvertureScene sceneMobiusBand(const view::Theme& th, float t) {
  OvertureScene s;
  s.cam = {lerpf(-0.05f, 0.74f, ease(t)), lerpf(1.42f, 0.55f, ease(t)),
           lerpf(8.2f, 7.2f, ease(t)), 0.12f};
  const float roll = ease(seg(t, 0.0f, 0.30f));
  const float openOut = ease(seg(t, 0.44f, 0.64f));
  const float tw = ease(seg(t, 0.64f, 0.84f));
  const float th_ = roll * kTau;
  const Pos pos = [th_, openOut, tw](float u, float v) {
    TubeOpt o;
    o.th = th_;
    // One positional blend between two embeddings of the same gluing. It is the one
    // place an overture changes its mind about how to draw a surface, and it does so for
    // a reason the player can see: the tube has no way to reverse its own axis.
    return mix(tube(u, v, o), band(u, v, th_, tw), openOut);
  };
  GridOpt g;
  g.sub = std::max(roll, openOut) > 0.02f ? 4 : 1;
  addGrid(s, pos, g);
  addArmy(s, pos, 1.0f);

  // The seam tries to close and does not match: rank 1 has arrived opposite rank 8.
  const float bad = seg(t, 0.30f, 0.44f) * (1.0f - ease(seg(t, 0.44f, 0.60f)));
  if (bad > 0.02f) {
    OvTrail tr;
    tr.colour = th.blood;
    tr.width = 3.4f;
    tr.fade = bad;
    for (int i = 0; i <= 8; ++i) tr.pts.push_back(pos(0.9995f, fi(i) / 8.0f));
    s.trails.push_back(tr);
    for (int i = 0; i < 8; ++i) {
      s.bursts.push_back(
          {pos(0.9995f, (fi(i) + 0.5f) / 8.0f), 0.30f, bad * 0.9f, th.blood, true});
    }
  }
  if (tw > 0.05f) {
    OvTrail tr;
    tr.colour = seamColour(th, 0, 1);
    tr.width = 3.0f;
    tr.fade = tw;
    for (int i = 0; i <= 8; ++i) tr.pts.push_back(pos(0.9995f, fi(i) / 8.0f));
    s.trails.push_back(tr);
  }
  const float ride = seg(t, 0.84f, 1.0f);
  if (ride > 0.0f) {
    const float q = ease(ride);
    OvTrail tr = trailOn(pos, 0.12f, 0.28f, 0.12f + q, 0.28f, 50, th.ember);
    tr.width = 2.6f;
    s.trails.push_back(tr);
    const float mu = std::fmod(0.12f + q, 1.0f);
    TokenOpt tk;
    tk.at = true;
    tk.where = pos(mu, 0.28f);
    tk.hasNormal = true;
    tk.normal = normalAt(pos, mu, 0.28f);
    tk.mirrored = q > 0.5f;
    addToken(s, pos, 0, 0, 'B', true, tk);
  }
  s.caption = t < 0.30f   ? "the files roll, as ever"
              : t < 0.44f ? "and the seam does not match"
              : t < 0.64f ? "so the tube opens out"
              : t < 0.84f ? "and takes a half-turn"
                          : "home, and mirrored";
  return s;
}

/// The second Mobius option: the board stretched into a long ribbon, then bent and given
/// one half-turn into the classical strip. It exists because the band's loop is only as
/// wide as the board, so on an 8 x 8 it reads as a fat ring - "a circle, stretched a
/// little" - even though the topology was never in question. The price is long
/// rectangular cells, and it is paid deliberately: a legible ribbon is worth more here
/// than square cells, and the surface is subdivided so the bend and twist stay smooth.
OvertureScene sceneMobiusStrip(const view::Theme& th, float t) {
  OvertureScene s;
  const float stretch = ease(seg(t, 0.05f, 0.28f));
  const float bend = ease(seg(t, 0.28f, 0.70f));
  const float twist = ease(seg(t, 0.50f, 0.86f));
  const float ride = seg(t, 0.86f, 1.0f);
  const float len = kW * lerpf(1.0f, kMobiusStretch, stretch);
  const float wid = kH * lerpf(1.0f, 1.0f / kMobiusStretch, stretch);
  const float theta = bend * kTau;
  const Pos pos = [len, wid, theta, twist](float u, float v) {
    return stripSurface(u, v, len, wid, theta, twist);
  };
  // The shared opening pose, then round so the ring is seen from above and the twist,
  // which stands the ribbon on edge at the seam, is legible rather than edge-on.
  s.cam = {lerpf(-0.05f, 0.78f, ease(t)), lerpf(1.42f, 1.02f, ease(seg(t, 0.25f, 1.0f))),
           lerpf(8.2f, 12.0f, ease(seg(t, 0.2f, 1.0f))), 0.11f};
  GridOpt g;
  g.sub = 6;  // a bending, twisting ribbon facets badly out of one quad per cell
  addGrid(s, pos, g);
  // The army belongs to the square, not to the ribbon, so it steps aside as the board is
  // stretched - the same way the cube overtures' army leaves with its cells.
  addArmy(s, pos, 1.0f - ease(seg(t, 0.05f, 0.24f)));

  // The two long edges, a hue each. On a Mobius strip they are one edge, so the two hues
  // meet where the ends join - the single fact the whole variant turns on.
  const float edges = ease(seg(t, 0.18f, 0.45f));
  if (edges > 0.02f) {
    for (int e = 0; e < 2; ++e) {
      OvTrail tr;
      tr.colour = seamColour(th, e, 2);
      tr.width = 2.4f;
      tr.fade = edges;
      const float v = e == 0 ? 0.0015f : 0.9985f;
      for (int i = 0; i <= 64; ++i) tr.pts.push_back(pos(fi(i) / 64.0f, v));
      s.trails.push_back(tr);
    }
  }
  // The two short ends are one portal, so one hue. They are named while apart and fade as
  // they meet, because the join is a flip and not a straight meeting.
  const float ends = ease(seg(t, 0.42f, 0.62f)) * (1.0f - ease(seg(t, 0.80f, 0.90f)));
  if (ends > 0.02f) {
    for (const float u : {0.0005f, 0.9995f}) {
      OvTrail tr;
      tr.colour = seamColour(th, 0, 1);
      tr.width = 3.2f;
      tr.fade = ends;
      for (int i = 0; i <= 8; ++i) tr.pts.push_back(pos(u, fi(i) / 8.0f));
      s.trails.push_back(tr);
    }
  }
  // A bishop rides the ribbon once round. It goes in on one long edge and comes back on
  // the other, mirrored, because there is only one edge to come back on.
  if (ride > 0.0f) {
    const float q = ease(ride);
    constexpr float kStart = 0.12f;
    constexpr float kV0 = 0.25f;
    OvTrail tr;
    tr.colour = th.ember;
    tr.width = 2.6f;
    for (int i = 0; i <= 56; ++i) {
      float uu = kStart + fi(i) / 56.0f * q;
      float vv = kV0;
      // Crossing the seam keeps the piece on the same strip: the width reverses, which is
      // exactly what the half-turn does.
      if (uu > 1.0f) {
        uu -= 1.0f;
        vv = 1.0f - vv;
      }
      tr.pts.push_back(pos(uu, vv));
    }
    s.trails.push_back(tr);
    float uu = kStart + q;
    float vv = kV0;
    bool flipped = false;
    if (uu > 1.0f) {
      uu -= 1.0f;
      vv = 1.0f - vv;
      flipped = true;
    }
    TokenOpt tk;
    tk.at = true;
    tk.where = pos(uu, vv);
    tk.hasNormal = true;
    tk.normal = normalAt(pos, uu, vv);
    tk.mirrored = flipped;
    addToken(s, pos, 0, 0, 'B', true, tk);
  }
  s.caption = t < 0.05f   ? "the board is the reference"
              : t < 0.28f ? "stretched into a ribbon"
              : t < 0.50f ? "the ribbon curls into a ring"
              : t < 0.86f ? "and takes one half-turn, so its ends meet reversed"
                          : "one edge, ridden once, home mirrored";
  return s;
}

/// Which Mobius plays. Flipping `kMobiusStrip` and rebuilding is the whole switch.
OvertureScene sceneMobius(const view::Theme& th, float t) {
  if constexpr (kMobiusStrip) {
    return sceneMobiusStrip(th, t);
  }
  return sceneMobiusBand(th, t);
}

OvertureScene sceneKlein(const view::Theme& th, float t) {
  OvertureScene s;
  s.cam = {lerpf(-0.05f, 1.02f, ease(t)), lerpf(1.42f, 0.46f, ease(t)),
           lerpf(8.2f, 6.0f, ease(seg(t, 0.3f, 1.0f))), 0.12f};
  const float roll = ease(seg(t, 0.0f, 0.26f));
  const float bad = seg(t, 0.26f, 0.40f);
  const float pinch = ease(seg(t, 0.40f, 0.60f));
  const float pr = ease(seg(t, 0.60f, 0.86f));
  const float esc = seg(t, 0.86f, 1.0f);
  const Pos pos = [roll, pinch, pr](float u, float v) {
    return kleinSurf(u, v, roll * kTau, pinch, pr * kTau, pr, lerpf(1.0f, 3.6f, pr));
  };
  GridOpt g;
  g.sub = std::max(pinch, pr) > 0.02f ? 4 : 1;
  // Only the two cells the bishop leaves and arrives on. Lighting every light square on
  // the surface says "half of these are the same colour", which is true of any board;
  // lighting two says "it started on one of these and finished on the other", which is
  // the thing this variant can do and no other can.
  g.tone = [esc](int f, int r) {
    const bool from = f == 1 && r == 2;
    const bool to = f == 1 && r == 5;
    if (esc > 0.05f && (from || to)) return OvTone::Lit;
    return ((f + r) % 2 != 0) ? OvTone::Dark : OvTone::Light;
  };
  addGrid(s, pos, g);
  addArmy(s, pos, 1.0f);

  if (pr < 0.98f) {
    const view::Rgba c = seamColour(th, 1, 2);
    for (const float e : {0.0005f, 0.9995f}) {
      OvTrail tr;
      tr.colour = c;
      tr.width = 2.6f;
      tr.fade = 1.0f - pr * 0.55f;
      for (int i = 0; i <= 24; ++i) tr.pts.push_back(pos(fi(i) / 24.0f, e));
      s.trails.push_back(tr);
    }
  }
  // The refusal: a circle's half-turn lands file f opposite file f+4, which is a shift
  // and not the reflection the gluing asked for.
  const float showBad = bad * (1.0f - ease(seg(t, 0.40f, 0.52f)));
  if (showBad > 0.02f) {
    for (int f = 0; f < 8; ++f) {
      OvTrail tr;
      tr.colour = th.blood;
      tr.width = 1.6f;
      tr.fade = showBad * 0.7f;
      tr.dashed = true;
      tr.pts = {pos((fi(f) + 0.5f) / 8.0f, 0.0005f),
                pos((fi((f + 4) % 8) + 0.5f) / 8.0f, 0.9995f)};
      s.trails.push_back(tr);
    }
    s.bursts.push_back({pos(0.5f, 0.0005f), 0.5f, showBad, th.blood, true});
  }
  if (esc > 0.0f) {
    const float q = ease(esc);
    OvTrail tr = trailOn(pos, 0.19f, 0.31f, 0.19f, 0.31f + q, 56, th.ember);
    tr.width = 2.8f;
    s.trails.push_back(tr);
    const float kv = std::fmod(0.31f + q, 1.0f);
    TokenOpt tk;
    tk.at = true;
    tk.where = pos(0.19f, kv);
    tk.hasNormal = true;
    tk.normal = normalAt(pos, 0.19f, kv);
    addToken(s, pos, 0, 0, 'B', true, tk);
  }
  s.caption = t < 0.26f   ? "the files close, straight"
              : t < 0.40f ? "a circle comes back shifted, not reflected"
              : t < 0.60f ? "so the cross-section pinches"
              : t < 0.86f ? "and now the ring closes exactly"
                          : "a bishop leaves its colour";
  return s;
}

OvertureScene sceneMirrorbox(const view::Theme& th, float t) {
  OvertureScene s;
  s.cam = {lerpf(0.0f, 0.30f, ease(t)), lerpf(1.42f, 0.78f, ease(t)), 8.4f, 0.10f};
  const Pos pos = flatBoard();
  addGrid(s, pos, GridOpt{});
  addArmy(s, pos, 1.0f);

  const float rise = ease(seg(t, 0.0f, 0.30f));
  if (rise > 0.02f) {
    for (const float side : {-1.0f, 1.0f}) {
      const float x = side * kW * 0.5f;
      const float hgt = rise * 2.5f;
      OvQuad q;
      q.p[0] = {x, 0.0f, -kH * 0.5f};
      q.p[1] = {x, 0.0f, kH * 0.5f};
      q.p[2] = {x, hgt, kH * 0.5f};
      q.p[3] = {x, hgt, -kH * 0.5f};
      q.tone = OvTone::Mirror;
      q.fade = rise;
      s.quads.push_back(q);
    }
  }
  const float ray = seg(t, 0.30f, 0.84f);
  if (ray > 0.0f) {
    const int n = std::max(2, static_cast<int>(26.0f * ease(ray)));
    float x = -1.5f;
    float z = -3.5f;
    float dx = 1.0f;
    const float dz = 1.0f;
    OvTrail tr;
    tr.colour = th.ember;
    tr.width = 2.8f;
    tr.pts.push_back({x, 0.3f, z});
    for (int i = 0; i < n; ++i) {
      x += dx * 0.5f;
      z += dz * 0.5f;
      // A ray reaching a file wall bounces and continues in the mirrored direction,
      // exactly as light would - and nothing teleports, which is the claim.
      if (x > 4.0f) {
        x = 8.0f - x;
        dx = -dx;
        s.bursts.push_back({{4.0f, 0.3f, z}, 0.55f, 0.8f, th.mirrorEdge, false});
      } else if (x < -4.0f) {
        x = -8.0f - x;
        dx = -dx;
        s.bursts.push_back({{-4.0f, 0.3f, z}, 0.55f, 0.8f, th.mirrorEdge, false});
      }
      tr.pts.push_back({x, 0.3f, z});
    }
    s.trails.push_back(tr);
    TokenOpt tk;
    tk.at = true;
    tk.where = {tr.pts.back().x, 0.02f, tr.pts.back().z};
    addToken(s, pos, 0, 0, 'B', true, tk);

    // The unfolding: the same path, straight, drawn through the mirrors. Bounce and
    // straight line are one path, which is what makes the reflection legible.
    const float unf = seg(t, 0.84f, 1.0f);
    if (unf > 0.0f) {
      OvTrail st;
      st.colour = th.ember;
      st.width = 1.4f;
      st.fade = 0.45f * ease(unf);
      st.dashed = true;
      float sx = -1.5f;
      float sz = -3.5f;
      for (int i = 0; i <= n; ++i) {
        st.pts.push_back({sx, 0.3f, sz});
        sx += 0.5f;
        sz += 0.5f;
      }
      s.trails.push_back(st);
    }
  }
  s.caption = t < 0.30f   ? "two walls, silvered"
              : t < 0.84f ? "the ray bounces and keeps going"
                          : "bounce and straight line are one path";
  return s;
}

OvertureScene sceneCube5(const view::Theme& th, float t) {
  OvertureScene s;
  constexpr int N = 5;
  const float cut = ease(seg(t, 0.0f, 0.34f));
  const float shift = ease(seg(t, 0.34f, 0.44f));
  const float ext = ease(seg(t, 0.44f, 0.74f));
  const float gap = lerpf(0.0f, 1.65f, ext);
  s.cam = {lerpf(0.05f, 0.66f, ease(t)), lerpf(1.45f, 0.52f, ease(t)),
           lerpf(8.2f, 6.6f, ease(seg(t, 0.44f, 1.0f))), 0.11f};
  addCutGrid(s, N, cut);

  const auto lv = [&](int L) { return cornerPos(N, shift, (fi(L) - 2.0f) * gap); };
  for (int L = 0; L < N; ++L) {
    const float alive =
        L == 2 ? 1.0f
               : clampf(ext * 1.5f - (fi(std::abs(L - 2)) - 1.0f) * 0.28f, 0.0f, 1.0f);
    if (alive <= 0.01f) continue;
    GridOpt g;
    g.nx = N;
    g.nz = N;
    g.fade = alive * (L == 2 ? 1.0f : 0.92f);
    g.tone = [L](int f, int r) {
      return ((f + r + L) % 2 != 0) ? OvTone::Dark : OvTone::Light;
    };
    addGrid(s, lv(L), g);
  }
  if (ext < 0.35f) {
    addCutArmy(s, N, cut, lv(2), 1.0f - ease(clampf(ext * 2.4f, 0.0f, 1.0f)));
  }
  if (ext > 0.3f) {
    const float born = ease(seg(t, 0.56f, 0.72f));
    constexpr char kRoyal[N]{'R', 'N', 'K', 'N', 'R'};
    for (const auto& [L, white] : {std::pair{0, true}, std::pair{1, true},
                                   std::pair{3, false}, std::pair{4, false}}) {
      const Pos pL = lv(L);
      for (int f = 0; f < N; ++f) {
        const int r = white ? (L == 0 ? 0 : 1) : (L == 4 ? 4 : 3);
        const char g = (L == 0 || L == 4) ? kRoyal[f] : 'P';
        TokenOpt o;
        o.nx = N;
        o.nz = N;
        o.fade = born;
        addToken(s, pL, f, r, g, white, o);
      }
    }
  }
  // The extra axis is cold: an axis you cannot see in a plane is exactly the case the
  // seam ramp is reserved for. The pieces stay warm.
  if (ext > 0.05f) {
    for (int L = 0; L < N; ++L) {
      addRim(s, lv(L), seamColour(th, L, N), 1.3f, 0.5f * ext);
    }
  }
  const float move = seg(t, 0.74f, 1.0f);
  if (move > 0.0f) {
    const float q = ease(clampf(move / 0.62f, 0.0f, 1.0f));
    const Pos c0 = lv(1);
    const Pos c3 = lv(4);
    const OvVec3 a = c0(1.5f / fi(N), 1.5f / fi(N));
    const OvVec3 b = c3(4.5f / fi(N), 4.5f / fi(N));
    const OvVec3 now = mix(a, b, q);
    OvTrail tr;
    tr.colour = th.ember;
    tr.width = 2.8f;
    tr.pts = {a, now};
    s.trails.push_back(tr);
    TokenOpt tk;
    tk.nx = N;
    tk.nz = N;
    tk.at = true;
    tk.where = now;
    addToken(s, c0, 0, 0, 'U', true, tk);

    const float kn = seg(move, 0.62f, 1.0f);
    if (kn > 0.0f) {
      const float ks = ease(kn);
      const OvVec3 ka = lv(1)(0.5f / fi(N), 2.5f / fi(N));
      const OvVec3 kb = lv(3)(1.5f / fi(N), 2.5f / fi(N));
      OvTrail jt;
      jt.colour = seamColour(th, 2, N);
      jt.width = 2.2f;
      for (int i = 0; i <= 14; ++i) {
        const float p = fi(i) / 14.0f * ks;
        OvVec3 at = mix(ka, kb, p);
        at.y += std::sin(p * kPi) * 0.9f;
        jt.pts.push_back(at);
      }
      s.trails.push_back(jt);
      TokenOpt nt;
      nt.nx = N;
      nt.nz = N;
      nt.at = true;
      nt.where = jt.pts.back();
      addToken(s, lv(1), 0, 0, 'N', true, nt);
    }
  }
  s.caption = t < 0.34f   ? "three files and three ranks go"
              : t < 0.44f ? "what is left moves to the middle"
              : t < 0.74f ? "and gains a third axis"
                          : "the unicorn goes through the solid";
  return s;
}

OvertureScene sceneHyper4(const view::Theme& th, float t) {
  OvertureScene s;
  constexpr int N = 4;
  const float cut = ease(seg(t, 0.0f, 0.16f));
  const float shift = ease(seg(t, 0.16f, 0.24f));
  const float ext = ease(seg(t, 0.24f, 0.40f));
  const float hyp = ease(seg(t, 0.40f, 0.62f));
  const float unfold = ease(seg(t, 0.62f, 0.78f));
  const float gap = lerpf(0.0f, 1.5f, ext);
  // The four aeon slices unfold into a 2 x 2 arrangement, not a row. A row of four is a
  // thin strip, and once the fit has to hold the whole strip each cube is too small to
  // see a move on; two by two says the same thing about the fourth axis at twice the
  // size. Which cell of the arrangement a slice lands in is (A & 1, A >> 1) - the same
  // bit-split a 4-cube's own vertices use.
  const float aeonGap = 7.0f;
  const float spin = 0.55f + t * 0.9f;
  // Where the surviving block sits: at the a1 corner the cut left it in, sliding to the
  // middle as `shift` runs. The 4-cube maths needs the cell's *centred* coordinates to
  // nest correctly, so the corner offset is a translation applied to all three
  // placements rather than being folded into the coordinates themselves.
  const float corner = (fi(N) * 0.5f - 4.0f) * (1.0f - shift);
  s.cam = {lerpf(0.05f, 0.46f, ease(t)), lerpf(1.45f, 0.60f, ease(t)),
           lerpf(8.2f, 21.0f, ease(seg(t, 0.55f, 1.0f))), 0.09f};
  addCutGrid(s, N, cut);

  // Three placements for the same cell, blended by phase: the plain cube with w
  // ignored, the 4-cube projection, and the unfolded row. The fourth axis arrives the
  // way a 4-cube is always drawn - nested, scaled by w - and only afterwards pulls
  // apart into a row a player can read a move on. Same 256 cells throughout; only their
  // positions interpolate.
  const auto place = [=](float cx, float cy, float cz, float cw) -> OvVec3 {
    const OvVec3 cube{cx + corner, cy * gap, cz + corner};
    const float nx = cx * std::cos(spin) - cw * std::sin(spin);
    const float nw = cx * std::sin(spin) + cw * std::cos(spin);
    const float k = 2.35f / (3.5f - nw);
    const OvVec3 hyper{nx * k * 2.15f + corner, cy * k * 2.15f * 1.05f,
                       cz * k * 2.15f + corner};
    const int slice = static_cast<int>(std::lround(cw + 1.5f));
    const OvVec3 row{(fi(slice & 1) - 0.5f) * aeonGap + cx + corner, cy * gap,
                     (fi(slice >> 1) - 0.5f) * aeonGap + cz + corner};
    return mix(mix(cube, hyper, hyp * (1.0f - unfold)), row, unfold);
  };
  const auto cellPos = [&](int A, int L) -> Pos {
    return [=](float u, float v) {
      return place(u * fi(N) - 2.0f, fi(L) - 1.5f, v * fi(N) - 2.0f, fi(A) - 1.5f);
    };
  };
  const auto sliceAlive = [=](int A) {
    return A == 1 ? 1.0f : clampf(hyp * 1.7f - fi(std::abs(A - 1)) * 0.16f, 0.0f, 1.0f);
  };
  // Before the fourth axis arrives there is one slice, and it is the one the levels
  // were extruded on - A = 1, the slice `sliceAlive` keeps at full strength. Iterating
  // from zero drew the block that does not exist yet and left the cut board with nothing
  // standing on it.
  const bool fanned = hyp > 0.02f;
  const int firstSlice = fanned ? 0 : 1;
  const int lastSlice = fanned ? N - 1 : 1;
  for (int A = firstSlice; A <= lastSlice; ++A) {
    const float alive = sliceAlive(A);
    if (alive <= 0.01f) continue;
    for (int L = 0; L < N; ++L) {
      const float lAlive =
          L == 1 ? 1.0f
                 : clampf(ext * 1.5f - (fi(std::abs(L - 1)) - 1.0f) * 0.22f, 0.0f, 1.0f);
      if (lAlive <= 0.01f) continue;
      GridOpt g;
      g.nx = N;
      g.nz = N;
      // The cells step back while the figure is a hypercube: what has to read there is
      // the nesting, and 256 chequered tiles at full strength read as noise instead.
      g.fade = alive * lAlive * (A == 1 ? 1.0f : 0.86f) *
               lerpf(1.0f, 0.62f, hyp * (1.0f - unfold));
      g.tone = [L, A](int f, int r) {
        return ((f + r + L + A) % 2 != 0) ? OvTone::Dark : OvTone::Light;
      };
      addGrid(s, cellPos(A, L), g);
    }
  }
  if (ext < 0.4f) {
    addCutArmy(s, N, cut, cellPos(1, 1), 1.0f - ease(clampf(ext * 2.2f, 0.0f, 1.0f)));
  }
  if (ext > 0.08f) {
    for (int A = firstSlice; A <= lastSlice; ++A) {
      const float alive = sliceAlive(A);
      if (alive <= 0.01f) continue;
      for (int L = 0; L < N; ++L) {
        // The outer levels carry the shell's silhouette, so they are drawn hardest - it
        // is those boxes, nested, that make the figure legible as a 4-cube.
        const bool shell = L == 0 || L == N - 1;
        addRim(s, cellPos(A, L), seamColour(th, L, N), shell ? 2.0f : 1.0f,
               (shell ? 0.85f : 0.34f) * ext * alive);
      }
    }
  }
  // The w-edges: what makes the figure a hypercube rather than four cubes that happen
  // to be near each other, and what keeps the rook's slide from reading as a teleport.
  if (hyp > 0.05f) {
    for (int A = 0; A < N - 1; ++A) {
      const float alive = clampf(hyp * 1.7f - fi(std::abs(A - 1)) * 0.16f, 0.0f, 1.0f);
      for (const int L : {0, 3}) {
        for (const float cu : {0.0f, 1.0f}) {
          for (const float cv : {0.0f, 1.0f}) {
            OvTrail tr;
            tr.colour = seamColour(th, 3, N);
            tr.width = 1.6f;
            // The fourth-axis edges are the ones that are not really there, and saying
            // so is the whole reason a 4-cube is worth drawing - but they still have to
            // be visible enough to join the shells into one figure.
            tr.fade = 0.55f * alive;
            tr.dashed = unfold > 0.5f;
            tr.pts = {cellPos(A, L)(cu, cv), cellPos(A + 1, L)(cu, cv)};
            s.trails.push_back(tr);
          }
        }
      }
    }
  }
  if (ext > 0.3f && hyp > 0.2f) {
    const float born = ease(seg(t, 0.44f, 0.60f));
    const Pos p = cellPos(1, 0);
    constexpr char kRoyal[N]{'R', 'N', 'K', 'R'};
    for (int f = 0; f < N; ++f) {
      TokenOpt o;
      o.nx = N;
      o.nz = N;
      o.fade = born;
      o.height = 0.85f;
      addToken(s, p, f, 0, kRoyal[f], true, o);
      addToken(s, p, f, N - 1, kRoyal[f], false, o);
    }
  }
  const float show = seg(t, 0.78f, 1.0f);
  if (show > 0.0f) {
    const float q = ease(clampf(show / 0.5f, 0.0f, 1.0f));
    const OvVec3 a0 = place(-0.5f, -0.5f, 0.5f, -1.5f);
    const OvVec3 now = place(-0.5f, -0.5f, 0.5f, lerpf(0.0f, 3.0f, q) - 1.5f);
    OvTrail tr;
    tr.colour = th.ember;
    tr.width = 2.6f;
    tr.pts = {a0, now};
    s.trails.push_back(tr);
    TokenOpt tk;
    tk.nx = N;
    tk.nz = N;
    tk.at = true;
    tk.where = now;
    addToken(s, cellPos(0, 1), 0, 0, 'R', true, tk);

    const float fan = seg(show, 0.5f, 1.0f);
    if (fan > 0.0f) {
      // {1,2} on any two of four axes, both orders, both signs: the same expansion the
      // engine does, which is the claim the variant is making. Forty-eight before the
      // board's edges clip any.
      const int base[4]{1, 1, 1, 1};
      int seen = 0;
      for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
          if (i == j) continue;
          for (const int si : {-1, 1}) {
            for (const int sj : {-1, 1}) {
              int c[4]{base[0], base[1], base[2], base[3]};
              c[i] += si;
              c[j] += sj * 2;
              ++seen;
              if (c[0] < 0 || c[0] > 3 || c[1] < 0 || c[1] > 3 || c[2] < 0 || c[2] > 3 ||
                  c[3] < 0 || c[3] > 3) {
                continue;
              }
              s.bursts.push_back(
                  {place(fi(c[0]) + 0.5f - 2.0f, fi(c[2]) - 1.5f, fi(c[1]) + 0.5f - 2.0f,
                         fi(c[3]) - 1.5f),
                   0.30f, clampf(fan * 2.0f - fi(seen) / 96.0f, 0.0f, 1.0f) * 0.95f,
                   seamColour(th, 1, N), false});
            }
          }
        }
      }
      TokenOpt nt;
      nt.nx = N;
      nt.nz = N;
      nt.at = true;
      nt.where = place(-0.5f, -0.5f, -0.5f, -0.5f);
      addToken(s, cellPos(1, 1), 0, 0, 'N', true, nt);
    }
  }
  s.caption = t < 0.16f     ? "four files and four ranks go"
              : t < 0.24f   ? "and what is left moves in"
              : t < 0.40f   ? "a third axis"
              : t < 0.62f   ? "and a fourth, as a hypercube"
              : t < 0.78f   ? "which unfolds into four cubes"
              : show > 0.5f ? "one knight, forty-eight destinations"
                            : "the rook slides through aeon";
  return s;
}

OvertureScene sceneAtomic(const view::Theme& th, float t) {
  OvertureScene s;
  s.cam = {lerpf(0.22f, 0.46f, ease(t)), lerpf(0.95f, 0.60f, ease(seg(t, 0.4f, 1.0f))),
           lerpf(8.0f, 8.8f, ease(t)), 0.10f};
  const Pos pos = flatBoard();
  const float set = seg(t, 0.0f, 0.22f);
  const float app = seg(t, 0.22f, 0.44f);
  const float boom = seg(t, 0.44f, 0.70f);
  // A triangle of pawns, apex at d5, and a bishop waiting on the long diagonal.
  constexpr int kTri[9][2]{{3, 4}, {2, 3}, {3, 3}, {4, 3}, {1, 2},
                           {2, 2}, {3, 2}, {4, 2}, {5, 2}};
  const auto inBlast = [](int f, int r) {
    return std::abs(f - 3) <= 1 && std::abs(r - 4) <= 1;
  };
  GridOpt g;
  g.tone = [&, boom](int f, int r) {
    if (inBlast(f, r) && boom > 0.05f) return OvTone::Scorch;
    return ((f + r) % 2 != 0) ? OvTone::Dark : OvTone::Light;
  };
  addGrid(s, pos, g);
  addArmy(s, pos, 1.0f - ease(clampf(set * 1.25f, 0.0f, 1.0f)));

  const float here = ease(clampf(set * 1.4f, 0.0f, 1.0f));
  const float gone = 1.0f - ease(clampf(boom * 1.35f, 0.0f, 1.0f));
  for (const auto& cell : kTri) {
    const int f = cell[0];
    const int r = cell[1];
    if (inBlast(f, r) && boom > 0.0f) {
      if (f == 3 && r == 4) continue;  // the captured pawn is simply gone
      const float dx = fi(f - 3);
      const float dz = fi(r - 4);
      const float l = std::max(1e-3f, std::hypot(dx, dz));
      const float q = ease(boom);
      TokenOpt o;
      o.fade = gone;
      o.at = true;
      o.where = {fi(f) + 0.5f - 4.0f + dx / l * q * 3.4f,
                 std::sin(q * kPi) * 3.2f - q * q * 0.6f,
                 fi(r) + 0.5f - 4.0f + dz / l * q * 3.4f};
      addToken(s, pos, f, r, 'P', true, o);
    } else {
      TokenOpt o;
      o.fade = here;
      addToken(s, pos, f, r, 'P', true, o);
    }
  }
  {
    TokenOpt o;
    o.at = true;
    if (boom <= 0.0f) {
      o.fade = here;
      o.where = {lerpf(0.0f, 3.0f, ease(app)) + 0.5f - 4.0f, 0.0f,
                 lerpf(1.0f, 4.0f, ease(app)) + 0.5f - 4.0f};
    } else {
      const float q = ease(boom);
      o.fade = gone;
      o.where = {-0.5f - q * 2.4f, std::sin(q * kPi) * 3.6f, 0.5f - q * 2.0f};
    }
    addToken(s, pos, 0, 0, 'B', false, o);
  }
  if (boom > 0.0f) {
    // Warm only. An explosion is a *game* event, so it spends blood and ember and may
    // not touch the seam ramp - or the one thing a player learns by playing, that cold
    // means the board is not flat, stops being true.
    const OvVec3 at{-0.5f, 0.35f, 0.5f};
    s.bursts.push_back(
        {at, 0.6f + ease(boom) * 7.6f, 1.0f - ease(boom), th.blood, false});
    s.bursts.push_back(
        {at, 0.4f + easeIn(boom) * 2.2f, (1.0f - ease(boom)) * 0.9f, th.ember, false});
  }
  s.caption = t < 0.22f   ? "a triangle, and a bishop on the diagonal"
              : t < 0.44f ? "the bishop takes the apex"
              : t < 0.70f ? "and takes eight more with it"
                          : "nine cells, and what is left to count";
  return s;
}

OvertureScene sceneAtomicTorus(const view::Theme& th, float t) {
  OvertureScene s;
  const float form = ease(seg(t, 0.0f, 0.42f));
  const float unroll = ease(seg(t, 0.74f, 1.0f));
  const float shape = form * (1.0f - unroll);
  const float mark = seg(t, 0.42f, 0.56f);
  const float boom = seg(t, 0.56f, 0.74f);
  s.cam = {lerpf(0.15f, 0.80f, ease(seg(t, 0.0f, 0.5f))),
           lerpf(1.40f, 0.62f, ease(seg(t, 0.0f, 0.5f))),
           lerpf(8.0f, 5.8f, ease(seg(t, 0.0f, 0.5f))), 0.12f};
  TubeOpt o;
  o.th = shape * kTau;
  o.ph = shape * kTau;
  o.open = lerpf(1.0f, 2.2f, shape);
  const Pos pos = [o](float u, float v) { return tube(u, v, o); };

  // The 3 x 3 neighbourhood of a1 *on a torus*, which is four corners of the flat
  // board. The marks are a property of the cells, indexed by lattice position, so the
  // unroll carries them for free - which is the whole reason this overture exists.
  const auto inNb = [](int f, int r) {
    const int df = std::min((f + 8) % 8, (8 - f) % 8);
    const int dr = std::min((r + 8) % 8, (8 - r) % 8);
    return df <= 1 && dr <= 1;
  };
  GridOpt g;
  g.sub = shape > 0.02f ? 4 : 1;
  g.tone = [&, mark, boom](int f, int r) {
    if (!inNb(f, r)) return ((f + r) % 2 != 0) ? OvTone::Dark : OvTone::Light;
    if (boom > 0.04f) return OvTone::Scorch;
    if (mark > 0.04f) return OvTone::Lit;
    return ((f + r) % 2 != 0) ? OvTone::Dark : OvTone::Light;
  };
  addGrid(s, pos, g);
  addArmy(s, pos, 1.0f);

  if (boom > 0.0f) {
    const float q = ease(boom);
    const float fade = 1.0f - ease(clampf(boom * 1.3f, 0.0f, 1.0f));
    for (int f = 0; f < 8; ++f) {
      for (int r = 0; r < 8; ++r) {
        if (!inNb(f, r) || !(r <= 1 || r >= 6)) continue;
        const char glyph = (r == 1 || r == 6) ? 'P' : kBack[f];
        const OvVec3 c = cellCentre(pos, f, r, 8, 8);
        const OvVec3 n = normalAt(pos, (fi(f) + 0.5f) / 8.0f, (fi(r) + 0.5f) / 8.0f);
        TokenOpt tk;
        tk.fade = fade;
        tk.at = true;
        tk.where = add(c, mul(n, q * 3.0f));
        tk.hasNormal = true;
        tk.normal = n;
        addToken(s, pos, f, r, glyph, r <= 1, tk);
      }
    }
    const OvVec3 c0 = cellCentre(pos, 0, 0, 8, 8);
    s.bursts.push_back({c0, 0.6f + q * 6.4f, 1.0f - q, th.blood, false});
    s.bursts.push_back(
        {c0, 0.4f + easeIn(boom) * 2.0f, (1.0f - q) * 0.9f, th.ember, false});
  }
  s.caption = t < 0.42f   ? "the torus closes"
              : t < 0.56f ? "nine cells about a1"
              : t < 0.74f ? "and they are contiguous"
                          : "flat again - and they are four corners";
  return s;
}

OvertureScene sceneMustCapture(const view::Theme& th, float t) {
  OvertureScene s;
  s.cam = {0.18f, lerpf(1.50f, 1.18f, ease(t)), 8.2f, 0.09f};
  const Pos pos = flatBoard();
  const float start = seg(t, 0.0f, 0.24f);
  const float lit = seg(t, 0.24f, 0.46f);
  const float refuse = seg(t, 0.46f, 0.68f);
  const float play = seg(t, 0.68f, 1.0f);
  const float dim = ease(lit) * (1.0f - ease(play));
  const auto isCap = [](int f, int r) {
    return (f == 2 && r == 2) || (f == 5 && r == 3);
  };
  GridOpt g;
  g.tone = [&, dim](int f, int r) {
    return (isCap(f, r) && dim > 0.1f)
               ? OvTone::Lit
               : (((f + r) % 2 != 0) ? OvTone::Dark : OvTone::Light);
  };
  addGrid(s, pos, g);
  // Everything a capture cannot be made from dims right down. The board states the
  // constraint before anything moves, because a rule that acts by taking moves away has
  // nothing else to show.
  if (dim > 0.01f) {
    for (int f = 0; f < 8; ++f) {
      for (int r = 0; r < 8; ++r) {
        if (isCap(f, r)) continue;
        OvQuad q;
        q.p[0] = {fi(f) - 4.0f + 0.03f, 0.004f, fi(r) - 4.0f + 0.03f};
        q.p[1] = {fi(f) - 3.0f - 0.03f, 0.004f, fi(r) - 4.0f + 0.03f};
        q.p[2] = {fi(f) - 3.0f - 0.03f, 0.004f, fi(r) - 3.0f - 0.03f};
        q.p[3] = {fi(f) - 4.0f + 0.03f, 0.004f, fi(r) - 3.0f - 0.03f};
        q.tone = OvTone::Scrim;
        q.fade = dim * 0.55f;
        s.quads.push_back(q);
      }
    }
  }
  addArmy(s, pos, 1.0f - ease(clampf(start * 1.3f, 0.0f, 1.0f)));

  const float on = ease(clampf(start * 1.5f, 0.0f, 1.0f));
  struct Cast {
    int f, r;
    char g;
    bool white;
  };
  constexpr Cast kCast[]{{4, 0, 'K', true},  {4, 7, 'K', false}, {0, 1, 'P', true},
                         {7, 6, 'P', false}, {5, 3, 'B', true},  {3, 5, 'R', false},
                         {1, 6, 'P', false}, {6, 1, 'P', true}};
  for (const Cast& c : kCast) {
    TokenOpt o;
    o.fade = on;
    addToken(s, pos, c.f, c.r, c.g, c.white, o);
  }
  {
    TokenOpt o;
    o.fade = on * (1.0f - ease(clampf(play * 1.6f - 0.35f, 0.0f, 1.0f)));
    addToken(s, pos, 4, 4, 'N', false, o);
  }
  float kf = 2.0f;
  float kr = 2.0f;
  float lift = 0.0f;
  if (refuse > 0.0f && play <= 0.0f) {
    // A hard stop, not a bounce: the quiet move is not discouraged, it does not exist.
    const float q =
        refuse < 0.72f ? ease(refuse / 0.72f) : 1.0f - ease((refuse - 0.72f) / 0.28f);
    kf = lerpf(2.0f, 1.0f, q * 0.82f);
    kr = lerpf(2.0f, 4.0f, q * 0.82f);
    lift = std::sin(q * kPi) * 0.7f;
    if (refuse > 0.55f) {
      s.bursts.push_back(
          {{-2.5f, 0.25f, 0.5f},
           0.62f,
           clampf((refuse - 0.55f) / 0.2f, 0.0f, 1.0f) * (1.0f - ease(refuse * 0.6f)),
           th.blood,
           true});
    }
  } else if (play > 0.0f) {
    const float q = ease(play);
    kf = lerpf(2.0f, 4.0f, q);
    kr = lerpf(2.0f, 4.0f, q);
    lift = std::sin(q * kPi) * 1.4f;
  }
  {
    TokenOpt o;
    o.fade = on;
    o.at = true;
    o.where = {kf - 4.0f + 0.5f, lift, kr - 4.0f + 0.5f};
    addToken(s, pos, 0, 0, 'N', true, o);
  }
  s.caption = t < 0.24f   ? "a position with a capture in it"
              : t < 0.46f ? "a capture exists"
              : t < 0.68f ? "so the quiet move is not a move"
                          : "the capture plays";
  return s;
}

OvertureScene sceneMultiverse(const view::Theme& th, float t) {
  OvertureScene s;
  constexpr int N = 4;
  const float cut = ease(seg(t, 0.0f, 0.16f));
  const float shift = ease(seg(t, 0.16f, 0.22f));
  const float grow = seg(t, 0.22f, 0.46f);
  const float branch = seg(t, 0.46f, 0.70f);
  const float pres = seg(t, 0.70f, 1.0f);
  constexpr float BW = 6.6f;
  constexpr float BH = 7.4f;
  s.cam = {lerpf(0.0f, 0.22f, ease(t)), lerpf(1.50f, 1.02f, ease(t)),
           lerpf(8.0f, 19.0f, ease(seg(t, 0.2f, 1.0f))), 0.08f};
  addCutGrid(s, N, cut);

  // Continuous lattice extent, so the recentring is smooth rather than jumping every
  // time a board is born. The first board sits dead centre and the lattice grows around
  // it - a lattice drifting off the pane explains nothing.
  const auto bornOf = [=](int k, int l) -> float {
    if (k == 0 && l == 0) return 1.0f;
    if (l == 1) {
      return ease(clampf((branch - (k == 1 ? 0.28f : 0.72f)) / 0.28f, 0.0f, 1.0f));
    }
    if (k == 1) return ease(clampf((grow - 0.22f) / 0.3f, 0.0f, 1.0f));
    if (k == 2) return ease(clampf((grow - 0.64f) / 0.3f, 0.0f, 1.0f));
    return ease(clampf((pres - 0.34f) / 0.3f, 0.0f, 1.0f));
  };
  // The lattice's extent, tracking the boards that are actually being born rather than a
  // clock of its own: a board fading in has to pull the centring with it, or the whole
  // arrangement leans for as long as it takes to arrive.
  const float kGrow = bornOf(1, 0) + bornOf(2, 0) + bornOf(3, 0);
  const float lGrow = std::max(bornOf(1, 1), bornOf(2, 1));
  // Where the surviving block's centre is: at the a1 corner when the cut has just
  // finished, and at the origin once it has slid. Every board is placed relative to it,
  // so the lattice inherits the slide instead of teleporting to the middle.
  const float slide = (fi(N) * 0.5f - 4.0f) * (1.0f - shift);
  // A board being born slides out of the one it was appended to rather than fading in
  // where it will end up. Two reasons, and the second is the load-bearing one: a move
  // *appending* a board is the thing being explained, and a board that occupied its
  // final slot while still invisible made the whole lattice lean for as long as it took
  // to arrive, because the extent jumped and the recentring did not.
  const auto originOf = [=](int k, int l) -> OvVec3 {
    const float kEff = k > 0 ? fi(k - 1) + bornOf(k, l) : 0.0f;
    const float lEff = l > 0 ? bornOf(k, l) : 0.0f;
    return {(kEff - kGrow * 0.5f) * BW + slide, 0.0f, (lEff - lGrow * 0.5f) * BH + slide};
  };
  const auto posOf = [=](int k, int l) -> Pos {
    const OvVec3 o = originOf(k, l);
    const float born = bornOf(k, l);
    return [=](float u, float v) -> OvVec3 {
      return {o.x + u * fi(N) - fi(N) * 0.5f, (1.0f - born) * 4.0f,
              o.z + v * fi(N) - fi(N) * 0.5f};
    };
  };
  struct Board {
    int k, l;
  };
  std::vector<Board> boards{{0, 0}};
  if (grow > 0.22f) boards.push_back({1, 0});
  if (grow > 0.64f) boards.push_back({2, 0});
  if (branch > 0.28f) boards.push_back({1, 1});
  if (branch > 0.72f) boards.push_back({2, 1});
  if (pres > 0.34f) boards.push_back({3, 0});
  if (pres > 0.58f) boards.push_back({3, 1});

  constexpr char kRoyal[N]{'R', 'N', 'K', 'R'};
  for (const Board& b : boards) {
    const float born = bornOf(b.k, b.l);
    if (born <= 0.02f) continue;
    const Pos bp = posOf(b.k, b.l);
    GridOpt g;
    g.nx = N;
    g.nz = N;
    g.fade = born;
    addGrid(s, bp, g);
    // Boards along the turn axis alternate whose move it is, and are rimmed alternately
    // to say so. The present column takes the accent.
    const bool isPresent = pres > 0.34f && b.k == 3;
    addRim(s, bp,
           isPresent ? th.ember : ((b.k % 2 == 0) ? th.boneFaint : seamColour(th, 0, 2)),
           isPresent ? 2.6f : 1.4f, born);
    // The boards' own schematic army arrives as the opening array leaves, so the flat
    // 8 x 8 this overture opens on carries exactly the pieces every other one does -
    // and not, for a frame, both sets at once.
    const float schematic = ease(clampf(cut * 3.0f, 0.0f, 1.0f));
    for (int f = 0; f < N; ++f) {
      TokenOpt o;
      o.nx = N;
      o.nz = N;
      o.fade = born * schematic;
      o.height = 0.8f;
      addToken(s, bp, f, 0, kRoyal[f], true, o);
      addToken(s, bp, f, N - 1, kRoyal[f], false, o);
    }
  }
  if (grow < 0.3f) {
    const Pos flat = flatBoard();
    for (int f = 0; f < 8; ++f) {
      for (const int r : {0, 1, 6, 7}) {
        // The survivors have their own board above - except before the cut starts, where
        // they have to be here or the flat 8 x 8 opens with a corner of its army
        // missing, and handing over from another overture becomes a jump.
        const bool doomed = !(f < N && r < N);
        const float k = doomed ? cutFraction(N, f, r, cut)
                               : 1.0f - ease(clampf(cut * 3.0f, 0.0f, 1.0f));
        if (k <= 0.02f) continue;
        TokenOpt o;
        o.fade = k;
        addToken(s, flat, f, r, (r == 1 || r == 6) ? 'P' : kBack[f], r <= 1, o);
      }
    }
  }
  for (const Board& b : boards) {
    if (b.k == 0) continue;
    const bool hasPrev = std::any_of(boards.begin(), boards.end(), [&](const Board& o) {
      return o.k == b.k - 1 && o.l == b.l;
    });
    if (!hasPrev) continue;
    const OvVec3 prev = originOf(b.k - 1, b.l);
    const OvVec3 here = originOf(b.k, b.l);
    OvTrail tr;
    tr.colour = th.boneFaint;
    tr.width = 1.3f;
    tr.fade = 0.7f * bornOf(b.k, b.l);
    tr.dashed = true;
    tr.pts = {{prev.x + fi(N) * 0.5f + 0.2f, 0.2f, prev.z},
              {here.x - fi(N) * 0.5f - 0.2f, 0.2f, here.z}};
    s.trails.push_back(tr);
  }
  if (branch > 0.28f) {
    const OvVec3 a = originOf(1, 0);
    const OvVec3 b = originOf(1, 1);
    const float s0 = ease(clampf((branch - 0.28f) / 0.34f, 0.0f, 1.0f));
    OvTrail tr;
    tr.colour = seamColour(th, 1, 2);
    tr.width = 2.6f;
    for (int i = 0; i <= 16; ++i) {
      const float q = fi(i) / 16.0f * s0;
      tr.pts.push_back({lerpf(a.x, b.x, q) - std::sin(q * kPi) * 2.4f,
                        0.4f + std::sin(q * kPi) * 1.1f, lerpf(a.z, b.z, q)});
    }
    s.trails.push_back(tr);
  }
  s.caption = t < 0.16f   ? "cut, so seven boards will read"
              : t < 0.22f ? "one board, centred"
              : t < 0.46f ? "every half-move appends a board"
              : t < 0.70f ? "going back makes a second timeline"
                          : "the present is a column, not a board";
  return s;
}

OvertureScene sceneTorus3d(const view::Theme& th, float t) {
  OvertureScene s;
  constexpr int N = 4;
  const float cut = ease(seg(t, 0.0f, 0.10f));
  const float shift = ease(seg(t, 0.10f, 0.15f));
  const float ext = ease(seg(t, 0.15f, 0.26f));
  const float rollF = ease(seg(t, 0.26f, 0.38f));  // files close
  const float rollR = ease(seg(t, 0.38f, 0.52f));  // ranks close
  const float undo = ease(seg(t, 0.62f, 0.72f));   // and both open again
  const float cop = seg(t, 0.78f, 0.90f);
  const float show = seg(t, 0.90f, 1.0f);
  const float off = (4.0f - fi(N) * 0.5f) * shift;
  // Level spacing. It opens so the extrusion reads, closes to exactly one cell so the
  // lattice of copies has the same period on all three axes - it must, the manifold is a
  // cube - and opens again at the end so the 26 destinations are not buried in a block.
  const float gap = lerpf(lerpf(0.0f, 1.6f, ext), 1.0f, ease(seg(t, 0.68f, 0.78f))) +
                    lerpf(0.0f, 0.7f, ease(seg(t, 0.90f, 1.0f)));
  // `theta` rather than `th`, which is the theme.
  const float theta = rollF * (1.0f - undo) * kTau;
  const float ph = rollR * (1.0f - undo) * kTau;
  const float curl = std::max(rollF, rollR) * (1.0f - undo);
  // A level is a height above the board when the box is flat, and a shell radius when it
  // is rolled. Both are the same parameter to `shellTube`, which is why the whole
  // overture is one surface function rather than a blend between two.
  const auto shellOf = [&](int L) {
    return lerpf((fi(L) - 1.5f) * gap, 0.62f + fi(L) * 0.82f, curl);
  };
  const float open = lerpf(1.0f, 10.5f, rollR * (1.0f - undo));
  const auto posOf = [&](int L) -> Pos {
    const float d = shellOf(L);
    return [theta, ph, d, open, off](float u, float v) -> OvVec3 {
      const OvVec3 p = shellTube(u, v, theta, ph, d, open);
      return {p.x + off - 2.0f, p.y, p.z + off - 2.0f};
    };
  };
  // Back up again for the box: a cube seen from too low reads as a tower, and the one
  // thing this shape may not look like is taller on one axis than another.
  const float dist = lerpf(22.0f, 30.0f, ease(seg(t, 0.15f, 0.55f))) -
                     lerpf(0.0f, 5.0f, ease(seg(t, 0.62f, 0.78f))) +
                     lerpf(0.0f, 10.0f, ease(seg(t, 0.78f, 0.90f))) -
                     lerpf(0.0f, 17.0f, ease(seg(t, 0.90f, 1.0f)));
  s.cam = {lerpf(0.05f, 0.92f, ease(t)),
           lerpf(1.45f, 0.50f, ease(t)) + lerpf(0.0f, 0.30f, ease(seg(t, 0.72f, 1.0f))),
           dist * 0.36f, 0.11f};

  addCutGrid(s, N, cut);
  constexpr float kShell[N]{1.0f, 0.70f, 0.52f, 0.40f};
  for (int L = 0; L < N; ++L) {
    const float alive =
        L == 0 ? 1.0f : clampf(ext * 1.7f - (fi(L) - 1.0f) * 0.20f, 0.0f, 1.0f);
    if (alive <= 0.01f) continue;
    GridOpt g;
    g.nx = N;
    g.nz = N;
    g.inset = 0.04f;
    g.sub = curl > 0.02f ? 3 : 1;
    g.fade = alive * lerpf(1.0f, kShell[L], curl);
    g.tone = [L](int f, int r) {
      return ((f + r + L) % 2 != 0) ? OvTone::Dark : OvTone::Light;
    };
    addGrid(s, posOf(L), g);
  }
  // The 8 x 8 army goes with its cells, then leaves with the extrusion.
  if (ext < 0.6f) {
    addCutArmy(s, N, cut, posOf(0), 1.0f - ease(clampf(ext * 1.8f, 0.0f, 1.0f)));
  }
  // Name the pair that is going to fail before it fails: the level axis glues the
  // outermost shell to the innermost, so both get its hue while the ring closes.
  const float named = ease(seg(t, 0.38f, 0.50f)) * (1.0f - ease(seg(t, 0.60f, 0.70f)));
  if (named > 0.02f) {
    for (const int L : {0, 3}) {
      OvTrail tr;
      tr.colour = seamColour(th, 2, 3);
      tr.width = 2.6f;
      tr.fade = named;
      for (int i = 0; i <= 40; ++i) tr.pts.push_back(posOf(L)(0.75f, fi(i) / 40.0f));
      s.trails.push_back(tr);
    }
  }
  // The refusal: outermost shell to innermost, which is not a step space can take.
  const float bad = seg(t, 0.52f, 0.62f) * (1.0f - ease(seg(t, 0.62f, 0.70f)));
  if (bad > 0.02f) {
    for (int i = 0; i < 6; ++i) {
      const float v = (fi(i) + 0.5f) / 6.0f;
      const OvVec3 a = posOf(3)(0.75f, v);
      OvTrail tr;
      tr.colour = th.blood;
      tr.width = 2.6f;
      tr.fade = bad * 0.9f;
      tr.dashed = true;
      tr.pts = {a, posOf(0)(0.75f, v)};
      s.trails.push_back(tr);
      s.bursts.push_back({a, 0.55f, bad, th.blood, true});
    }
  }
  // The box. Its cells span [-4 + off, off] on file and rank - the cut block's own
  // coordinates, slid by `off` - and 4 x gap on level.
  const OvVec3 lo{-4.0f + off, -2.0f * gap, -4.0f + off};
  const OvVec3 hi{off, 2.0f * gap, off};
  const auto cellAt = [&](int f, int r, int L) -> OvVec3 {
    return {fi(f) + 0.5f - 4.0f + off, (fi(L) - 1.5f) * gap, fi(r) + 0.5f - 4.0f + off};
  };
  const float faces = ease(seg(t, 0.68f, 0.78f));
  addPortalFaces(s, th, lo, hi, 3, faces * 0.14f * (1.0f - 0.7f * ease(show)),
                 faces * 0.9f * (1.0f - 0.45f * ease(show)));
  // The 26 translates. A face-neighbour gets the whole wire box in its axis hue; an edge
  // or corner neighbour gets one face loop, which places it and keeps the draw count near
  // 60 polylines rather than over 300 segments.
  const float ghost = ease(clampf(cop * 1.3f, 0.0f, 1.0f)) *
                      (1.0f - ease(clampf(show * 1.7f, 0.0f, 1.0f)));
  if (ghost > 0.02f) {
    const OvVec3 per{4.0f, 4.0f * gap, 4.0f};
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -1; dz <= 1; ++dz) {
          const int man = std::abs(dx) + std::abs(dy) + std::abs(dz);
          if (man == 0) continue;
          const float bornG = clampf(cop * 2.8f - fi(man - 1) * 0.26f, 0.0f, 1.0f);
          if (bornG <= 0.02f) continue;
          const OvVec3 d{fi(dx) * per.x, fi(dy) * per.y, fi(dz) * per.z};
          const int axis = man == 1 ? (dx != 0 ? 0 : (dy != 0 ? 1 : 2)) : -1;
          const view::Rgba tone = axis < 0 ? th.boneFaint : seamColour(th, axis, 3);
          const float fade = ghost * bornG * (axis < 0 ? 0.24f : 0.60f);
          addWireBox(s, lo, hi, d, axis >= 0, tone, axis < 0 ? 0.8f : 1.3f, fade);
        }
      }
    }
  }
  // A rook off one face and back in at the other: one straight line, cut in two.
  if (cop > 0.24f && show < 0.85f) {
    const float q = ease(clampf((cop - 0.24f) / 0.66f, 0.0f, 1.0f));
    const OvVec3 home = cellAt(0, 1, 3);
    const float run = q * 7.6f;
    constexpr float kPeriod = 4.0f;
    const auto wrap = [&](float x) {
      return std::fmod(std::fmod(x - lo.x, kPeriod) + kPeriod, kPeriod) + lo.x;
    };
    {
      OvTrail tr;
      tr.colour = seamColour(th, 0, 3);
      tr.width = 1.5f;
      tr.fade = 0.6f * ghost;
      tr.dashed = true;
      tr.pts = {{home.x, home.y + 0.32f, home.z}, {home.x + run, home.y + 0.32f, home.z}};
      s.trails.push_back(tr);
    }
    // The same line inside the box, cut wherever it leaves a face.
    std::vector<OvVec3> part;
    float prev = 0.0f;
    bool first = true;
    for (int i = 0; i <= 48; ++i) {
      const float x = wrap(home.x + run * fi(i) / 48.0f);
      if (!first && x < prev - 1e-3f) {
        if (part.size() > 1) {
          OvTrail tr;
          tr.colour = th.ember;
          tr.width = 2.6f;
          tr.fade = 1.0f - ease(show);
          tr.pts = part;
          s.trails.push_back(tr);
        }
        part.clear();
      }
      part.push_back({x, home.y + 0.32f, home.z});
      prev = x;
      first = false;
    }
    if (part.size() > 1) {
      OvTrail tr;
      tr.colour = th.ember;
      tr.width = 2.6f;
      tr.fade = 1.0f - ease(show);
      tr.pts = part;
      s.trails.push_back(tr);
    }
    TokenOpt tk;
    tk.nx = N;
    tk.nz = N;
    tk.at = true;
    tk.where = {wrap(home.x + run), home.y, home.z};
    tk.hasNormal = true;
    tk.normal = {0.0f, 1.0f, 0.0f};
    tk.fade = 1.0f - ease(clampf(show * 2.2f, 0.0f, 1.0f));
    addToken(s, posOf(3), 0, 0, 'R', true, tk);
  }
  // Twenty-six, and not one of them clipped.
  if (show > 0.0f) {
    const float q = ease(show);
    TokenOpt kt;
    kt.nx = N;
    kt.nz = N;
    kt.at = true;
    kt.where = cellAt(1, 1, 1);
    kt.hasNormal = true;
    kt.normal = {0.0f, 1.0f, 0.0f};
    addToken(s, posOf(1), 0, 0, 'K', true, kt);
    int k = 0;
    for (int df = -1; df <= 1; ++df) {
      for (int dr = -1; dr <= 1; ++dr) {
        for (int dl = -1; dl <= 1; ++dl) {
          const int used = (df != 0 ? 1 : 0) + (dr != 0 ? 1 : 0) + (dl != 0 ? 1 : 0);
          if (used == 0) continue;
          ++k;
          s.bursts.push_back(
              {cellAt((1 + df + 4) % 4, (1 + dr + 4) % 4, (1 + dl + 4) % 4), 0.34f,
               clampf(q * 2.0f - fi(k) / 44.0f, 0.0f, 1.0f) * 0.95f,
               seamColour(th, used - 1, 3), false});
        }
      }
    }
  }
  s.caption = t < 0.10f   ? "four files and four ranks go"
              : t < 0.15f ? "and what is left moves in"
              : t < 0.26f ? "a third axis, four levels deep"
              : t < 0.38f ? "the files close, as ever"
              : t < 0.52f ? "the ranks close, and the levels nest"
              : t < 0.62f ? "the third has nowhere to go"
              : t < 0.78f ? "so the box keeps the gluing as colour"
              : t < 0.90f ? "leave a face, arrive at the opposite one"
                          : "twenty-six neighbours, and no cell without them";
  return s;
}

OvertureScene sceneT6(const view::Theme& th, float t) {
  OvertureScene s;
  constexpr int N = 4;
  const float cut = ease(seg(t, 0.0f, 0.08f));
  const float shift = ease(seg(t, 0.08f, 0.13f));
  const float ext = ease(seg(t, 0.13f, 0.24f));
  const float lat = seg(t, 0.24f, 0.36f);
  const float skew = seg(t, 0.36f, 0.58f);
  const float gather = seg(t, 0.58f, 0.76f);
  const float settle = ease(seg(t, 0.68f, 0.80f));
  const float walk = seg(t, 0.76f, 0.88f);
  const float fan = seg(t, 0.88f, 1.0f);
  const float off = (4.0f - fi(N) * 0.5f) * shift;
  const float gap = ext;  // a true cube the moment it exists
  const float dist = lerpf(22.0f, 34.0f, ease(seg(t, 0.20f, 0.36f))) +
                     lerpf(0.0f, 9.0f, ease(seg(t, 0.36f, 0.56f))) -
                     lerpf(0.0f, 15.0f, ease(seg(t, 0.58f, 0.76f)));
  s.cam = {lerpf(-0.05f, 1.32f, ease(t)), lerpf(1.45f, 0.60f, ease(t)), dist * 0.34f,
           0.10f};

  const auto boxAt = [=](int L, const OvVec3& o) -> Pos {
    return [=](float u, float v) -> OvVec3 {
      return {u * fi(N) - 4.0f + off + o.x, (fi(L) - 1.5f) * gap + o.y,
              v * fi(N) - 4.0f + off + o.z};
    };
  };
  const OvVec3 origin{0.0f, 0.0f, 0.0f};
  const OvVec3 lo{-4.0f + off, -2.0f * gap, -4.0f + off};
  const OvVec3 hi{off, 2.0f * gap, off};

  // The three axes that get a direction, and the three that do not. E_SKEW is
  // incommensurate with the cubic lattice on purpose: a copy offset by one of these lands
  // PART-WAY THROUGH its neighbours instead of beside them, which is the whole visual
  // argument. Nothing here is noise - every box is a real translate of the domain.
  static constexpr float kSkew[3][3]{
      {2.7f, 2.3f, -1.5f}, {-1.9f, 2.7f, 2.5f}, {2.3f, -2.5f, 2.1f}};
  struct Copy {
    OvVec3 p;
    int man;
    int axis;
    bool skew;
  };
  std::vector<Copy> copies;
  for (int a = -1; a <= 1; ++a) {
    for (int b = -1; b <= 1; ++b) {
      for (int c = -1; c <= 1; ++c) {
        if (a == 0 && b == 0 && c == 0) continue;
        const int man = std::abs(a) + std::abs(b) + std::abs(c);
        const int axis = man == 1 ? (a != 0 ? 0 : (b != 0 ? 1 : 2)) : -1;
        copies.push_back(
            {{fi(a) * 4.0f, fi(b) * 4.0f * gap, fi(c) * 4.0f}, man, axis, false});
      }
    }
  }
  static constexpr int kSeeds[7][3]{{0, 0, 0},  {1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                    {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int j = 0; j < 3; ++j) {
    for (const int sg : {-1, 1}) {
      for (int q = 0; q < 7; ++q) {
        const int* sd = kSeeds[q];
        copies.push_back({{fi(sd[0]) * 4.0f + fi(sg) * kSkew[j][0],
                           fi(sd[1]) * 4.0f * gap + fi(sg) * kSkew[j][1],
                           fi(sd[2]) * 4.0f + fi(sg) * kSkew[j][2]},
                          1 + q,
                          3 + j,
                          true});
      }
    }
  }
  const auto srcOf = [&](int k1, int k2) -> OvVec3 {
    const int idx = ((k1 * kQuinticN + k2) * 11) % static_cast<int>(copies.size());
    return copies[static_cast<std::size_t>(idx)].p;
  };
  const auto born = [&](int k1, int k2) {
    return ease(clampf((gather - fi(k1 + k2) / 8.0f * 0.5f) / 0.5f, 0.0f, 1.0f));
  };

  addCutGrid(s, N, cut);
  // The fundamental domain's own cells, until the sheets take over.
  const float boxFade = 1.0f - ease(clampf(gather * 1.6f, 0.0f, 1.0f));
  if (boxFade > 0.02f) {
    for (int L = 0; L < N; ++L) {
      const float alive =
          L == 0 ? 1.0f : clampf(ext * 1.7f - (fi(L) - 1.0f) * 0.20f, 0.0f, 1.0f);
      if (alive <= 0.01f) continue;
      GridOpt g;
      g.nx = N;
      g.nz = N;
      g.inset = 0.04f;
      g.fade = alive * boxFade;
      g.tone = [L](int f, int r) {
        return ((f + r + L) % 2 != 0) ? OvTone::Dark : OvTone::Light;
      };
      addGrid(s, boxAt(L, origin), g);
    }
  }
  if (ext < 0.6f) {
    addCutArmy(s, N, cut, boxAt(0, origin), 1.0f - ease(clampf(ext * 1.8f, 0.0f, 1.0f)));
  }
  // Three axes, three pairs of faces - torus3d's vocabulary, quoted outright and without
  // its nested-tori detour. That beat belongs to that overture; repeating it here would
  // make the two hardest entries in the library the same animation.
  const float faces =
      ease(seg(t, 0.18f, 0.26f)) * (1.0f - ease(clampf(gather * 1.6f, 0.0f, 1.0f)));
  addPortalFaces(s, th, lo, hi, 6, faces * 0.14f, faces * 0.9f);
  // The lattice, and then the tangle. Every skew copy is drawn as a whole wire box,
  // because a box landing part-way through another box is the entire argument and a few
  // loose rails would read as haze.
  const float shown = ease(clampf(lat * 1.3f, 0.0f, 1.0f));
  const float messy = ease(clampf(skew * 1.2f, 0.0f, 1.0f));
  const float fadeOut = 1.0f - ease(clampf(gather * 1.5f, 0.0f, 1.0f));
  if (shown > 0.02f && fadeOut > 0.02f) {
    for (const Copy& cp : copies) {
      const float arrive =
          cp.skew
              ? clampf(messy * 2.4f - fi(cp.man - 1) * 0.22f - fi(cp.axis - 3) * 0.20f,
                       0.0f, 1.0f)
              : clampf(shown * 2.6f - fi(cp.man - 1) * 0.26f, 0.0f, 1.0f);
      if (arrive <= 0.02f) continue;
      const view::Rgba tone = cp.axis < 0 ? th.boneFaint : seamColour(th, cp.axis, 6);
      const float w = cp.axis < 0 ? 0.8f : (cp.skew ? 1.0f : 1.3f);
      const float fade =
          arrive * fadeOut * (cp.axis < 0 ? 0.22f : (cp.skew ? 0.40f : 0.58f));
      addWireBox(s, lo, hi, cp.p, cp.skew || cp.axis >= 0, tone, w, fade);
    }
  }
  // Where a skew copy lands inside one already there.
  const float clash = ease(clampf((skew - 0.30f) / 0.45f, 0.0f, 1.0f)) * fadeOut;
  if (clash > 0.02f) {
    const OvVec3 mid{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
    int n = 0;
    for (int j = 0; j < 3; ++j) {
      for (const int sg : {-1, 1}) {
        for (const float g : {0.5f, 1.0f}) {
          ++n;
          s.bursts.push_back(
              {{mid.x + fi(sg) * kSkew[j][0] * g, mid.y + fi(sg) * kSkew[j][1] * g,
                mid.z + fi(sg) * kSkew[j][2] * g},
               0.95f,
               clampf(clash * 1.8f - fi(n) / 14.0f, 0.0f, 1.0f) * 0.9f,
               th.blood,
               true});
        }
      }
    }
  }
  // Resolved: every surviving copy collapses to one sheet and flies to its patch.
  for (int k1 = 0; k1 < kQuinticN; ++k1) {
    for (int k2 = 0; k2 < kQuinticN; ++k2) {
      const float m = born(k1, k2);
      if (m <= 0.005f) continue;
      const Pos src = boxAt(1, srcOf(k1, k2));
      GridOpt g;
      g.nx = N;
      g.nz = N;
      g.inset = lerpf(0.04f, 0.015f, m);
      g.sub = m > 0.02f ? 2 : 1;
      // Opaque almost at once: the sheet is not arriving, it IS the copy that was already
      // on screen, now flattened. Fading it in would read as a new object.
      g.fade = clampf(m * 5.0f, 0.0f, 1.0f) * lerpf(1.0f, 0.55f, m);
      g.tone = [k1, k2](int f, int r) {
        return ((f + r + k1 + k2) % 2 != 0) ? OvTone::Dark : OvTone::Light;
      };
      addGrid(s, blendPos(src, quinPatch(k1, k2), m), g);
    }
  }
  // Fourteen figures, walking the surface's own parameter lines, both colours. `t` is the
  // clock, so they drift forwards on the way out and backwards on the way home, for free.
  if (settle > 0.02f) {
    static constexpr char kGlyphs[6]{'R', 'N', 'B', 'Q', 'K', 'P'};
    for (int i = 0; i < 14; ++i) {
      const int k1 = i % kQuinticN;
      const int k2 = (i * 2 + 1) % kQuinticN;
      if (born(k1, k2) < 0.9f) continue;
      const float du = (rndFloat(i * 5 + 1) - 0.5f) * 1.1f;
      const float dv = (rndFloat(i * 5 + 2) - 0.5f) * 1.1f;
      float u = rndFloat(i * 5 + 3) + t * du;
      u = std::fmod(std::fmod(u, 1.0f) + 1.0f, 1.0f);
      const float v = 0.5f + 0.44f * std::sin((rndFloat(i * 5 + 4) + t * dv) * kTau);
      const Pos p = quinPatch(k1, k2);
      TokenOpt tk;
      tk.at = true;
      tk.where = p(u, v);
      tk.hasNormal = true;
      tk.normal = normalAt(p, u, v);
      tk.fade = settle;
      addToken(s, p, 0, 0, kGlyphs[i % 6], i % 2 == 0, tk);
    }
  }
  // The rook's line of four: three steps drawn on the sheet, the fourth as a link - on a
  // 4-cell periodic axis a rook's line is a closed loop, and the quintic is not this
  // board's own space, so the return is called out rather than merged.
  if (walk > 0.0f) {
    const float q = ease(walk);
    const Pos p = quinPatch(1, 2);
    const float v0 = 1.5f / fi(N);
    const float u0 = 0.5f / fi(N);
    const float span = std::min(q * 4.0f, 3.0f) / fi(N);
    {
      OvTrail tr = trailOn(p, u0, v0, u0 + span, v0, 28, th.ember, 0.22f);
      tr.width = 2.6f;
      s.trails.push_back(tr);
    }
    const float close = clampf(q * 4.0f - 3.0f, 0.0f, 1.0f);
    if (close > 0.0f) {
      const OvVec3 a = p(u0 + 3.0f / fi(N), v0);
      const OvVec3 b = p(u0, v0);
      OvTrail tr;
      tr.colour = seamColour(th, 0, 6);
      tr.width = 2.2f;
      tr.dashed = true;
      for (int i = 0; i <= 18; ++i) {
        const float q2 = fi(i) / 18.0f * close;
        const float lift = std::sin(q2 * kPi) * 2.0f;
        tr.pts.push_back(
            {lerpf(a.x, b.x, q2), lerpf(a.y, b.y, q2) + lift, lerpf(a.z, b.z, q2)});
      }
      s.trails.push_back(tr);
    }
    const float uNow = close > 0.99f ? u0 : u0 + span;
    TokenOpt tk;
    tk.at = true;
    tk.where = p(uNow, v0);
    tk.hasNormal = true;
    tk.normal = normalAt(p, uNow, v0);
    addToken(s, p, 0, 0, 'R', true, tk);
  }
  // The knight's atom: {1,2} on every ordered pair of six axes, both signs. 120 vectors;
  // 60 cells, because +2 and -2 coincide on an axis of extent 4. The animation draws 60
  // pips, and it is a real fact about this variant that the two signs of the magnitude-2
  // leg land on the same cell.
  if (fan > 0.0f) {
    const float q = ease(fan);
    // The address: the four axes with no sheet of their own, read as a base-4 number and
    // spread over the 25 patches by a multiplier coprime to 25. A labelling for the eye -
    // distinct cells get distinct places - and emphatically not a projection. The tangle
    // is what a projection would have cost.
    const auto addr = [&](const int c[6]) -> OvVec3 {
      const int k =
          ((c[2] + 4 * c[3] + 16 * c[4] + 64 * c[5]) * 7) % (kQuinticN * kQuinticN);
      return quinPatch(k % kQuinticN, k / kQuinticN)((fi(c[0]) + 0.5f) / fi(N),
                                                     (fi(c[1]) + 0.5f) / fi(N));
    };
    const int base[6]{1, 1, 1, 1, 1, 1};
    std::vector<std::array<int, 6>> seen;
    int drawn = 0;
    for (int i = 0; i < 6; ++i) {
      for (int j = 0; j < 6; ++j) {
        if (i == j) continue;
        for (const int si : {-1, 1}) {
          for (const int sj : {-1, 1}) {
            std::array<int, 6> c{base[0], base[1], base[2], base[3], base[4], base[5]};
            const auto ui = static_cast<std::size_t>(i);
            const auto uj = static_cast<std::size_t>(j);
            c[ui] = (c[ui] + si + 4) % 4;
            c[uj] = (c[uj] + sj * 2 + 4) % 4;
            if (std::find(seen.begin(), seen.end(), c) != seen.end()) continue;
            seen.push_back(c);
            ++drawn;
            s.bursts.push_back({addr(c.data()), 0.26f,
                                clampf(q * 2.0f - fi(drawn) / 100.0f, 0.0f, 1.0f) * 0.9f,
                                seamColour(th, std::min(i, j), 6), false});
          }
        }
      }
    }
    TokenOpt tk;
    tk.nx = N;
    tk.nz = N;
    tk.at = true;
    tk.where = addr(base);
    addToken(s, quinPatch(0, 0), 0, 0, 'N', true, tk);
  }
  s.caption = t < 0.08f   ? "four files and four ranks go"
              : t < 0.13f ? "and what is left moves in"
              : t < 0.24f ? "three axes, three pairs of faces"
              : t < 0.36f ? "a glued box is already a Calabi-Yau: the flat one"
              : t < 0.58f ? "three more axes, and space has no directions left"
              : t < 0.76f ? "what a lattice could not hold, a manifold can"
              : t < 0.88f ? "a rook's line of four is a closed loop"
                          : "one knight, a hundred and twenty directions";
  return s;
}

/// A move that shows the variant, for the derived overture: a real piece of the variant's
/// own start position, taking a real route the engine traced, preferring one that leaves
/// through a seam. Returns nothing for a board the 8x8 canvas cannot show - a higher-D or
/// non-8x8 board gets the surface alone for now.
struct DemoMove {
  view::MovePath path;
  PieceTypeId type{kNoPiece};
  Color color{Color::White};
};

std::optional<DemoMove> demoMoveFor(const VariantSpec& v, const Position& pos) {
  if (v.dims.dims() != 2 || v.dims.extent(0) != 8 || v.dims.extent(1) != 8) {
    return std::nullopt;
  }
  std::optional<DemoMove> fallback;
  int traces = 0;  // bound the scan: a wrapped route is usually found in the first few
  for (const StartPiece& sp : v.start) {
    if (sp.type == kNoPiece || sp.type >= v.pieces.size()) continue;
    if (v.pieces[sp.type].royal || v.pieces[sp.type].atoms.empty()) continue;
    const auto c = static_cast<std::size_t>(sp.color);
    const CellId from = v.dims.toCell(sp.at);
    for (const MoveAtom& atom : v.pieces[sp.type].atoms) {
      if (atom.mode == MoveMode::Hop) continue;
      const std::uint32_t limit =
          atom.maxK == kUnlimited ? 8u : std::min<std::uint32_t>(atom.maxK, 8u);
      for (std::uint32_t di = atom.dirBegin[c]; di < atom.dirEnd[c] && traces < 400;
           ++di) {
        if (di >= v.dirTable.size()) break;
        Walker w = v.geom.start(from, v.dirTable[di]);
        for (std::uint32_t k = 1; k <= limit; ++k) {
          if (!v.geom.step(w)) break;
          if (w.cell == from) break;
          Move m;
          m.from = from;
          m.to = w.cell;
          ++traces;
          const view::MovePath path = view::tracePath(v, pos, sp.type, sp.color, m);
          if (path.unexplained || path.steps.empty()) continue;
          for (const view::PathStep& st : path.steps) {
            if (st.kind == view::StepKind::Portal)
              return DemoMove{path, sp.type, sp.color};
          }
          if (!fallback.has_value()) fallback = DemoMove{path, sp.type, sp.color};
        }
      }
    }
  }
  return fallback;
}

/// The derived overture for a board of three or more dimensions: the board's own grid,
/// laid out by `view::layout` and extruded from stacked to spaced as the cycle forms it.
/// Above 2-D there is no faithful embedding to warp into - the hand-authored
/// `cube5`/`hyper4`/`t6` are stylised for the same reason - so the honest generalisation
/// is to animate into the very lattice the game will draw (M13.4).
OvertureScene derivedGridOverture(const VariantSpec& v, float t, const view::Theme& th) {
  (void)th;  // tones are `Light`/`Dark`; the draw layer colours them from the theme
  OvertureScene s;
  const float form = ease(t);
  const float settle = ease(seg(t, 0.0f, 0.12f));
  s.cam = {lerpf(kOpenCam.yaw, 0.66f, ease(t)), lerpf(kOpenCam.elev, 0.62f, ease(t)),
           lerpf(kOpenCam.reach, 9.5f, ease(t)), kOpenCam.persp};

  view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  // The depth axis is squeezed at t = 0 and the grid axes close up, so the whole lattice
  // grows out of one board rather than arriving as a block.
  cfg.depthSpacing = lerpf(0.35f, 2.6f, form);
  cfg.gridGap = lerpf(0.35f, 2.2f, form);
  const std::vector<view::Placement> places = view::layout(v.dims, cfg);

  // `layout` numbers cells from zero; centre each axis so the lattice sits in the middle
  // of the pane. The overture world is Y-up with the board in X-Z; layout is X-Y with
  // depth in Z, so the level axis becomes height.
  const float midX = (fi(v.dims.extent(0)) - 1.0f) * 0.5f;
  const float midZ = (fi(v.dims.extent(1)) - 1.0f) * 0.5f;
  const float midY = (fi(v.dims.extent(2)) - 1.0f) * cfg.depthSpacing * 0.5f;
  const auto toWorld = [&](const view::Placement& p) {
    return OvVec3{p.x - midX, p.z - midY, p.y - midZ};
  };

  const Pos dummy = flatBoard();  // unused: every token below carries its own placement
  for (const view::Placement& p : places) {
    const OvVec3 c = toWorld(p);
    constexpr float kHalf = 0.42f;
    OvQuad q;
    q.p[0] = {c.x - kHalf, c.y, c.z - kHalf};
    q.p[1] = {c.x + kHalf, c.y, c.z - kHalf};
    q.p[2] = {c.x + kHalf, c.y, c.z + kHalf};
    q.p[3] = {c.x - kHalf, c.y, c.z + kHalf};
    q.tone = (p.cell % 2) != 0 ? OvTone::Dark : OvTone::Light;
    q.fade = 1.0f;
    s.quads.push_back(q);
  }
  for (const StartPiece& sp : v.start) {
    if (sp.type == kNoPiece || sp.type >= v.pieces.size()) continue;
    const CellId cell = v.dims.toCell(sp.at);
    for (const view::Placement& p : places) {
      if (p.cell != cell) continue;
      TokenOpt o;
      o.at = true;
      o.where = toWorld(p);
      o.hasNormal = true;
      o.normal = {0.0f, 1.0f, 0.0f};
      o.fade = 1.0f;
      addToken(s, dummy, 0, 0, v.pieces[sp.type].symbol, sp.color == Color::White, o);
      break;
    }
  }

  for (OvTrail& tr : s.trails) tr.fade *= settle;
  for (OvBurst& b : s.bursts) b.fade *= settle;
  s.cam.yaw = lerpf(kOpenCam.yaw, s.cam.yaw, settle);
  s.cam.elev = lerpf(kOpenCam.elev, s.cam.elev, settle);
  s.cam.reach = lerpf(kOpenCam.reach, s.cam.reach, settle);
  s.cam.persp = lerpf(kOpenCam.persp, s.cam.persp, settle);

  s.caption = t < 0.12f  ? "the board every overture starts on"
              : t < 0.6f ? "laid out into its own lattice"
                         : "the shape this variant plays on";
  return s;
}

}  // namespace

OvVec3 overtureSurfaceAt(app::Overture which, float u, float v) {
  switch (which) {
    case app::Overture::Cylinder: {
      TubeOpt o;
      o.th = kTau;
      return tube(u, v, o);
    }
    case app::Overture::Torus:
    case app::Overture::AtomicTorus: {
      TubeOpt o;
      o.th = kTau;
      o.ph = kTau;
      o.open = 2.2f;
      return tube(u, v, o);
    }
    case app::Overture::Mobius:
      // The formed surface the active Mobius option settles into, so the seam-closure
      // test pins whichever one is compiled in.
      return kMobiusStrip ? stripSurface(u, v, kW * kMobiusStretch, kH / kMobiusStretch,
                                         kTau, 1.0f)
                          : band(u, v, kTau, 1.0f);
    case app::Overture::Klein:
      return kleinSurf(u, v, kTau, 1.0f, kTau, 1.0f, 2.05f);
    default:
      return flatBoard()(u, v);
  }
}

OvVec3 derivedSurfaceAt(app::SurfaceKind kind, float u, float v) {
  return derivedSurfaceAt(kind, u, v, SurfacePose{});
}

OvVec3 derivedSurfaceAt(app::SurfaceKind kind, float u, float v, SurfacePose pose) {
  const float e = std::clamp(pose.evert, 0.0f, 1.0f);
  switch (kind) {
    case app::SurfaceKind::Tube: {
      TubeOpt o;
      o.th = kTau;
      o.evert = e;
      return tube(u, v, o);
    }
    case app::SurfaceKind::Torus: {
      TubeOpt o;
      o.th = kTau;
      o.ph = kTau;
      // The ring radius is the surface's "width"; the cross-section is its thickness and
      // does not change with `openness`.
      o.open = 2.2f * pose.openness;
      o.evert = e;
      return tube(u, v, o);
    }
    case app::SurfaceKind::Band: {
      const float stretch = lerpf(1.0f, kMobiusStretch, clampf(pose.stretch, 0.0f, 1.0f));
      // `len` sets the ribbon's loop radius, so widening it opens the loop and leaves the
      // ribbon width (`kH / stretch`) alone.
      return kMobiusStrip ? stripSurface(u, v, kW * stretch * pose.openness, kH / stretch,
                                         kTau, 1.0f, e)
                          : band(u, v, kTau, 1.0f, e);
    }
    case app::SurfaceKind::Klein:
      return kleinSurf(u, v, kTau, 1.0f, kTau, pose.twist, 2.05f * pose.openness, e);
    case app::SurfaceKind::FlatGrid:
    case app::SurfaceKind::MirrorBox:
      return flatBoard()(u, v);
  }
  return flatBoard()(u, v);
}

bool hasPlaySurface(const VariantSpec& v) noexcept {
  const app::OvertureSignature sig = app::overtureSignature(v);
  // A glued two-dimensional board has a surface derived from its identifications.
  const bool glued =
      sig.surface == app::SurfaceKind::Tube || sig.surface == app::SurfaceKind::Torus ||
      sig.surface == app::SurfaceKind::Band || sig.surface == app::SurfaceKind::Klein;
  if (sig.dims == 2 && glued) return true;
  // Above two dimensions the shape is authored, not derived, so it is keyed by name
  // (M17.12): `torus3d`'s nested shells, `hyper4`'s tesseract. Anything else - `cube5`,
  // `t6` for now - has no play shape and stays on the ordinary lattice.
  OvVec3 ignored;
  return playShapePosition(v, 0, ignored);
}

bool playShapePosition(const VariantSpec& v, CellId cell, OvVec3& out) {
  const DimSpec& d = v.dims;
  if (d.dims() == 3 && v.name == "torus3d") {
    // The nested shells, at the point the library screen has fully rolled: two of the
    // three gluings close T^2 as the shell's surface, the level axis becomes the shell's
    // radius, and four levels come out as four shells about one core circle.
    const Coord c = d.toCoord(cell);
    out = shellTube((fi(c.c[0]) + 0.5f) / 4.0f, (fi(c.c[1]) + 0.5f) / 4.0f, kTau, kTau,
                    0.62f + fi(c.c[2]) * 0.82f, 10.5f);
    return true;
  }
  if (d.dims() == 4 && v.name == "hyper4") {
    // The tesseract: the 4-cube projected as two nested cubes joined corner to corner,
    // the aeon axis being the nesting. `spin` is fixed, so the play board does not turn
    // under the player the way the library's does.
    constexpr float kSpin = 0.9f;
    const Coord c = d.toCoord(cell);
    const float cx = fi(c.c[0]) - 1.5f;
    const float cy = fi(c.c[2]) - 1.5f;
    const float cz = fi(c.c[1]) - 1.5f;
    const float cw = fi(c.c[3]) - 1.5f;
    const float nx = cx * std::cos(kSpin) - cw * std::sin(kSpin);
    const float nw = cx * std::sin(kSpin) + cw * std::cos(kSpin);
    const float k = 2.35f / (3.5f - nw);
    out = OvVec3{nx * k * 2.15f, cy * k * 2.15f * 1.05f, cz * k * 2.15f};
    return true;
  }
  return false;
}

OvertureScene derivedOvertureScene(const VariantSpec& variant, float t,
                                   const view::Theme& th) {
  const app::OvertureSignature sig = app::overtureSignature(variant);
  // Above 2-D the surface catalogue does not apply - nothing embeds faithfully - so the
  // overture is the board's own lattice, extruded (M13.4). A temporal board keeps the
  // flat path: its unfilled boards are not a surface either way.
  if (sig.dims >= 3 && !sig.temporal) {
    return derivedGridOverture(variant, t, th);
  }
  OvertureScene s;
  const float form = ease(t);
  const float settle = ease(seg(t, 0.0f, 0.12f));

  // One arc for all of them: the camera starts on the shared opening pose and eases back
  // as the surface forms, so the hand-over in and out is the same for every variant
  // (M13).
  s.cam = {lerpf(kOpenCam.yaw, 0.62f, ease(t)), lerpf(kOpenCam.elev, 0.58f, ease(t)),
           lerpf(kOpenCam.reach, 7.4f, ease(t)), kOpenCam.persp};

  const Pos flat = flatBoard();
  const Pos formed = [sig](float u, float v) {
    return derivedSurfaceAt(sig.surface, u, v);
  };
  const Pos pos = [flat, formed, form](float u, float v) {
    return mix(flat(u, v), formed(u, v), form);
  };

  GridOpt g;
  g.sub = form > 0.02f ? 4 : 1;
  addGrid(s, pos, g);
  const float armyFade = 1.0f - ease(clampf(form * 1.4f, 0.0f, 1.0f));
  const bool alignedBoard =
      sig.dims == 2 && variant.dims.extent(0) == 8 && variant.dims.extent(1) == 8;
  if (alignedBoard) {
    // The variant's own army, not the standard one: a data-only variant opens on its own
    // start position, which is the point of showing *it* rather than the reference board.
    for (const StartPiece& sp : variant.start) {
      if (sp.type == kNoPiece || sp.type >= variant.pieces.size()) continue;
      if (sp.at.c[0] < 0 || sp.at.c[0] >= 8 || sp.at.c[1] < 0 || sp.at.c[1] >= 8) {
        continue;
      }
      const float u = (fi(sp.at.c[0]) + 0.5f) / 8.0f;
      const float v = (fi(sp.at.c[1]) + 0.5f) / 8.0f;
      TokenOpt o;
      o.at = true;
      o.where = pos(u, v);
      o.hasNormal = true;
      o.normal = normalAt(pos, u, v);
      // The variant's own army rides the forming surface; it does not step aside the way
      // the reference army does, because for this scene it *is* the army.
      o.fade = 1.0f;
      addToken(s, pos, 0, 0, variant.pieces[sp.type].symbol, sp.color == Color::White, o);
    }
  } else {
    // A board the 8x8 canvas cannot lay out exactly opens on the reference army, so the
    // hand-over in and out still reads.
    addArmy(s, pos, armyFade);
  }
  addRim(s, pos, th.rule, 1.4f, 0.5f + 0.4f * form);

  // The seams the surface closed, each periodic pair in one hue off the ramp: the two
  // ends of one identification share a colour, which is the grammar the hand-authored
  // scenes established.
  const bool filePeriodic =
      sig.surface == app::SurfaceKind::Tube || sig.surface == app::SurfaceKind::Band ||
      sig.surface == app::SurfaceKind::Torus || sig.surface == app::SurfaceKind::Klein;
  const bool rankPeriodic =
      sig.surface == app::SurfaceKind::Torus || sig.surface == app::SurfaceKind::Klein;
  if (filePeriodic) {
    const view::Rgba c = seamColour(th, 0, 2);
    for (const float e : {0.0005f, 0.9995f}) {
      OvTrail tr;
      tr.colour = c;
      tr.width = 3.0f;
      tr.fade = settle * form;
      for (int i = 0; i <= 16; ++i) tr.pts.push_back(pos(e, fi(i) / 16.0f));
      s.trails.push_back(tr);
    }
  }
  if (rankPeriodic) {
    const view::Rgba c = seamColour(th, 1, 2);
    for (const float e : {0.0005f, 0.9995f}) {
      OvTrail tr;
      tr.colour = c;
      tr.width = 3.0f;
      tr.fade = settle * form;
      for (int i = 0; i <= 16; ++i) tr.pts.push_back(pos(fi(i) / 16.0f, e));
      s.trails.push_back(tr);
    }
  }

  // A real move of a real piece, traced by the engine, so a derived scene shows the
  // variant's own route - a torus variant's rook wrapping, a Klein variant's bishop
  // coming back reversed - rather than a path the scene invented (M13.3).
  if (alignedBoard) {
    const Position startPos = Position::startPosition(variant);
    const std::optional<DemoMove> demo = demoMoveFor(variant, startPos);
    const float moveFade = settle * seg(t, 0.4f, 0.65f);
    if (demo.has_value() && !demo->path.steps.empty() && moveFade > 0.01f) {
      const auto point = [&](CellId cell) {
        const Coord cc = variant.dims.toCoord(cell);
        const float u = (fi(cc.c[0]) + 0.5f) / 8.0f;
        const float v = (fi(cc.c[1]) + 0.5f) / 8.0f;
        return add(pos(u, v), mul(normalAt(pos, u, v), 0.28f));
      };
      // One polyline per straight run, so the seam crossing is a gap rather than a chord
      // straight across the board.
      const auto emit = [&](std::vector<OvVec3>& seg) {
        if (seg.size() < 2) return;
        OvTrail tr;
        tr.colour = th.ember;
        tr.width = 2.8f;
        tr.fade = moveFade;
        tr.pts = seg;
        s.trails.push_back(tr);
      };
      std::vector<OvVec3> seg;
      seg.push_back(point(demo->path.from));
      for (const view::PathStep& st : demo->path.steps) {
        seg.push_back(point(st.to));
        if (st.kind != view::StepKind::Interior) {
          emit(seg);
          seg.clear();
          seg.push_back(point(st.to));
        }
      }
      emit(seg);
      TokenOpt o;
      o.at = true;
      o.where = point(demo->path.to);
      o.hasNormal = true;
      const Coord end = variant.dims.toCoord(demo->path.to);
      o.normal =
          normalAt(pos, (fi(end.c[0]) + 0.5f) / 8.0f, (fi(end.c[1]) + 0.5f) / 8.0f);
      o.fade = moveFade;
      addToken(s, pos, 0, 0, variant.pieces[demo->type].symbol,
               demo->color == Color::White, o);
    }
  }

  // Settle onto the shared opening pose and hide everything drawn on it, the same rule
  // the hand-authored scenes obey centrally.
  for (OvTrail& tr : s.trails) tr.fade *= settle;
  for (OvBurst& b : s.bursts) b.fade *= settle;
  s.cam.yaw = lerpf(kOpenCam.yaw, s.cam.yaw, settle);
  s.cam.elev = lerpf(kOpenCam.elev, s.cam.elev, settle);
  s.cam.reach = lerpf(kOpenCam.reach, s.cam.reach, settle);
  s.cam.persp = lerpf(kOpenCam.persp, s.cam.persp, settle);

  s.caption = t < 0.12f  ? "the board every overture starts on"
              : t < 0.5f ? "becoming the surface its geometry describes"
                         : "the shape this variant plays on";
  return s;
}

OvertureScene overtureScene(app::Overture which, float t, bool intro,
                            const view::Theme& th) {
  const float p = clampf(t, 0.0f, 1.0f);
  OvertureScene out;
  switch (which) {
    case app::Overture::None:
      return out;
    case app::Overture::Standard:
      out = sceneStandard(th, p, intro);
      break;
    case app::Overture::Cylinder:
      out = sceneCylinder(th, p);
      break;
    case app::Overture::Torus:
      out = sceneTorus(th, p);
      break;
    case app::Overture::Mobius:
      out = sceneMobius(th, p);
      break;
    case app::Overture::Klein:
      out = sceneKlein(th, p);
      break;
    case app::Overture::Mirrorbox:
      out = sceneMirrorbox(th, p);
      break;
    case app::Overture::Cube5:
      out = sceneCube5(th, p);
      break;
    case app::Overture::Hyper4:
      out = sceneHyper4(th, p);
      break;
    case app::Overture::Atomic:
      out = sceneAtomic(th, p);
      break;
    case app::Overture::AtomicTorus:
      out = sceneAtomicTorus(th, p);
      break;
    case app::Overture::MustCapture:
      out = sceneMustCapture(th, p);
      break;
    case app::Overture::Multiverse:
      out = sceneMultiverse(th, p);
      break;
    case app::Overture::Torus3d:
      out = sceneTorus3d(th, p);
      break;
    case app::Overture::T6:
      out = sceneT6(th, p);
      break;
  }
  // Settle the camera onto the shared opening pose as t reaches 0. Applied here rather
  // than left to each scene, because "every overture opens on the same picture" is a
  // property of the set, not of any one of them, and a scene cannot be trusted to
  // remember a rule that is about its neighbours.
  const float settle = ease(seg(p, 0.0f, 0.12f));
  // And nothing is drawn *on* that opening board. A seam rim, a board outline, a wall -
  // each is true of its own variant and of no other, so any of them present at t = 0
  // makes the shared picture not shared. Faded in centrally for the same reason the
  // camera is: it is a rule about the set, and a scene cannot be trusted to remember a
  // rule about its neighbours.
  for (OvTrail& tr : out.trails) tr.fade *= settle;
  for (OvBurst& b : out.bursts) b.fade *= settle;
  out.cam.yaw = lerpf(kOpenCam.yaw, out.cam.yaw, settle);
  out.cam.elev = lerpf(kOpenCam.elev, out.cam.elev, settle);
  out.cam.reach = lerpf(kOpenCam.reach, out.cam.reach, settle);
  out.cam.persp = lerpf(kOpenCam.persp, out.cam.persp, settle);
  return out;
}

// ===========================================================================
// Drawing.
//
// One sorted list, far to near. Separate passes per kind would let a piece on the far
// side of a torus draw over a cell on the near side, which is exactly the class of bug
// the lattice decoration had before it started sorting.
// ===========================================================================
namespace {

view::Rgba toneColour(const view::Theme& th, OvTone tone) {
  switch (tone) {
    case OvTone::Light:
      return th.boardLight;
    case OvTone::Dark:
      return th.boardDark;
    case OvTone::Lit:
      return th.ember;
    case OvTone::Scorch:
      return th.blood;
    case OvTone::Mirror:
      return th.mirrorEdge;
    // A wash rather than a hue: the cell is still itself, just out of play.
    case OvTone::Scrim:
      return th.light ? view::Rgba::hex(0xFFFFFF) : view::Rgba::hex(0x000000);
  }
  return th.boardLight;
}

view::Rgba shadeBy(view::Rgba c, float f) {
  c.r = clampf(c.r * f, 0.0f, 1.0f);
  c.g = clampf(c.g * f, 0.0f, 1.0f);
  c.b = clampf(c.b * f, 0.0f, 1.0f);
  return c;
}

/// Two-sided, because a glued board shows its underside constantly and an unlit face
/// there reads as a hole rather than as a surface.
float faceShade(const OvVec3 p[4]) {
  const OvVec3 n = normalise(cross(sub(p[1], p[0]), sub(p[2], p[0])));
  const OvVec3 light = normalise({-0.35f, 0.9f, -0.25f});
  const float d = std::abs(n.x * light.x + n.y * light.y + n.z * light.z);
  return 0.74f + 0.40f * d;
}

Archetype archetypeOf(char glyph) {
  switch (glyph) {
    case 'P':
      return Archetype::Dome;
    case 'R':
      return Archetype::Tower;
    case 'N':
      return Archetype::Wedge;
    case 'B':
      return Archetype::Spire;
    case 'Q':
      return Archetype::Crown;
    case 'K':
      return Archetype::Monolith;
    case 'U':
      return Archetype::Horn;
    default:
      return Archetype::Tower;
  }
}

/// One piece icon, filled then outlined. The outline is what keeps a pale piece visible
/// on a pale cell, and it has to be stroked after the fill or it is lost under it.
void drawIcon(ImDrawList* dl, IconStyle style, Archetype shape, ImVec2 centre, float size,
              bool mirrored, ImU32 fill, ImU32 line) {
  const PieceIcon art = pieceIcon(style, shape);
  const float k = size / 100.0f;
  const float sx = mirrored ? -k : k;
  for (const IconPoly& poly : art.fills) {
    if (poly.size() < 3) continue;
    dl->PathClear();
    for (const IconPoint& p : poly) {
      dl->PathLineTo(ImVec2(centre.x + (p.x - 50.0f) * sx, centre.y + (p.y - 50.0f) * k));
    }
    const ImVector<ImVec2> path = dl->_Path;
    widgets::fillPolygon(dl, dl->_Path.Data, dl->_Path.Size, fill);
    dl->_Path = path;
    dl->PathStroke(line, ImDrawFlags_Closed, std::max(1.0f, size * 0.022f));
  }
}

/// A dashed polyline. ImDrawList has no dash, and the w-edges of a hypercube and the
/// connectors between boards both need to read as "not really there".
void addDashed(ImDrawList* dl, const ImVec2* pts, int n, ImU32 col, float width) {
  constexpr float kOn = 5.0f;
  constexpr float kOff = 4.0f;
  float carried = 0.0f;
  bool on = true;
  for (int i = 0; i + 1 < n; ++i) {
    ImVec2 a = pts[i];
    const ImVec2 b = pts[i + 1];
    float len = std::hypot(b.x - a.x, b.y - a.y);
    if (len < 1e-4f) continue;
    const float ux = (b.x - a.x) / len;
    const float uy = (b.y - a.y) / len;
    while (len > 0.0f) {
      const float want = (on ? kOn : kOff) - carried;
      const float step = std::min(want, len);
      const ImVec2 e(a.x + ux * step, a.y + uy * step);
      if (on) dl->AddLine(a, e, col, width);
      a = e;
      len -= step;
      carried += step;
      if (carried >= (on ? kOn : kOff) - 1e-4f) {
        on = !on;
        carried = 0.0f;
      }
    }
  }
}

// The projection `drawOverture` uses, pulled out so picking can share it exactly. A pick
// that recomputed the framing its own way could disagree with the drawing at the pane's
// edges, which is the one bug M17.3 exists to prevent.
struct OvProjection {
  ImVec2 centre{};
  float scale{1.0f};
  float cy{1.0f}, sy{0.0f}, ce{1.0f}, se{0.0f}, persp{0.0f};
  bool valid{false};

  struct Shot {
    ImVec2 at;
    float depth{0};
    float k{1};
  };

  // Y is up and `elev` is the angle above the board, so the far rank is both higher on
  // screen and deeper in the sort - which is the pair of facts a flat drawing of a solid
  // has to keep consistent.
  [[nodiscard]] Shot raw(const OvVec3& p) const {
    const float x1 = p.x * cy + p.z * sy;
    const float z1 = -p.x * sy + p.z * cy;
    const float y2 = p.y * ce + z1 * se;
    const float z2 = -p.y * se + z1 * ce;
    const float k = 1.0f / std::max(0.2f, 1.0f + z2 * persp);
    return {ImVec2(x1 * k, -y2 * k), z2, k};
  }
  [[nodiscard]] Shot project(const OvVec3& p) const {
    const Shot sh = raw(p);
    return {ImVec2(centre.x + sh.at.x * scale, centre.y + sh.at.y * scale), sh.depth,
            sh.k};
  }
};

OvProjection makeOvProjection(const OvertureScene& scene, ImVec2 min, ImVec2 max,
                              float zoom) {
  OvProjection P;
  P.centre = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const float paneW = max.x - min.x;
  const float paneH = max.y - min.y;
  if (std::min(paneW, paneH) <= 1.0f) return P;
  P.cy = std::cos(scene.cam.yaw);
  P.sy = std::sin(scene.cam.yaw);
  P.ce = std::cos(scene.cam.elev);
  P.se = std::sin(scene.cam.elev);
  P.persp = scene.cam.persp;

  // Fit by measuring, not by guessing a constant - the lesson the lattice decoration
  // already learned. Measured on the cells only: a flung piece must not shrink the board.
  float extX = 1e-3f;
  float extY = 1e-3f;
  for (const OvQuad& q : scene.quads) {
    for (const OvVec3& p : q.p) {
      const OvProjection::Shot sh = P.raw(p);
      extX = std::max(extX, std::abs(sh.at.x));
      extY = std::max(extY, std::abs(sh.at.y));
    }
  }
  constexpr float kFill = 0.40f * 0.70f;
  P.scale = std::min(paneW * kFill / extX, paneH * kFill / extY) * zoom;
  P.valid = true;
  return P;
}

}  // namespace

void drawOverture(ImDrawList* dl, const OvertureScene& scene, ImVec2 min, ImVec2 max,
                  const view::Theme& theme, IconStyle iconStyle, float zoom,
                  float alpha) {
  // An overture on its way out must leave no geometry behind, the same contract every
  // decoration keeps.
  if (alpha <= 0.001f) return;

  const OvProjection P = makeOvProjection(scene, min, max, zoom);
  if (!P.valid) return;
  const float scale = P.scale;
  using Shot = OvProjection::Shot;
  const auto project = [&](const OvVec3& p) -> Shot { return P.project(p); };

  enum class Kind : std::uint8_t { Quad, Token, Trail, Burst };
  struct Item {
    float depth{0};
    Kind kind{Kind::Quad};
    int index{0};
  };
  std::vector<Item> items;
  items.reserve(scene.quads.size() + scene.tokens.size() + scene.trails.size() +
                scene.bursts.size());

  std::vector<ImVec2> quadPts(scene.quads.size() * 4);
  for (std::size_t i = 0; i < scene.quads.size(); ++i) {
    float d = 0.0f;
    for (int j = 0; j < 4; ++j) {
      const Shot sh = project(scene.quads[i].p[j]);
      quadPts[i * 4 + static_cast<std::size_t>(j)] = sh.at;
      d += sh.depth;
    }
    items.push_back({d * 0.25f, Kind::Quad, static_cast<int>(i)});
  }
  struct TokenShot {
    ImVec2 at;
    float size{0};
  };
  std::vector<TokenShot> tokenShots(scene.tokens.size());
  std::vector<Item> tokenItems;
  tokenItems.reserve(scene.tokens.size());
  for (std::size_t i = 0; i < scene.tokens.size(); ++i) {
    const OvToken& tk = scene.tokens[i];
    // Centred on the cell it stands on, not floated above it: a piece and its square
    // are the same square, and an icon nudged along the normal reads as hovering - and
    // on a steeply-seen board it drifts onto the neighbouring rank entirely.
    const Shot base = project(tk.at);
    tokenShots[i] = {base.at, std::max(4.0f, scale * base.k * 0.95f * tk.height)};
    tokenItems.push_back({base.depth, Kind::Token, static_cast<int>(i)});
  }
  std::vector<std::vector<ImVec2>> trailPts(scene.trails.size());
  for (std::size_t i = 0; i < scene.trails.size(); ++i) {
    float d = 1e9f;
    trailPts[i].reserve(scene.trails[i].pts.size());
    for (const OvVec3& p : scene.trails[i].pts) {
      const Shot sh = project(p);
      trailPts[i].push_back(sh.at);
      d = std::min(d, sh.depth);
    }
    items.push_back({d - 0.03f, Kind::Trail, static_cast<int>(i)});
  }
  std::vector<Shot> burstShots(scene.bursts.size());
  for (std::size_t i = 0; i < scene.bursts.size(); ++i) {
    burstShots[i] = project(scene.bursts[i].at);
    items.push_back({burstShots[i].depth - 0.04f, Kind::Burst, static_cast<int>(i)});
  }

  // Surfaces first, then pieces. Depth alone is not enough: a cell of the rank in front
  // is nearer than the piece behind it and would be drawn over its base, so a piece
  // ends up sliced by the board it is standing on. Pieces are the subject, so they go
  // on top of every surface and are sorted only against each other.
  std::sort(items.begin(), items.end(),
            [](const Item& a, const Item& b) { return a.depth > b.depth; });
  std::sort(tokenItems.begin(), tokenItems.end(),
            [](const Item& a, const Item& b) { return a.depth > b.depth; });
  items.insert(items.end(), tokenItems.begin(), tokenItems.end());

  // Cells are drawn without anti-aliased fill. A curved surface subdivides each cell so
  // it can bend, and an anti-aliased edge on every sub-quad draws a faint seam inside
  // the cell - a grid the board does not have, and a difference between a subdivided
  // surface and a flat one that should not exist. Restored before returning: the flag
  // belongs to the whole draw list, not to this object.
  const ImDrawListFlags savedFlags = dl->Flags;
  dl->Flags &= ~static_cast<ImDrawListFlags>(ImDrawListFlags_AntiAliasedFill);

  for (const Item& it : items) {
    const auto i = static_cast<std::size_t>(it.index);
    switch (it.kind) {
      case Kind::Quad: {
        const OvQuad& q = scene.quads[i];
        const bool flatFill =
            q.tone == OvTone::Scrim || q.tone == OvTone::Mirror || q.hasColour;
        const view::Rgba base = q.hasColour ? q.colour : toneColour(theme, q.tone);
        const view::Rgba col = flatFill ? base : shadeBy(base, faceShade(q.p));
        const ImVec2* p = &quadPts[i * 4];
        // Cells are drawn through. A closed surface hides half of itself, and the half
        // it hides is usually the half that explains the gluing - the far side of a
        // torus, the cells a Klein bottle passes through itself to reach. Sorting back
        // to front means the blend is right; the transparency is what makes the shape
        // legible rather than a silhouette.
        const float sheer = flatFill ? 1.0f : 0.72f;
        dl->AddQuadFilled(p[0], p[1], p[2], p[3],
                          widgets::u32(col, q.fade * alpha * sheer));
        if (q.tone == OvTone::Mirror) {
          // A mirror gets no hue at all - there is nothing on the other side of it - so
          // it is drawn as silvered metal, with a bright edge to say it is a surface.
          dl->AddQuad(p[0], p[1], p[2], p[3],
                      widgets::u32(theme.mirrorEdge, 0.8f * q.fade * alpha), 1.4f);
        }
        break;
      }
      case Kind::Token: {
        const OvToken& tk = scene.tokens[i];
        // Same rule as the flat board: the figure in its own colour, on a token that is
        // the same for both sides.
        const view::Rgba fill = tk.white ? theme.whitePiece : theme.blackPiece;
        const view::Rgba line = theme.pieceToken;
        drawIcon(dl, iconStyle, archetypeOf(tk.glyph), tokenShots[i].at,
                 tokenShots[i].size, tk.mirrored,
                 widgets::u32(fill, 0.97f * tk.fade * alpha),
                 widgets::u32(line, 0.55f * tk.fade * alpha));
        break;
      }
      case Kind::Trail: {
        const OvTrail& tr = scene.trails[i];
        if (trailPts[i].size() < 2) break;
        const ImU32 col = widgets::u32(tr.colour, tr.fade * alpha);
        if (tr.dashed) {
          addDashed(dl, trailPts[i].data(), static_cast<int>(trailPts[i].size()), col,
                    tr.width);
        } else {
          dl->AddPolyline(trailPts[i].data(), static_cast<int>(trailPts[i].size()), col,
                          ImDrawFlags_None, tr.width);
        }
        break;
      }
      case Kind::Burst: {
        const OvBurst& b = scene.bursts[i];
        const float r = std::max(2.0f, b.radius * burstShots[i].k * scale);
        const ImU32 col = widgets::u32(b.colour, b.fade * alpha);
        const ImVec2 at = burstShots[i].at;
        if (b.cross) {
          dl->AddLine(ImVec2(at.x - r, at.y - r), ImVec2(at.x + r, at.y + r), col, 2.4f);
          dl->AddLine(ImVec2(at.x + r, at.y - r), ImVec2(at.x - r, at.y + r), col, 2.4f);
        } else if (r >= 0.5f) {
          // An explicit segment count, never ImGui's automatic one: that path divides
          // by a tessellation table which is zero-filled until NewFrame has run, so a
          // headless draw list divides by zero. Stating the count also keeps the vertex
          // count of a given frame fixed, which is what a reproducible screenshot needs.
          const int segs = std::clamp(12 + static_cast<int>(r * 0.4f), 12, 64);
          dl->AddCircle(at, r, col, segs, std::max(1.2f, 3.0f - b.radius * 0.2f));
        }
        break;
      }
    }
  }
  dl->Flags = savedFlags;
}

}  // namespace cb::render
