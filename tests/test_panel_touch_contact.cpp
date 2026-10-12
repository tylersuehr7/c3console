#include "platform/input_source.hpp"
#include "platform/panel_touch_contact.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include <optional>
#include <type_traits>

using c3::platform::PanelTouchContact;
using c3::platform::TouchEvent;
using c3::platform::TouchPhase;

CATCH_REGISTER_ENUM(TouchPhase, TouchPhase::Down, TouchPhase::Move, TouchPhase::Up, TouchPhase::Cancel)

namespace {

void expect_event(const std::optional<TouchEvent>& e, TouchPhase phase, std::int32_t id, float x, float y) {
  REQUIRE(e.has_value());
  CHECK(e->phase == phase);
  CHECK(e->id == id);
  CHECK(e->x == x);
  CHECK(e->y == y);
}

// ---------------------------------------------------------------------------
// Type
// ---------------------------------------------------------------------------

TEST_CASE("PanelTouchContact: is a small value type with noexcept operations", "[panel_touch_contact]") {
  STATIC_CHECK(std::is_nothrow_default_constructible_v<PanelTouchContact>);
  STATIC_CHECK(std::is_nothrow_copy_constructible_v<PanelTouchContact>);
  PanelTouchContact contact;
  STATIC_CHECK(noexcept(contact.update(TouchPhase::Down, 1, 0.0F, 0.0F)));
  STATIC_CHECK(noexcept(contact.cancel()));
}

// ---------------------------------------------------------------------------
// A single finger's life cycle
// ---------------------------------------------------------------------------

TEST_CASE("update: Down on an idle contact is delivered with its coordinates", "[panel_touch_contact]") {
  PanelTouchContact contact;
  expect_event(contact.update(TouchPhase::Down, 7, 100.0F, 200.0F), TouchPhase::Down, 7, 100.0F, 200.0F);
}

TEST_CASE("update: Move on the active finger is delivered with the new coordinates", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 7, 100.0F, 200.0F).has_value());
  expect_event(contact.update(TouchPhase::Move, 7, 110.0F, 205.0F), TouchPhase::Move, 7, 110.0F, 205.0F);
  expect_event(contact.update(TouchPhase::Move, 7, 150.0F, 260.0F), TouchPhase::Move, 7, 150.0F, 260.0F);
}

TEST_CASE("update: Up is stamped with the last known position, ignoring any coordinates passed",
          "[panel_touch_contact]") {
  // libinput releases carry no coordinates: whatever the source passes must not leak through.
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 7, 100.0F, 200.0F).has_value());
  REQUIRE(contact.update(TouchPhase::Move, 7, 150.0F, 260.0F).has_value());
  expect_event(contact.update(TouchPhase::Up, 7, 999.0F, 999.0F), TouchPhase::Up, 7, 150.0F, 260.0F);
}

TEST_CASE("update: Up with the default arguments uses the last known position", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 7, 100.0F, 200.0F).has_value());
  REQUIRE(contact.update(TouchPhase::Move, 7, 150.0F, 260.0F).has_value());
  expect_event(contact.update(TouchPhase::Up, 7), TouchPhase::Up, 7, 150.0F, 260.0F);
}

TEST_CASE("update: a tap with no Move releases where it landed", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 3, 42.0F, 84.0F).has_value());
  expect_event(contact.update(TouchPhase::Up, 3), TouchPhase::Up, 3, 42.0F, 84.0F);
}

TEST_CASE("update: Cancel behaves like Up, stamped with the last position", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 7, 100.0F, 200.0F).has_value());
  REQUIRE(contact.update(TouchPhase::Move, 7, 120.0F, 220.0F).has_value());
  expect_event(contact.update(TouchPhase::Cancel, 7, -1.0F, -1.0F), TouchPhase::Cancel, 7, 120.0F, 220.0F);
}

TEST_CASE("update: after Up the contact is idle and a new Down of any id is accepted", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 7, 1.0F, 2.0F).has_value());
  REQUIRE(contact.update(TouchPhase::Up, 7).has_value());
  expect_event(contact.update(TouchPhase::Down, 8, 3.0F, 4.0F), TouchPhase::Down, 8, 3.0F, 4.0F);
  REQUIRE(contact.update(TouchPhase::Up, 8).has_value());
  expect_event(contact.update(TouchPhase::Down, 7, 5.0F, 6.0F), TouchPhase::Down, 7, 5.0F, 6.0F);
}

TEST_CASE("update: after Cancel the contact is idle again", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 7, 1.0F, 2.0F).has_value());
  REQUIRE(contact.update(TouchPhase::Cancel, 7).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Move, 7, 9.0F, 9.0F).has_value());
  expect_event(contact.update(TouchPhase::Down, 7, 3.0F, 4.0F), TouchPhase::Down, 7, 3.0F, 4.0F);
}

TEST_CASE("update: the mouse uses id 0 like any other finger", "[panel_touch_contact]") {
  PanelTouchContact contact;
  expect_event(contact.update(TouchPhase::Down, 0, 10.0F, 20.0F), TouchPhase::Down, 0, 10.0F, 20.0F);
  expect_event(contact.update(TouchPhase::Move, 0, 11.0F, 21.0F), TouchPhase::Move, 0, 11.0F, 21.0F);
  expect_event(contact.update(TouchPhase::Up, 0), TouchPhase::Up, 0, 11.0F, 21.0F);
}

TEST_CASE("update: negative ids are tracked too", "[panel_touch_contact]") {
  PanelTouchContact contact;
  expect_event(contact.update(TouchPhase::Down, -5, 1.0F, 1.0F), TouchPhase::Down, -5, 1.0F, 1.0F);
  CHECK_FALSE(contact.update(TouchPhase::Move, 5, 2.0F, 2.0F).has_value());
  expect_event(contact.update(TouchPhase::Up, -5), TouchPhase::Up, -5, 1.0F, 1.0F);
}

// ---------------------------------------------------------------------------
// Reports that are dropped
// ---------------------------------------------------------------------------

TEST_CASE("update: Move, Up and Cancel with no active contact are dropped", "[panel_touch_contact]") {
  PanelTouchContact contact;
  CHECK_FALSE(contact.update(TouchPhase::Move, 7, 1.0F, 2.0F).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Up, 7).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Cancel, 7).has_value());
  // Still idle: the next Down is accepted.
  expect_event(contact.update(TouchPhase::Down, 7, 1.0F, 2.0F), TouchPhase::Down, 7, 1.0F, 2.0F);
}

TEST_CASE("update: a duplicate Down for the active finger is dropped and does not move it", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 7, 100.0F, 200.0F).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Down, 7, 500.0F, 600.0F).has_value());
  expect_event(contact.update(TouchPhase::Up, 7), TouchPhase::Up, 7, 100.0F, 200.0F);
}

TEST_CASE("update: Move or Up for a finger that is not the active one is dropped", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 7, 100.0F, 200.0F).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Move, 8, 1.0F, 1.0F).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Up, 8).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Cancel, 8).has_value());
  // The active finger is untouched by the stranger's reports.
  expect_event(contact.update(TouchPhase::Move, 7, 101.0F, 201.0F), TouchPhase::Move, 7, 101.0F, 201.0F);
  expect_event(contact.update(TouchPhase::Up, 7), TouchPhase::Up, 7, 101.0F, 201.0F);
}

// ---------------------------------------------------------------------------
// Single-contact policy: first finger wins
// ---------------------------------------------------------------------------

TEST_CASE("policy: a second finger landing during a gesture is ignored for its whole life", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 1, 100.0F, 100.0F).has_value());

  CHECK_FALSE(contact.update(TouchPhase::Down, 2, 300.0F, 300.0F).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Move, 2, 310.0F, 310.0F).has_value());

  expect_event(contact.update(TouchPhase::Move, 1, 120.0F, 120.0F), TouchPhase::Move, 1, 120.0F, 120.0F);

  CHECK_FALSE(contact.update(TouchPhase::Up, 2).has_value());  // the ignored finger lifts: still nothing

  expect_event(contact.update(TouchPhase::Up, 1), TouchPhase::Up, 1, 120.0F, 120.0F);
}

TEST_CASE("policy: a second finger that is still down when the first lifts does not take over",
          "[panel_touch_contact]") {
  // Its Down was ignored, so there is nothing to continue; it must go Down again to be tracked.
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 1, 100.0F, 100.0F).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Down, 2, 300.0F, 300.0F).has_value());
  REQUIRE(contact.update(TouchPhase::Up, 1).has_value());

  CHECK_FALSE(contact.update(TouchPhase::Move, 2, 310.0F, 310.0F).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Up, 2).has_value());
  expect_event(contact.update(TouchPhase::Down, 2, 320.0F, 320.0F), TouchPhase::Down, 2, 320.0F, 320.0F);
}

TEST_CASE("policy: fingers are tracked one after another without leaking state", "[panel_touch_contact]") {
  PanelTouchContact contact;
  for (std::int32_t id = 1; id <= 5; ++id) {
    const auto fx = static_cast<float>(id) * 10.0F;
    expect_event(contact.update(TouchPhase::Down, id, fx, fx), TouchPhase::Down, id, fx, fx);
    expect_event(contact.update(TouchPhase::Move, id, fx + 1, fx + 2), TouchPhase::Move, id, fx + 1, fx + 2);
    expect_event(contact.update(TouchPhase::Up, id, 0.0F, 0.0F), TouchPhase::Up, id, fx + 1, fx + 2);
  }
}

// ---------------------------------------------------------------------------
// cancel(): the host ends the gesture
// ---------------------------------------------------------------------------

TEST_CASE("cancel: with an active finger emits Cancel at its last position and clears it", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 4, 10.0F, 20.0F).has_value());
  REQUIRE(contact.update(TouchPhase::Move, 4, 30.0F, 40.0F).has_value());
  expect_event(contact.cancel(), TouchPhase::Cancel, 4, 30.0F, 40.0F);
  CHECK_FALSE(contact.update(TouchPhase::Move, 4, 50.0F, 60.0F).has_value());
  CHECK_FALSE(contact.update(TouchPhase::Up, 4).has_value());
}

TEST_CASE("cancel: with no active finger emits nothing and changes nothing", "[panel_touch_contact]") {
  PanelTouchContact contact;
  CHECK_FALSE(contact.cancel().has_value());
  CHECK_FALSE(contact.cancel().has_value());
  expect_event(contact.update(TouchPhase::Down, 4, 10.0F, 20.0F), TouchPhase::Down, 4, 10.0F, 20.0F);
}

TEST_CASE("cancel: is idempotent; the second call after a gesture finds nothing", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 4, 10.0F, 20.0F).has_value());
  REQUIRE(contact.cancel().has_value());
  CHECK_FALSE(contact.cancel().has_value());
}

TEST_CASE("cancel: after cancel a new gesture starts cleanly", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 4, 10.0F, 20.0F).has_value());
  REQUIRE(contact.cancel().has_value());
  expect_event(contact.update(TouchPhase::Down, 9, 1.0F, 2.0F), TouchPhase::Down, 9, 1.0F, 2.0F);
  expect_event(contact.update(TouchPhase::Up, 9), TouchPhase::Up, 9, 1.0F, 2.0F);
}

TEST_CASE("cancel: the event carries the active finger's id, not a placeholder", "[panel_touch_contact]") {
  PanelTouchContact contact;
  REQUIRE(contact.update(TouchPhase::Down, 42, 5.0F, 6.0F).has_value());
  const auto e = contact.cancel();
  REQUIRE(e.has_value());
  CHECK(e->id == 42);
}

}  // namespace
