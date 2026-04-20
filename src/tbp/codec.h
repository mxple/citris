#pragma once

// JSON encode/decode for TBP messages. Wraps nlohmann::json so callers don't
// need to include the heavy header transitively.

#include "tbp/types.h"

#include <optional>
#include <string>
#include <string_view>

namespace tbp {

// Serialize a message to a single-line JSON string (no trailing newline).
// Append '\n' yourself when writing to a transport.
std::string serialize(const Message &m);

// Parse a single TBP message from a JSON string. Returns nullopt on:
//   - parse error
//   - missing "type" field
//   - unrecognized "type" value (per spec: such messages are ignored)
std::optional<Message> parse(std::string_view text);

} // namespace tbp
