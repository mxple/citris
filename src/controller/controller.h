#pragma once

#include "command.h"
#include "engine_event.h"
#include "engine/game_state.h"
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
  virtual void reset_input_state() = 0;
  virtual void notify(const EngineEvent &ev, TimePoint now) {
    (void)ev;
    (void)now;
  }
  virtual void fill_plan_overlay(ViewModel &, const GameState &) {}
  virtual void draw_imgui() {}
};
