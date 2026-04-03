#pragma once

#include "attack.h"
#include "board.h"
#include "event.h"
#include "game_state.h"
#include "rng.h"
#include "settings.h"
#include "stats.h"
#include "timer_manager.h"
#include <deque>
#include <optional>
#include <vector>

class Game {
public:
  Game(const Settings &settings, Stats &stats, TimerManager &timers,
       unsigned seed = std::random_device{}());

  bool process(const Event &ev, TimePoint now);

  GameState state() const;

  bool dirty() const { return dirty_; }
  void clear_dirty() { dirty_ = false; }

  int drain_attack();

  bool undo();

private:
  void handle_move(const MoveInput &e);
  void handle_gravity();
  void handle_lock_delay_expired();
  void handle_garbage_received(const GarbageReceived &e);
  void handle_garbage_delay_expired();

  void spawn_piece();
  void lock_piece();
  Piece compute_ghost() const;
  bool drop1();

  bool is_grounded() const;
  void post_move_timers();

  void arm_gravity();
  void apply_20g();
  void arm_lock_delay();
  bool apply_arr0();
  bool apply_sonic_drop();
  void settle();

  struct PendingGarbage {
    int lines;
    int gap_col;
  };

  struct GameSnapshot {
    Board board;
    Piece current_piece;
    std::optional<PieceType> hold_piece;
    bool hold_available;
    AttackState attack_state;
    int lock_resets_remaining;
    std::vector<PendingGarbage> pending_garbage;
    LastClear last_clear;
    int piece_gen;
    int pending_attack;
    bool game_over;
    bool last_move_was_rotation;
    BagRandomizer::BagSnapshot bag_snapshot;
    Stats::UndoState stats_snapshot;
  };

  void push_snapshot();
  void restore_snapshot(const GameSnapshot &snap);

  static constexpr int kMaxUndoDepth = 100;
  std::deque<GameSnapshot> undo_stack_;

  const Settings &settings_;
  Stats &stats_;
  TimerManager &timers_;
  Board board_;
  Piece current_piece_{PieceType::I};
  std::optional<PieceType> hold_piece_;
  bool hold_available_ = true;
  bool last_move_was_rotation_ = false;
  AttackState attack_state_;
  std::unique_ptr<BagRandomizer> bag_;
  int lock_resets_remaining_ = 0;

  std::vector<PendingGarbage> pending_garbage_;

  LastClear last_clear_;
  int piece_gen_ = 0;
  int pending_attack_ = 0;
  bool game_over_ = false;
  bool dirty_ = true;
  std::optional<Input> arr_direction_;
  bool soft_drop_active_ = false;

  TimePoint now_{};
  TimePoint hard_drop_blocked_until_{};
};
