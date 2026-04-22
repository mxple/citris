#pragma once

#include "command.h"
#include "engine/game_state.h"
#include "engine_event.h"
#include "input_event.h"
#include "render/view_model.h"
#include <optional>

class IController {
public:
  virtual ~IController() = default;
  virtual void handle_event(const InputEvent &ev, TimePoint now,
                            const GameState &state, CommandBuffer &cmds) = 0;
  virtual void tick(TimePoint now, const GameState &state,
                    CommandBuffer &cmds) = 0;
  virtual std::optional<TimePoint> next_deadline() const = 0;
  virtual void reset() = 0;
  virtual void notify(const EngineEvent &ev, TimePoint now, const GameState &state) {
    (void)ev;
    (void)now;
  }

  // Called once per engine tick AFTER the entire notify batch has been drained.
  virtual void post_hook(TimePoint now, const GameState &state) {
    (void)now;
    (void)state;
  }
  virtual void fill_plan_overlay(ViewModel &, const GameState &) {}
  virtual void draw_imgui() {}
  virtual bool has_sidebar() const { return false; }
  virtual void draw_sidebar() {}
};
