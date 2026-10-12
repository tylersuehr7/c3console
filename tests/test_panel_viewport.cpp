#include "platform/display_backend.hpp"
#include "platform/panel_viewport.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <optional>

using c3::platform::drawable_to_panel;
using c3::platform::drawable_to_panel_clamped;
using c3::platform::fit_panel;
using c3::platform::PanelPoint;
using c3::platform::PanelViewport;
using c3::platform::Size;
using c3::platform::window_to_panel;
using c3::platform::window_to_panel_clamped;
using Catch::Matchers::WithinAbs;

namespace {

constexpr Size kPanel{.width = 800, .height = 1280};         // spec §5 panel at 1x
constexpr Size kTargetPanel{.width = 1668, .height = 2388};  // header's target geometry

/// Fits with a 1:1 window, as on the target and on a non-HiDPI desktop.
[[nodiscard]] constexpr PanelViewport fit(Size panel, int w, int h) noexcept {
  return fit_panel(panel, w, h, w, h);
}

void expect_point(const std::optional<PanelPoint>& p, float x, float y) {
  REQUIRE(p.has_value());
  CHECK_THAT(p->x, WithinAbs(x, 1e-3));
  CHECK_THAT(p->y, WithinAbs(y, 1e-3));
}

// ---------------------------------------------------------------------------
// fit_panel
// ---------------------------------------------------------------------------

TEST_CASE("fit_panel: a drawable that is the panel itself is scale 1 with no letterbox", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 800, 1280);
  STATIC_CHECK(vp.scale == 1.0F);
  STATIC_CHECK(vp.offset_x == 0.0F);
  STATIC_CHECK(vp.offset_y == 0.0F);
  STATIC_CHECK(vp.panel == kPanel);
  STATIC_CHECK(vp.drawable_w == 800);
  STATIC_CHECK(vp.drawable_h == 1280);
  STATIC_CHECK(vp.pixel_ratio() == 1.0F);

  constexpr PanelViewport target = fit(kTargetPanel, 1668, 2388);
  STATIC_CHECK(target.scale == 1.0F);
  STATIC_CHECK(target.offset_x == 0.0F);
  STATIC_CHECK(target.offset_y == 0.0F);
}

TEST_CASE("fit_panel: a wider drawable pillarboxes the panel horizontally, centred", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 1000, 1280);
  STATIC_CHECK(vp.scale == 1.0F);
  STATIC_CHECK(vp.offset_x == 100.0F);
  STATIC_CHECK(vp.offset_y == 0.0F);
}

TEST_CASE("fit_panel: a taller drawable letterboxes the panel vertically, centred", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 800, 2000);
  STATIC_CHECK(vp.scale == 1.0F);
  STATIC_CHECK(vp.offset_x == 0.0F);
  STATIC_CHECK(vp.offset_y == 360.0F);
}

TEST_CASE("fit_panel: scales down uniformly to a smaller drawable of the same aspect", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 400, 640);
  STATIC_CHECK(vp.scale == 0.5F);
  STATIC_CHECK(vp.offset_x == 0.0F);
  STATIC_CHECK(vp.offset_y == 0.0F);
}

TEST_CASE("fit_panel: scales up when the drawable is larger in both axes", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 1600, 2560);
  STATIC_CHECK(vp.scale == 2.0F);
  STATIC_CHECK(vp.offset_x == 0.0F);
  STATIC_CHECK(vp.offset_y == 0.0F);
}

TEST_CASE("fit_panel: a laptop window shows the portrait panel pillarboxed and shrunk", "[panel_viewport]") {
  const PanelViewport vp = fit(kPanel, 1440, 900);
  CHECK_THAT(vp.scale, WithinAbs(900.0 / 1280.0, 1e-6));
  CHECK(vp.offset_y == 0.0F);
  CHECK_THAT(vp.offset_x, WithinAbs((1440.0 - 800.0 * 900.0 / 1280.0) / 2.0, 1e-3));
  CHECK_THAT(vp.offset_x, WithinAbs(438.75, 1e-3));
}

TEST_CASE("fit_panel: the limiting axis gets an exact zero offset, never a rounding residue", "[panel_viewport]") {
  // Awkward sizes where panel * scale does not reproduce the drawable extent in float.
  const PanelViewport by_height = fit(kTargetPanel, 1000, 1431);
  CHECK(by_height.offset_y == 0.0F);
  CHECK(by_height.offset_x >= 0.0F);

  const PanelViewport by_width = fit(kTargetPanel, 997, 2000);
  CHECK(by_width.offset_x == 0.0F);
  CHECK(by_width.offset_y >= 0.0F);
}

TEST_CASE("fit_panel: for any drawable the scaled panel fits, is centred, and uses the smaller ratio",
          "[panel_viewport]") {
  for (const int w : {1, 100, 320, 799, 800, 801, 1000, 1440, 1920, 3840}) {
    for (const int h : {1, 100, 480, 900, 1279, 1280, 1281, 2000, 2160}) {
      INFO("drawable " << w << " x " << h);
      const PanelViewport vp = fit(kPanel, w, h);
      const float sx = static_cast<float>(w) / 800.0F;
      const float sy = static_cast<float>(h) / 1280.0F;
      CHECK(vp.scale == std::min(sx, sy));
      const float scaled_w = 800.0F * vp.scale;
      const float scaled_h = 1280.0F * vp.scale;
      CHECK(scaled_w <= static_cast<float>(w) + 1e-3F);
      CHECK(scaled_h <= static_cast<float>(h) + 1e-3F);
      CHECK(vp.offset_x >= 0.0F);
      CHECK(vp.offset_y >= 0.0F);
      CHECK_THAT(vp.offset_x * 2 + scaled_w, WithinAbs(static_cast<double>(w), 1e-2));
      CHECK_THAT(vp.offset_y * 2 + scaled_h, WithinAbs(static_cast<double>(h), 1e-2));
      CHECK(((vp.offset_x == 0.0F) || (vp.offset_y == 0.0F)));
    }
  }
}

TEST_CASE("fit_panel: degenerate panel or drawable sizes leave the identity mapping", "[panel_viewport]") {
  for (const auto [pw, ph, dw, dh] :
       {std::array{0, 1280, 800, 1280}, std::array{800, 0, 800, 1280}, std::array{800, 1280, 0, 1280},
        std::array{800, 1280, 800, 0}, std::array{-1, 1280, 800, 1280}, std::array{800, 1280, 800, -5}}) {
    INFO("panel " << pw << "x" << ph << " drawable " << dw << "x" << dh);
    const PanelViewport vp = fit_panel({.width = pw, .height = ph}, dw, dh, dw, dh);
    CHECK(vp.scale == 1.0F);
    CHECK(vp.offset_x == 0.0F);
    CHECK(vp.offset_y == 0.0F);
    CHECK(vp.panel == Size{.width = pw, .height = ph});
    CHECK(vp.drawable_w == dw);
    CHECK(vp.drawable_h == dh);
  }
}

TEST_CASE("pixel_ratio: drawable pixels per window point", "[panel_viewport]") {
  STATIC_CHECK(fit_panel(kPanel, 1600, 2560, 800, 1280).pixel_ratio() == 2.0F);  // Retina
  STATIC_CHECK(fit_panel(kPanel, 800, 1280, 800, 1280).pixel_ratio() == 1.0F);
  STATIC_CHECK(fit_panel(kPanel, 1200, 1920, 800, 1280).pixel_ratio() == 1.5F);
  STATIC_CHECK(fit_panel(kPanel, 800, 1280, 0, 0).pixel_ratio() == 1.0F);  // unknown window: assume 1:1
}

// ---------------------------------------------------------------------------
// drawable_to_panel
// ---------------------------------------------------------------------------

TEST_CASE("drawable_to_panel: identity viewport passes points through", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 800, 1280);
  expect_point(drawable_to_panel(vp, 0.0F, 0.0F), 0.0F, 0.0F);
  expect_point(drawable_to_panel(vp, 10.0F, 20.0F), 10.0F, 20.0F);
  expect_point(drawable_to_panel(vp, 799.5F, 1279.5F), 799.5F, 1279.5F);
}

TEST_CASE("drawable_to_panel: the panel edge is half-open: 0 is inside, width and height are out", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 800, 1280);
  CHECK(drawable_to_panel(vp, 0.0F, 0.0F).has_value());
  CHECK(drawable_to_panel(vp, 799.0F, 1279.0F).has_value());
  CHECK(drawable_to_panel(vp, 799.999F, 1279.999F).has_value());
  CHECK_FALSE(drawable_to_panel(vp, 800.0F, 0.0F).has_value());
  CHECK_FALSE(drawable_to_panel(vp, 0.0F, 1280.0F).has_value());
  CHECK_FALSE(drawable_to_panel(vp, -0.001F, 0.0F).has_value());
  CHECK_FALSE(drawable_to_panel(vp, 0.0F, -0.001F).has_value());
}

TEST_CASE("drawable_to_panel: points in the letterbox are rejected, the boundary itself is inside",
          "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 1000, 1280);  // offset_x = 100
  CHECK_FALSE(drawable_to_panel(vp, 0.0F, 600.0F).has_value());
  CHECK_FALSE(drawable_to_panel(vp, 99.999F, 600.0F).has_value());
  expect_point(drawable_to_panel(vp, 100.0F, 600.0F), 0.0F, 600.0F);
  expect_point(drawable_to_panel(vp, 899.999F, 600.0F), 799.999F, 600.0F);
  CHECK_FALSE(drawable_to_panel(vp, 900.0F, 600.0F).has_value());
  CHECK_FALSE(drawable_to_panel(vp, 999.0F, 600.0F).has_value());
}

TEST_CASE("drawable_to_panel: divides out the scale", "[panel_viewport]") {
  constexpr PanelViewport half = fit(kPanel, 400, 640);
  expect_point(drawable_to_panel(half, 200.0F, 300.0F), 400.0F, 600.0F);
  expect_point(drawable_to_panel(half, 399.5F, 639.5F), 799.0F, 1279.0F);
  CHECK_FALSE(drawable_to_panel(half, 400.0F, 0.0F).has_value());

  constexpr PanelViewport twice = fit(kPanel, 1600, 2560);
  expect_point(drawable_to_panel(twice, 200.0F, 300.0F), 100.0F, 150.0F);
}

TEST_CASE("drawable_to_panel: scale and offset together on the laptop viewport", "[panel_viewport]") {
  const PanelViewport vp = fit(kPanel, 1440, 900);  // scale 0.703125, offset_x 438.75
  expect_point(drawable_to_panel(vp, 438.75F, 0.0F), 0.0F, 0.0F);
  expect_point(drawable_to_panel(vp, 438.75F + 400.0F * vp.scale, 450.0F), 400.0F, 640.0F);
  CHECK_FALSE(drawable_to_panel(vp, 438.0F, 450.0F).has_value());
  CHECK_FALSE(drawable_to_panel(vp, 438.75F + 800.0F * vp.scale, 450.0F).has_value());
}

TEST_CASE("drawable_to_panel: a viewport with no scale maps nothing", "[panel_viewport]") {
  PanelViewport vp = fit(kPanel, 800, 1280);
  vp.scale = 0.0F;
  CHECK_FALSE(drawable_to_panel(vp, 10.0F, 10.0F).has_value());
  vp.scale = -1.0F;
  CHECK_FALSE(drawable_to_panel(vp, 10.0F, 10.0F).has_value());
}

TEST_CASE("drawable_to_panel: round-trips a panel point drawn through the viewport", "[panel_viewport]") {
  for (const PanelViewport vp : {fit(kPanel, 800, 1280), fit(kPanel, 1440, 900), fit(kPanel, 400, 640),
                                 fit(kPanel, 1000, 1280), fit(kTargetPanel, 1000, 1431)}) {
    const float pw = static_cast<float>(vp.panel.width);
    const float ph = static_cast<float>(vp.panel.height);
    for (const PanelPoint p : {PanelPoint{.x = 0.5F, .y = 0.5F}, PanelPoint{.x = 123.0F, .y = 456.0F},
                               PanelPoint{.x = pw / 2, .y = ph / 2}, PanelPoint{.x = pw - 1.0F, .y = ph - 1.0F}}) {
      const float dx = p.x * vp.scale + vp.offset_x;  // where the renderer puts it
      const float dy = p.y * vp.scale + vp.offset_y;
      INFO("viewport scale " << vp.scale << " point (" << p.x << ", " << p.y << ")");
      const auto back = drawable_to_panel(vp, dx, dy);
      REQUIRE(back.has_value());
      CHECK_THAT(back->x, WithinAbs(p.x, 1e-2));
      CHECK_THAT(back->y, WithinAbs(p.y, 1e-2));
    }
  }
}

// ---------------------------------------------------------------------------
// drawable_to_panel_clamped
// ---------------------------------------------------------------------------

TEST_CASE("drawable_to_panel_clamped: inside the panel it agrees with the unclamped mapping", "[panel_viewport]") {
  const PanelViewport vp = fit(kPanel, 1440, 900);
  for (const auto [x, y] : {std::array{438.75F, 0.0F}, std::array{700.0F, 450.0F}, std::array{900.0F, 899.0F}}) {
    const auto exact = drawable_to_panel(vp, x, y);
    REQUIRE(exact.has_value());
    const PanelPoint clamped = drawable_to_panel_clamped(vp, x, y);
    if (exact->x <= 799.0F && exact->y <= 1279.0F) {
      CHECK(clamped.x == exact->x);
      CHECK(clamped.y == exact->y);
    }
  }
}

TEST_CASE("drawable_to_panel_clamped: the letterbox and beyond clamp to the nearest panel edge", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 1000, 1280);        // offset_x = 100
  PanelPoint p = drawable_to_panel_clamped(vp, 0.0F, 640.0F);  // left letterbox
  CHECK(p.x == 0.0F);
  CHECK(p.y == 640.0F);

  p = drawable_to_panel_clamped(vp, 999.0F, 640.0F);  // right letterbox
  CHECK(p.x == 799.0F);
  CHECK(p.y == 640.0F);

  p = drawable_to_panel_clamped(vp, 500.0F, -50.0F);  // above the drawable
  CHECK(p.x == 400.0F);
  CHECK(p.y == 0.0F);

  p = drawable_to_panel_clamped(vp, 500.0F, 5000.0F);  // below
  CHECK(p.x == 400.0F);
  CHECK(p.y == 1279.0F);
}

TEST_CASE("drawable_to_panel_clamped: never returns a point outside [0, size - 1]", "[panel_viewport]") {
  const PanelViewport vp = fit(kPanel, 1440, 900);
  for (const float x : {-1e6F, -1.0F, 0.0F, 438.75F, 720.0F, 1439.0F, 1440.0F, 1e6F}) {
    for (const float y : {-1e6F, -1.0F, 0.0F, 450.0F, 899.0F, 900.0F, 1e6F}) {
      const PanelPoint p = drawable_to_panel_clamped(vp, x, y);
      INFO("drawable (" << x << ", " << y << ") -> (" << p.x << ", " << p.y << ")");
      CHECK(p.x >= 0.0F);
      CHECK(p.y >= 0.0F);
      CHECK(p.x <= 799.0F);
      CHECK(p.y <= 1279.0F);
    }
  }
}

TEST_CASE("drawable_to_panel_clamped: the far edge clamps to the last pixel, not to the size", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 800, 1280);
  const PanelPoint p = drawable_to_panel_clamped(vp, 800.0F, 1280.0F);
  CHECK(p.x == 799.0F);
  CHECK(p.y == 1279.0F);
  const PanelPoint q = drawable_to_panel_clamped(vp, 799.5F, 1279.5F);
  CHECK(q.x == 799.0F);  // the half pixel past the last centre is also pulled back
  CHECK(q.y == 1279.0F);
}

TEST_CASE("drawable_to_panel_clamped: a viewport with no scale returns the origin", "[panel_viewport]") {
  PanelViewport vp = fit(kPanel, 800, 1280);
  vp.scale = 0.0F;
  const PanelPoint p = drawable_to_panel_clamped(vp, 500.0F, 500.0F);
  CHECK(p.x == 0.0F);
  CHECK(p.y == 0.0F);
}

// ---------------------------------------------------------------------------
// window_to_panel: SDL reports window points, not drawable pixels
// ---------------------------------------------------------------------------

TEST_CASE("window_to_panel: on a 1:1 window it is the drawable mapping", "[panel_viewport]") {
  constexpr PanelViewport vp = fit(kPanel, 1000, 1280);
  expect_point(window_to_panel(vp, 100.0F, 600.0F), 0.0F, 600.0F);
  CHECK_FALSE(window_to_panel(vp, 50.0F, 600.0F).has_value());
}

TEST_CASE("window_to_panel: on a Retina window points are doubled before mapping", "[panel_viewport]") {
  constexpr PanelViewport vp = fit_panel(kPanel, 1600, 2560, 800, 1280);  // ratio 2, scale 2
  expect_point(window_to_panel(vp, 100.0F, 200.0F), 100.0F, 200.0F);      // a window point is a panel pixel here
  expect_point(window_to_panel(vp, 799.5F, 1279.5F), 799.5F, 1279.5F);
  CHECK_FALSE(window_to_panel(vp, 800.0F, 0.0F).has_value());
}

TEST_CASE("window_to_panel: Retina with letterboxing composes ratio, offset and scale", "[panel_viewport]") {
  const PanelViewport vp = fit_panel(kPanel, 2880, 1800, 1440, 900);  // ratio 2; scale 1.40625; offset_x 877.5
  CHECK(vp.pixel_ratio() == 2.0F);
  CHECK_THAT(vp.scale, WithinAbs(1800.0 / 1280.0, 1e-6));
  CHECK(vp.offset_y == 0.0F);
  expect_point(window_to_panel(vp, vp.offset_x / 2, 0.0F), 0.0F, 0.0F);
  expect_point(window_to_panel(vp, vp.offset_x / 2 + 400.0F * vp.scale / 2, 450.0F), 400.0F, 640.0F);
  CHECK_FALSE(window_to_panel(vp, vp.offset_x / 2 - 1.0F, 450.0F).has_value());
}

TEST_CASE("window_to_panel_clamped: applies the ratio and then clamps", "[panel_viewport]") {
  constexpr PanelViewport vp = fit_panel(kPanel, 1600, 2560, 800, 1280);
  PanelPoint p = window_to_panel_clamped(vp, -10.0F, 100.0F);
  CHECK(p.x == 0.0F);
  CHECK(p.y == 100.0F);
  p = window_to_panel_clamped(vp, 900.0F, 2000.0F);
  CHECK(p.x == 799.0F);
  CHECK(p.y == 1279.0F);
  p = window_to_panel_clamped(vp, 400.0F, 640.0F);
  CHECK(p.x == 400.0F);
  CHECK(p.y == 640.0F);
}

TEST_CASE("window_to_panel: a drag that leaves the panel still resolves through the clamped mapping",
          "[panel_viewport]") {
  // A finger that slides off the panel into the letterbox: the unclamped mapping says
  // "outside" and the clamped one says "nearest edge". Both are needed by the touch router.
  constexpr PanelViewport vp = fit(kPanel, 1000, 1280);
  CHECK_FALSE(window_to_panel(vp, 20.0F, 640.0F).has_value());
  const PanelPoint p = window_to_panel_clamped(vp, 20.0F, 640.0F);
  CHECK(p.x == 0.0F);
  CHECK(p.y == 640.0F);
}

}  // namespace
