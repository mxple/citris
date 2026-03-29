#pragma once

#include "attack.h"
#include "board.h"
#include "game_state.h"
#include "input.h"
#include "rng.h"
#include "settings.h"
#include <optional>
#include <vector>

class Game {
public:
  Game(const Settings &settings, unsigned seed = std::random_device{}());

  void post_event(GameEvent event);
  void update(TimePoint now);

  GameState state() const;

  bool state_dirty() const { return dirty_; }
  void clear_dirty() { dirty_ = false; }

  int drain_attack();

  std::optional<TimePoint> next_wakeup() const;

private:
  void process_event(const GameEvent &event);

  void handle_input(const InputEvent &e);
  void handle_timer(const TimerEvent &e);
  void handle_garbage(const GarbageEvent &e);

  void spawn_piece();
  void lock_piece();
  Piece compute_ghost() const;
  bool drop1();

  bool is_grounded() const;
  void post_move_timers();

  void schedule_timer(TimerKind kind, Duration fire_time);
  void cancel_timer(TimerKind kind);

  const Settings &settings_;
  Board board_;
  Piece current_piece_{PieceType::I};
  std::optional<PieceType> hold_piece_;
  bool hold_available_ = true;
  bool last_move_was_rotation_ = false;
  AttackState attack_state_;
  std::unique_ptr<BagRandomizer> bag_;
  int lock_resets_remaining_ = 0;

  struct PendingGarbage {
    int lines;
    int gap_col;
  };
  std::vector<PendingGarbage> pending_garbage_;

  std::vector<GameEvent> event_queue_;

  std::array<std::optional<TimePoint>, static_cast<int>(TimerKind::N)>
      timers_{};

  int pending_attack_ = 0;
  bool game_over_ = false;
  bool dirty_ = true;

  // Current tick time — set at the start of update(), used by all internal
  // methods.
  TimePoint now_{};
};
