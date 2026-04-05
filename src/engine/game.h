#pragma once

#include "attack.h"
#include "board.h"
#include "command.h"
#include "engine_event.h"
#include "game_state.h"
#include "rng.h"
#include <deque>
#include <optional>
#include <vector>

class GameMode;

class Game {
public:
  Game(const GameMode &mode, Board board, unsigned seed = std::random_device{}());

  void apply(const CommandBuffer &cmds);
  void tick(TimePoint now);

  std::vector<EngineEvent> drain_events() { return std::move(pending_events_); }

  GameState state() const;

  bool dirty() const { return dirty_; }
  void clear_dirty() { dirty_ = false; }

  int drain_attack();

  std::optional<TimePoint> next_deadline() const;

private:
  void handle_move(const cmd::MovePiece &e);
  void handle_gravity();
  void handle_lock_delay_expired();
  void handle_garbage_received(int lines, int gap_col, bool immediate);
  void handle_garbage_delay_expired();
  void handle_set_game_over(bool won);
  void handle_undo();

  void push_snapshot();
  void spawn_piece();
  void lock_piece();
  bool top_out();
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
    int lines_cleared;
    int total_attack;
    bool game_over;
    bool won;
    bool last_move_was_rotation;
    BagRandomizer::BagSnapshot bag_snapshot;
  };

  void restore_snapshot(const GameSnapshot &snap);

  static constexpr int kMaxUndoDepth = 100;
  std::deque<GameSnapshot> undo_stack_;

  const GameMode &mode_;
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
  int lines_cleared_ = 0;
  int total_attack_ = 0;
  bool game_over_ = false;
  bool won_ = false;
  bool dirty_ = true;
  std::optional<Input> arr_direction_;
  bool soft_drop_active_ = false;

  TimePoint now_{};

  // Internal timers (replacing TimerManager dependency)
  std::optional<TimePoint> gravity_deadline_;
  std::optional<TimePoint> lock_delay_deadline_;
  std::optional<TimePoint> garbage_delay_deadline_;

  // Engine events produced during apply/tick
  std::vector<EngineEvent> pending_events_;
};
