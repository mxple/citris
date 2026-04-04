#pragma once

#include <variant>

namespace note {
struct InputRegistered {};
} // namespace note

using Notification = std::variant<note::InputRegistered>;
