#pragma once

#include "ai/placement.h"
#include "notification.h"

enum class GameInput {
  Left,
  Right,
  RotateCW,
  RotateCCW,
  Rotate180,
  SoftDrop,
  HardDrop,
  Hold,
  LLeft,     // slide to left wall
  RRight,    // slide to right wall
  SonicDrop  // drop to lowest valid Y without locking
};
using Input = GameInput;

namespace cmd {
struct MovePiece {
  GameInput input;
};
struct SetARRDirection {
  std::optional<GameInput> direction;
};
struct SetSoftDropActive {
  bool active;
};
struct AddGarbage {
  int lines;
  int gap_col;
  bool immediate = false;
};
struct SetGameOver {
  bool won;
};
struct Undo {};
struct Place {
  Placement placement;
};
struct Passthrough {
  Notification notification;
};
} // namespace cmd

using Command = std::variant<cmd::MovePiece, cmd::SetARRDirection,
                             cmd::SetSoftDropActive, cmd::AddGarbage,
                             cmd::SetGameOver, cmd::Undo, cmd::Place,
                             cmd::Passthrough>;

class CommandBuffer {
public:
  void push(Command cmd) { cmds_.push_back(std::move(cmd)); }
  void clear() { cmds_.clear(); }
  bool empty() const { return cmds_.empty(); }
  auto begin() const { return cmds_.begin(); }
  auto end() const { return cmds_.end(); }

private:
  std::vector<Command> cmds_;
};
