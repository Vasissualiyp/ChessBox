// SPDX-License-Identifier: GPL-3.0-or-later
// Sound is generated, not loaded (M18.5), so its correctness is arithmetic rather than
// sample playback: the buffers must be the right length, deterministic, loud enough to
// hear and quiet enough not to clip, and "off" must mean silence. The hook points are
// exercised through a recorder that stands in for the audio device, so none of this needs
// a sound card - the same discipline the renderer's own tests use for a missing font.
#include "audio/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "app/session.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

const std::vector<audio::Sound> kAll{audio::Sound::Click, audio::Sound::Move,
                                     audio::Sound::Capture, audio::Sound::Check};

int peak(const std::vector<std::int16_t>& pcm) {
  int p = 0;
  for (const std::int16_t s : pcm) p = std::max(p, std::abs(static_cast<int>(s)));
  return p;
}

double rms(const std::vector<std::int16_t>& pcm) {
  if (pcm.empty()) return 0.0;
  double sum = 0.0;
  for (const std::int16_t s : pcm) sum += static_cast<double>(s) * s;
  return std::sqrt(sum / static_cast<double>(pcm.size()));
}

/// A sink that remembers what it was asked to play, standing in for the device.
struct Recorder final : audio::Sink {
  std::vector<audio::Sound> played;
  void play(audio::Sound s) override { played.push_back(s); }
};

/// Install a sink for the duration of a test and put the previous one back. The global is
/// how the (device-free) app sees the audio layer, so every test that touches it must
/// leave it as it found it.
struct ScopedSink {
  explicit ScopedSink(audio::Sink* s) : previous_(audio::sink()) { audio::setSink(s); }
  ~ScopedSink() { audio::setSink(previous_); }
  audio::Sink* previous_;
};

}  // namespace

TEST_CASE("every generated sound is correctly sized, audible and non-clipping",
          "[unit][audio]") {
  for (const audio::Sound s : kAll) {
    CAPTURE(static_cast<int>(s));
    const std::vector<std::int16_t> pcm = audio::synthesize(s);
    // Short by construction: an effects bus, not a score. The spec's own ceiling is 200
    // ms.
    REQUIRE(pcm.size() == static_cast<std::size_t>(audio::sampleCount(s)));
    REQUIRE(!pcm.empty());
    REQUIRE(pcm.size() < static_cast<std::size_t>(audio::kSampleRate / 5 + 1));
    // Audible but with headroom: the synthesis normalises to a target peak well below
    // full scale, so no sum of components can clip.
    REQUIRE(peak(pcm) > 6000);
    REQUIRE(peak(pcm) <= 24000);
    REQUIRE(rms(pcm) > 400.0);
  }
}

TEST_CASE("generated sound is deterministic and the four sounds differ",
          "[unit][audio]") {
  for (const audio::Sound s : kAll) {
    REQUIRE(audio::synthesize(s) == audio::synthesize(s));
    // The cached set is the same series the synthesis produces.
    REQUIRE(audio::samples(s) == audio::synthesize(s));
  }
  for (std::size_t i = 0; i < kAll.size(); ++i) {
    for (std::size_t j = i + 1; j < kAll.size(); ++j) {
      REQUIRE(audio::synthesize(kAll[i]) != audio::synthesize(kAll[j]));
    }
  }
}

TEST_CASE("zero volume is silence, not a quieter buffer", "[unit][audio]") {
  const std::vector<std::int16_t>& pcm = audio::samples(audio::Sound::Move);
  const auto silence = [](const std::vector<std::int16_t>& v) {
    return std::all_of(v.begin(), v.end(), [](std::int16_t s) { return s == 0; });
  };

  // Either slider at zero mutes the bus.
  REQUIRE(audio::effectsGain(0.0f, 0.9f) == 0.0f);
  REQUIRE(audio::effectsGain(0.8f, 0.0f) == 0.0f);
  REQUIRE(silence(audio::applyGain(pcm, audio::effectsGain(0.0f, 0.9f))));
  REQUIRE(silence(audio::applyGain(pcm, audio::effectsGain(0.8f, 0.0f))));
  REQUIRE(silence(audio::applyGain(pcm, 0.0f)));
  // A negative gain - which a hand-edited file could produce before sanitize - is off
  // too.
  REQUIRE(silence(audio::applyGain(pcm, -0.5f)));
  // But the buffer is still the same length, so a caller is never surprised by its shape.
  REQUIRE(audio::applyGain(pcm, 0.0f).size() == pcm.size());

  // Full gain is identity; the mixing is linear in between.
  REQUIRE(audio::applyGain(pcm, 1.0f) == pcm);
  REQUIRE(std::abs(audio::effectsGain(0.5f, 0.5f) - 0.25f) < 1e-6f);

  const auto half = audio::applyGain(pcm, 0.5f);
  REQUIRE(half.size() == pcm.size());
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    REQUIRE(std::abs(static_cast<int>(half[i]) - static_cast<int>(pcm[i]) / 2) <= 1);
  }
}

TEST_CASE("play with no sink is a silent no-op, and with one it forwards",
          "[unit][audio]") {
  // No sink is the state every capture and every unit test runs in: it must not crash.
  audio::setSink(nullptr);
  REQUIRE(audio::sink() == nullptr);
  audio::play(audio::Sound::Click);

  Recorder recorder;
  ScopedSink guard(&recorder);
  REQUIRE(audio::sink() == &recorder);
  for (const audio::Sound s : kAll) audio::play(s);
  REQUIRE(recorder.played == kAll);
}

TEST_CASE("playing a move, a capture and a check announces each sound", "[unit][audio]") {
  Recorder recorder;
  ScopedSink guard(&recorder);
  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session.has_value());

  SECTION("an ordinary move plays the landing sound") {
    REQUIRE((*session)->applyScript("click e2\nclick e4").has_value());
    REQUIRE(recorder.played == std::vector<audio::Sound>{audio::Sound::Move});
  }

  SECTION("a capture plays the capture sound, not the landing one") {
    REQUIRE(
        (*session)
            ->applyScript("click e2\nclick e4\nclick d7\nclick d5\nclick e4\nclick d5")
            .has_value());
    REQUIRE(recorder.played.size() == 3);
    REQUIRE(recorder.played.back() == audio::Sound::Capture);
    REQUIRE(recorder.played[0] == audio::Sound::Move);
    REQUIRE(recorder.played[1] == audio::Sound::Move);
  }

  SECTION("a move that gives check adds the check sound") {
    // A bare king and rook: Rh1-h8 lands on the back rank and checks the black king.
    REQUIRE((*session)->loadFen("4k3/8/8/8/8/8/8/4K2R w - - 0 1").has_value());
    REQUIRE((*session)->applyScript("click h1\nclick h8").has_value());
    REQUIRE(recorder.played ==
            std::vector<audio::Sound>{audio::Sound::Move, audio::Sound::Check});
  }
}
