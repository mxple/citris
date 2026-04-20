#pragma once

#include "attack.h"
#include "board.h"
#include "command.h"
#include "engine_event.h"
#include "game_state.h"
#include "engine/piece_queue.h"
#include <deque>
#include <optional>
#include <queue>
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

  // Sum of lines across all queued garbage entries that haven't materialized
  // yet. Used by the versus HUD for the opponent-attack meter; adds no cost
  // to the single-player path.
  int pending_garbage_lines() const {
    int total = 0;
    auto pending_garbage = pending_garbage_;
    while (!pending_garbage.empty()) {
      total += pending_garbage.front().lines;
      pending_garbage.pop();
    }
    return total;
  }

  // Cancel up to `amount` lines from the NEWEST end of pending_garbage_
  // (LIFO — cancel newest incoming garbage first). Partial cancellation
  // within a batch decrements the back entry in place, preserving its
  // gap_col for any unconsumed remainder. Returns the number of lines
  // actually cancelled. Called by route_garbage_between on the sender
  // before forwarding any outgoing-attack remainder to the opponent.
  int cancel_buffered_garbage(int amount);

  std::optional<TimePoint> next_deadline() const;

private:
  void handle_move(const cmd::MovePiece &e);
  void handle_place(const cmd::Place &c);
  void handle_gravity(TimePoint expired_at);
  void handle_lock_delay_expired();
  void handle_garbage_received(int lines, int gap_col, bool immediate);
  // Materialize up to `cap` lines of buffered garbage from pending_garbage_,
  // preserving each batch's gap_col. If the cap falls mid-batch, the
  // remaining lines stay in pending_garbage_ with the *same* gap_col so the
  // next materialization continues on the same well. Called after a piece
  // locks without clearing any lines.
  void materialize_buffered_garbage(int cap);
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

  void arm_gravity(std::optional<TimePoint> chain_from = {});
  void apply_20g();
  void arm_lock_delay();
  bool apply_arr0();
  bool apply_sonic_drop();
  void settle();

  struct PendingGarbage {
    int lines;
    int gap_col;
    // When the batch was received. A batch cannot materialize until
    // (now_ - arrival_time) >= mode_.garbage_delay() — giving the receiver
    // a reactive window to cancel with an outgoing attack.
    TimePoint arrival_time;
  };

  struct GameSnapshot {
    Board board;
    Piece current_piece;
    std::optional<PieceType> hold_piece;
    bool hold_available;
    AttackState attack_state;
    int lock_resets_remaining;
    std::queue<PendingGarbage> pending_garbage;
    LastClear last_clear;
    int piece_gen;
    int pending_attack;
    int lines_cleared;
    int total_attack;
    bool game_over;
    bool won;
    bool last_move_was_rotation;
    PieceQueue::Snapshot queue_snapshot;
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
  // Mutable: peek() lazily refills the buffer from the source, which is a
  // logical-state cache (not part of observable game state), so const
  // accessors like state() can call queue_.peek().
  mutable PieceQueue queue_;
  int lock_resets_remaining_ = 0;

  std::queue<PendingGarbage> pending_garbage_;

  LastClear last_clear_;
  int piece_gen_ = 0;
  int pending_attack_ = 0;
  int lines_cleared_ = 0;
  int total_attack_ = 0;
  bool game_over_ = false;
  bool won_ = false;
  bool dirty_ = true;
  std::optional<GameInput> arr_direction_;
  bool soft_drop_active_ = false;

  TimePoint now_{};

  // Internal timers (replacing TimerManager dependency)
  std::optional<TimePoint> gravity_deadline_;
  std::optional<TimePoint> lock_delay_deadline_;

  // Maximum lines of garbage that can materialize on a single non-clearing
  // lock. Competitive default is 8; excess stays buffered for the next
  // non-clearing lock, retaining the current well's gap column.
  static constexpr int kMaxGarbagePerLock = 8;

  // Engine events produced during apply/tick
  std::vector<EngineEvent> pending_events_;
};
