#pragma once

#include "game_mode.h"
#include "puzzle_bank.h"
#include "user_mode_config.h"
#include <random>

class UserMode : public GameMode {
public:
  explicit UserMode(UserModeConfig config);

  std::string title() const override { return config_.name; }

  // Training modes use freeplay-style tuning and always allow undo.
  bool undo_allowed() const override { return true; }
  bool auto_restart() const override { return true; }

  std::unique_ptr<BagRandomizer> create_bag(unsigned seed) const override;

  void on_start(TimePoint now) override;
  void setup_board(Board &board) override;

  void on_piece_locked(const eng::PieceLocked &ev, const GameState &state,
                       CommandBuffer &cmds) override;
  void on_undo(const GameState &state) override;

  void fill_hud(HudData &hud, const GameState &state, TimePoint now) override;

private:
  // Returns true if all goal conditions are currently satisfied.
  bool goals_met(const GameState &state) const;

  // Returns total pieces in the queue (initial + continuation). 0 = infinite.
  int total_pieces() const;

  // Is the queue finite?
  bool finite_queue() const { return total_pieces() > 0; }

  // Setup generation (runs in PuzzleBank worker thread)
  std::optional<GeneratedSetup> generate(std::mt19937 &rng);
  Board generate_board(std::mt19937 &rng);
  std::vector<PieceType> generate_queue(std::mt19937 &rng);
  bool validate_setup(const Board &board, const std::vector<PieceType> &queue);

  // Whether the bank is needed (generated boards needing validation).
  bool needs_bank() const { return config_.needs_validation(); }

  UserModeConfig config_;
  std::unique_ptr<PuzzleBank> bank_; // null if not needed
  GeneratedSetup current_;
  bool has_current_ = false;
  bool last_was_win_ = false;

  // Per-run tracking (reset each attempt)
  int pieces_placed_ = 0;
  int pcs_ = 0;
  int tsds_ = 0;
  int tsts_ = 0;
  int quads_ = 0;
  int max_combo_ = 0;
  int max_b2b_ = 0;

  // Per-session tracking
  int solves_ = 0;
  int attempts_ = 0;

  std::mt19937 fallback_rng_{std::random_device{}()};
};
