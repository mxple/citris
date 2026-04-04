#pragma once

#include "input.h"
#include "notification.h"
#include <optional>
#include <variant>
#include <vector>

namespace cmd {
struct MovePiece {
  Input input;
};
struct SetARRDirection {
  std::optional<Input> direction;
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
struct Passthrough {
  Notification notification;
};
} // namespace cmd

using Command = std::variant<cmd::MovePiece, cmd::SetARRDirection,
                             cmd::SetSoftDropActive, cmd::AddGarbage,
                             cmd::SetGameOver, cmd::Undo, cmd::Passthrough>;

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
