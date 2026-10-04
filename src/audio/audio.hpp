// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

/// Sound, and nothing else.
///
/// A v1 with no asset files at all (M18.5): every effect is procedurally synthesised into
/// a static 16-bit buffer the first time it is asked for, then played through SDL3's own
/// audio device stream. There is deliberately no music and no streaming - a good loop
/// needs a sourced or composed asset and a licensing decision, neither of which belongs
/// to a generator.
///
/// The layer depends only on `base` and sits below `app`/`render`/`gui`, which all call
/// `play`. It touches SDL only in the `.cpp`, so a build without SDL3 still compiles and
/// simply stays silent - the same "a missing thing is not fatal" rule the renderer uses
/// for an absent font.
namespace cb::audio {

/// The sounds the game can play. Each is a short placeholder tone, tuned for legibility
/// over character - this is "something plays instead of nothing", not sound design.
enum class Sound : std::uint8_t {
  Click,    ///< a UI control was activated
  Move,     ///< a piece landed (the ordinary move)
  Capture,  ///< a piece was captured: lower and sharper than a landing
  Check,    ///< the side to move is now in check
};

/// The rate every synthesised buffer uses. Fixed so a test can pin buffer sizes exactly;
/// SDL resamples from it to whatever the device wants.
inline constexpr int kSampleRate = 48000;

/// How long each sound is, in seconds. All short - this is an effects bus, not a score.
[[nodiscard]] float duration(Sound s) noexcept;

/// The number of mono samples `synthesize(s)` returns.
[[nodiscard]] int sampleCount(Sound s) noexcept;

/// 16-bit mono PCM for `s`. Deterministic: the same call always returns the same bytes,
/// with the noise component drawn from a fixed-seed `Rng` and every buffer normalised to
/// its intended peak so nothing clips.
[[nodiscard]] std::vector<std::int16_t> synthesize(Sound s);

/// `synthesize(s)`, generated once and cached. Handed out by reference because playing a
/// sound must not copy a buffer every frame.
[[nodiscard]] const std::vector<std::int16_t>& samples(Sound s);

/// Scale `pcm` by `gain` in [0, 1]. A gain of zero or less returns an all-zero buffer of
/// the same length: "off" means silence, not merely a quieter noise floor.
[[nodiscard]] std::vector<std::int16_t> applyGain(const std::vector<std::int16_t>& pcm,
                                                  float gain);

/// The effects bus gain: `master * effects`, each clamped to [0, 1]. Music is absent on
/// purpose - v1 has no music, so `volumeMusic` is read by the settings screen and has
/// nothing to multiply yet. Adding it later is one more factor here.
[[nodiscard]] float effectsGain(float master, float effects) noexcept;

/// Something that can receive a sound. The device-backed `Mixer` is the real one; a test
/// installs a recorder to observe what the game asked to play without opening a device.
class Sink {
 public:
  virtual ~Sink() = default;
  /// Play `s`. An implementation must be a no-op when it cannot - never fatal.
  virtual void play(Sound s) = 0;
};

/// The installed output, or null. Null means "silent": every `play` is a no-op, which is
/// the whole of the headless/capture guarantee. The front end installs a `Mixer` only on
/// the interactive path; `--shot`/`--clip` never do, so a capture cannot require a
/// device.
void setSink(Sink* sink) noexcept;
[[nodiscard]] Sink* sink() noexcept;

/// Play `s` through the installed sink, if there is one.
void play(Sound s) noexcept;

/// Owns SDL3's audio device and its stream. `create` returns nullptr and leaves a note on
/// stderr when there is no usable device or when the build has no SDL3 - a missing sound
/// card must never stop the game starting, exactly as a missing font must not.
class Mixer final : public Sink {
 public:
  [[nodiscard]] static std::unique_ptr<Mixer> create();
  ~Mixer() override;
  Mixer(const Mixer&) = delete;
  Mixer& operator=(const Mixer&) = delete;

  /// Set the effects gain from the player's sliders. Music is not a parameter because
  /// there is no music; the caller still reads `volumeMusic` and has nothing to pass it
  /// to.
  void setVolumes(float master, float effects) noexcept;
  [[nodiscard]] float gain() const noexcept { return gain_; }

  /// Queue `s`. Does nothing at gain zero, or when the output was closed.
  void play(Sound s) override;

 private:
  Mixer();
  struct Impl;
  std::unique_ptr<Impl> impl_;
  float gain_{1.0f};
};

}  // namespace cb::audio
