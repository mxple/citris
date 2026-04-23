#pragma once

#include <variant>

// Forward-declared; full definition in command.h. Storing by value is fine
// because the underlying type is fixed (sized int).
enum class GameInput : int;

namespace note {
struct InputRegistered {
  GameInput input;
};
} // namespace note

using Notification = std::variant<note::InputRegistered>;
