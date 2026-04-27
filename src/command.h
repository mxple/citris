#pragma once

#include "ai/placement.h"
#include "engine/piece.h"
#include "notification.h"
#include <vector>

enum class GameInput : int {
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
struct ReplaceQueuePrefix {
  std::vector<PieceType> pieces;
};
struct ReplaceCurrentPiece {
  PieceType type;
};
struct ClearHold {};
} // namespace cmd

using Command = std::variant<cmd::MovePiece, cmd::SetARRDirection,
                             cmd::SetSoftDropActive, cmd::AddGarbage,
                             cmd::SetGameOver, cmd::Undo, cmd::Place,
                             cmd::Passthrough, cmd::ReplaceQueuePrefix,
                             cmd::ReplaceCurrentPiece, cmd::ClearHold>;

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

template <>
struct fmt::formatter<GameInput> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.end();
    }
    
    template <typename FormatContext>
    auto format(GameInput input, FormatContext& ctx) const {
        switch (input) {
            case GameInput::Left: return fmt::format_to(ctx.out(), "Left");
            case GameInput::Right: return fmt::format_to(ctx.out(), "Right");
            case GameInput::RotateCW: return fmt::format_to(ctx.out(), "RotateCW");
            case GameInput::RotateCCW: return fmt::format_to(ctx.out(), "RotateCCW");
            case GameInput::Rotate180: return fmt::format_to(ctx.out(), "Rotate180");
            case GameInput::SoftDrop: return fmt::format_to(ctx.out(), "SoftDrop");
            case GameInput::HardDrop: return fmt::format_to(ctx.out(), "HardDrop");
            case GameInput::Hold: return fmt::format_to(ctx.out(), "Hold");
            case GameInput::LLeft: return fmt::format_to(ctx.out(), "LLeft");
            case GameInput::RRight: return fmt::format_to(ctx.out(), "RRight");
            case GameInput::SonicDrop: return fmt::format_to(ctx.out(), "SonicDrop");
            default: return fmt::format_to(ctx.out(), "Unknown");
        }
    }
};
