#include "sdl_input_source.hpp"

#include <SDL2/SDL.h>

namespace c3::platform {

std::optional<TouchEvent> SdlInputSource::map(TouchPhase phase, std::int32_t id, float wx, float wy) noexcept {
  const PanelViewport& vp = backend_.viewport();

  if (phase == TouchPhase::Down) {
    const auto p = window_to_panel(vp, wx, wy);
    if (!p) {
      return std::nullopt;  // presses in the letterbox are not ours
    }
    return contact_.update(phase, id, p->x, p->y);
  }

  const PanelPoint p = window_to_panel_clamped(vp, wx, wy);
  if (phase == TouchPhase::Up) {
    static_cast<void>(contact_.update(TouchPhase::Move, id, p.x, p.y));
  }

  return contact_.update(phase, id, p.x, p.y);
}

bool SdlInputSource::poll(TouchEvent& out) noexcept {
  SDL_Event e;
  for (unsigned count = 0; count < 64 && SDL_PollEvent(&e) != 0; ++count) {
    std::optional<TouchEvent> ev;

    switch (e.type) {
      case SDL_QUIT:
        quit_ = true;
        break;
      case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
          ev = contact_.cancel();
        }
        break;
      case SDL_KEYDOWN:
        if (e.key.keysym.sym == SDLK_ESCAPE) {
          quit_ = true;
        }
        break;
      case SDL_MOUSEBUTTONDOWN:
        // Touch arrives as finger events; ignore the mouse echo SDL synthesises for it.
        if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) {
          ev = map(TouchPhase::Down, 0, static_cast<float>(e.button.x), static_cast<float>(e.button.y));
        }
        break;
      case SDL_MOUSEBUTTONUP:
        if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID) {
          ev = map(TouchPhase::Up, 0, static_cast<float>(e.button.x), static_cast<float>(e.button.y));
        }
        break;
      case SDL_MOUSEMOTION:
        if (e.motion.which != SDL_TOUCH_MOUSEID) {
          ev = map(TouchPhase::Move, 0, static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
        }
        break;
      case SDL_FINGERDOWN:
      case SDL_FINGERMOTION:
      case SDL_FINGERUP: {
        // Finger coordinates are normalised to the window; ids are 64-bit, we keep the low bits.
        const PanelViewport& vp = backend_.viewport();
        const TouchPhase phase = e.type == SDL_FINGERDOWN     ? TouchPhase::Down
                                 : e.type == SDL_FINGERMOTION ? TouchPhase::Move
                                                              : TouchPhase::Up;
        ev = map(phase, static_cast<std::int32_t>(e.tfinger.fingerId & 0x7fffffff),
                 e.tfinger.x * static_cast<float>(vp.window_w), e.tfinger.y * static_cast<float>(vp.window_h));
        break;
      }
      default:
        break;
    }

    if (ev) {
      out = *ev;
      return true;
    }
  }
  return false;
}

}  // namespace c3::platform
