// telemetry_ingestor.cpp — frames from a source to the recorder and the renderer's snapshot.
#include "app/telemetry_ingestor.hpp"
#include "telemetry/frame.hpp"
#include "telemetry/freshness.hpp"
#include "telemetry/sensor_source.hpp"
#include "telemetry/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

using c3::app::TelemetryIngestor;
using c3::telemetry::Frame;
using c3::telemetry::kMaxStallTimeoutMs;
using c3::telemetry::LinkState;
using c3::telemetry::SourceHealth;
using c3::telemetry::TelemetryView;

CATCH_REGISTER_ENUM(LinkState, LinkState::NoData, LinkState::Live, LinkState::Stalled, LinkState::Failed)

namespace {

// The ingestor takes a plain function pointer for its clock, so the fake time is file-scope
// state that each test case sets before polling. Catch2 runs cases one at a time.
std::uint64_t g_now_ms = 0;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

[[nodiscard]] std::uint64_t fake_now() noexcept {
  return g_now_ms;
}

/// A source that hands out queued frames and reports whatever link state it is told to.
class FakeSource final : public c3::telemetry::SensorSource {
public:
  std::deque<Frame> frames;
  LinkState state = LinkState::Live;
  int polls = 0;

  [[nodiscard]] std::optional<Frame> poll() noexcept override {
    ++polls;
    if (frames.empty()) {
      return std::nullopt;
    }
    const Frame f = frames.front();
    frames.pop_front();
    return f;
  }
  [[nodiscard]] SourceHealth health() const noexcept override { return {.state = state}; }

  void queue(std::uint32_t t_ms) { frames.push_back(Frame{.t_ms = t_ms, .fuel_fraction = 0.5F}); }
};

/// Collects what pump() hands to the recorder.
struct Recorder {
  std::vector<std::uint32_t> seen;
  void operator()(const Frame& f) { seen.push_back(f.t_ms); }
};

constexpr std::uint32_t kTimeout = 1000;

struct Rig {
  FakeSource source;
  TelemetryIngestor ingestor{source, &fake_now};
  Recorder recorder;

  Rig() { g_now_ms = 10'000; }
  void pump() {
    ingestor.pump([this](const Frame& f) { recorder(f); });
  }
};

TEST_CASE("TelemetryIngestor: before any frame there is nothing to show and no data", "[telemetry_ingestor]") {
  Rig rig;
  CHECK_FALSE(rig.ingestor.update());
  const TelemetryView v = rig.ingestor.view(kTimeout);
  CHECK(v.frame == nullptr);
  CHECK(v.link == LinkState::NoData);
  CHECK(v.age_ms == 0);
  CHECK_FALSE(v.live());
}

TEST_CASE("TelemetryIngestor: pump with nothing queued polls once and records nothing", "[telemetry_ingestor]") {
  Rig rig;
  rig.pump();
  CHECK(rig.source.polls == 1);
  CHECK(rig.recorder.seen.empty());
  CHECK_FALSE(rig.ingestor.update());
}

TEST_CASE("TelemetryIngestor: one frame reaches the recorder and, after update, the view", "[telemetry_ingestor]") {
  Rig rig;
  rig.source.queue(42);
  rig.pump();
  CHECK(rig.recorder.seen == std::vector<std::uint32_t>{42});

  // not visible until the consumer side adopts it
  CHECK(rig.ingestor.view(kTimeout).frame == nullptr);
  CHECK(rig.ingestor.update());
  const TelemetryView v = rig.ingestor.view(kTimeout);
  REQUIRE(v.frame != nullptr);
  CHECK(v.frame->t_ms == 42);
  CHECK(v.frame->fuel_fraction == 0.5F);
  CHECK(v.link == LinkState::Live);
  CHECK(v.age_ms == 0);
  CHECK(v.live());
}

TEST_CASE("TelemetryIngestor: the recorder sees every frame; the view sees only the newest", "[telemetry_ingestor]") {
  Rig rig;
  for (std::uint32_t t : {1, 2, 3}) {
    rig.source.queue(t);
  }
  rig.pump();
  CHECK(rig.recorder.seen == std::vector<std::uint32_t>{1, 2, 3});
  CHECK(rig.ingestor.update());
  REQUIRE(rig.ingestor.view(kTimeout).frame != nullptr);
  CHECK(rig.ingestor.view(kTimeout).frame->t_ms == 3);
  CHECK_FALSE(rig.ingestor.update());  // nothing newer
}

TEST_CASE("TelemetryIngestor: a pump drains at most kMaxFramesPerPump frames", "[telemetry_ingestor]") {
  Rig rig;
  STATIC_CHECK(TelemetryIngestor::kMaxFramesPerPump == 32);
  for (std::uint32_t t = 0; t < 40; ++t) {
    rig.source.queue(t);
  }
  rig.pump();
  CHECK(rig.recorder.seen.size() == 32);
  CHECK(rig.source.frames.size() == 8);
  CHECK(rig.source.polls == 32);  // stopped by the bound, not by an empty poll
  rig.pump();
  CHECK(rig.recorder.seen.size() == 40);
  CHECK(rig.recorder.seen.back() == 39);
  CHECK(rig.source.frames.empty());
}

TEST_CASE("TelemetryIngestor: update keeps the previous frame when nothing new arrived", "[telemetry_ingestor]") {
  Rig rig;
  rig.source.queue(7);
  rig.pump();
  REQUIRE(rig.ingestor.update());
  const Frame* const first = rig.ingestor.view(kTimeout).frame;
  CHECK_FALSE(rig.ingestor.update());
  const TelemetryView v = rig.ingestor.view(kTimeout);
  CHECK(v.frame == first);  // storage inside the ingestor, stable across views
  CHECK(v.frame->t_ms == 7);
}

TEST_CASE("TelemetryIngestor: frames are stamped with the clock at pump time, not their MCU time",
          "[telemetry_ingestor]") {
  Rig rig;
  rig.source.queue(1);  // t_ms = 1 ms since MCU boot: meaningless as an arrival time
  g_now_ms = 50'000;
  rig.pump();
  REQUIRE(rig.ingestor.update());
  g_now_ms = 50'300;
  const TelemetryView v = rig.ingestor.view(kTimeout);
  CHECK(v.age_ms == 300);
  CHECK(v.link == LinkState::Live);
}

TEST_CASE("TelemetryIngestor: the link stalls once the frame is as old as the timeout", "[telemetry_ingestor]") {
  Rig rig;
  rig.source.queue(1);
  rig.pump();
  REQUIRE(rig.ingestor.update());

  g_now_ms = 10'000 + kTimeout - 1;
  CHECK(rig.ingestor.view(kTimeout).link == LinkState::Live);
  g_now_ms = 10'000 + kTimeout;
  const TelemetryView stalled = rig.ingestor.view(kTimeout);
  CHECK(stalled.link == LinkState::Stalled);
  CHECK(stalled.age_ms == kTimeout);
  REQUIRE(stalled.frame != nullptr);  // the stale frame is still offered, flagged as stale
  CHECK_FALSE(stalled.live());
}

TEST_CASE("TelemetryIngestor: the timeout is capped at the freshness limit (safety invariant 4)",
          "[telemetry_ingestor]") {
  Rig rig;
  rig.source.queue(1);
  rig.pump();
  REQUIRE(rig.ingestor.update());
  g_now_ms = 10'000 + kMaxStallTimeoutMs;
  CHECK(rig.ingestor.view(60'000).link == LinkState::Stalled);  // a generous timeout does not help
  g_now_ms = 10'000 + 250;
  CHECK(rig.ingestor.view(200).link == LinkState::Stalled);  // a tight one is honoured
  CHECK(rig.ingestor.view(300).link == LinkState::Live);
}

TEST_CASE("TelemetryIngestor: a new frame restores the link", "[telemetry_ingestor]") {
  Rig rig;
  rig.source.queue(1);
  rig.pump();
  REQUIRE(rig.ingestor.update());
  g_now_ms = 20'000;
  CHECK(rig.ingestor.view(kTimeout).link == LinkState::Stalled);
  rig.source.queue(2);
  rig.pump();
  CHECK(rig.ingestor.view(kTimeout).link == LinkState::Stalled);  // not until update()
  CHECK(rig.ingestor.update());
  const TelemetryView v = rig.ingestor.view(kTimeout);
  CHECK(v.link == LinkState::Live);
  CHECK(v.frame->t_ms == 2);
  CHECK(v.age_ms == 0);
}

TEST_CASE("TelemetryIngestor: a failed source reports Failed, with or without a frame", "[telemetry_ingestor]") {
  Rig rig;
  rig.source.state = LinkState::Failed;
  CHECK(rig.ingestor.view(kTimeout).link == LinkState::Failed);
  CHECK(rig.ingestor.view(kTimeout).frame == nullptr);

  rig.source.queue(1);
  rig.pump();
  REQUIRE(rig.ingestor.update());
  const TelemetryView v = rig.ingestor.view(kTimeout);
  CHECK(v.link == LinkState::Failed);  // even though the frame is fresh
  REQUIRE(v.frame != nullptr);
  CHECK_FALSE(v.live());
  rig.source.state = LinkState::Live;
  CHECK(rig.ingestor.view(kTimeout).link == LinkState::Live);  // the source's word is read each time
}

TEST_CASE("TelemetryIngestor: a clock that steps backwards reads as stale, never as fresh", "[telemetry_ingestor]") {
  Rig rig;
  rig.source.queue(1);
  rig.pump();
  REQUIRE(rig.ingestor.update());
  g_now_ms = 9'000;
  const TelemetryView v = rig.ingestor.view(kTimeout);
  CHECK(v.link == LinkState::Stalled);
  CHECK(v.age_ms == kMaxStallTimeoutMs);
}

TEST_CASE("TelemetryIngestor: pump accepts any callable taking a const Frame&", "[telemetry_ingestor]") {
  Rig rig;
  rig.source.queue(5);
  int calls = 0;
  rig.ingestor.pump([&calls](const Frame& f) {
    ++calls;
    CHECK(f.t_ms == 5);
  });
  CHECK(calls == 1);
  rig.ingestor.pump([](const Frame&) {});  // a no-op recorder, as in a run without --record
  CHECK(rig.ingestor.update());
}

}  // namespace
