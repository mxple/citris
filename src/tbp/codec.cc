#include "tbp/codec.h"

#include "tbp/conversions.h"

#include <nlohmann/json.hpp>

#include <array>
#include <string>
#include <string_view>
#include <variant>

// clang-format off
namespace tbp {

namespace {

using json = nlohmann::json;

// Pull a std::string out of a json field, or return default.
std::string j_str(const json &j, const char *key) {
  if (j.contains(key) && j[key].is_string())
    return j[key].get<std::string>();
  return {};
}

// --- Sub-object helpers ----------------------------------------------------

json piece_location_to_json(const PieceLocation &l) {
  return json{{"type", to_wire(l.type)},
              {"orientation", to_wire(l.orientation)},
              {"x", l.x},
              {"y", l.y}};
}

PieceLocation json_to_piece_location(const json &j) {
  PieceLocation l;
  if (auto p = from_wire_piece(j_str(j, "type"))) l.type = *p;
  if (auto r = from_wire_orientation(j_str(j, "orientation"))) l.orientation = *r;
  l.x = j.value("x", 0);
  l.y = j.value("y", 0);
  return l;
}

json move_to_json(const Move &m) {
  return json{{"location", piece_location_to_json(m.location)},
              {"spin", to_wire(m.spin)}};
}

Move json_to_move(const json &j) {
  Move m;
  std::string piece_letter;
  if (j.contains("location")) {
    m.location = json_to_piece_location(j["location"]);
    piece_letter = j_str(j["location"], "type");
  }
  m.spin = from_wire_spin(j_str(j, "spin"), piece_letter);
  return m;
}

json move_info_to_json(const MoveInfo &mi) {
  json j = json::object();
  if (mi.nodes) j["nodes"] = *mi.nodes;
  if (mi.nps)   j["nps"]   = *mi.nps;
  if (mi.depth) j["depth"] = *mi.depth;
  if (mi.extra) j["extra"] = *mi.extra;
  return j;
}

MoveInfo json_to_move_info(const json &j) {
  MoveInfo mi;
  if (auto it = j.find("nodes"); it != j.end() && it->is_number())
    mi.nodes = it->get<double>();
  if (auto it = j.find("nps"); it != j.end() && it->is_number())
    mi.nps = it->get<double>();
  if (auto it = j.find("depth"); it != j.end() && it->is_number())
    mi.depth = it->get<int>();
  if (auto it = j.find("extra"); it != j.end() && it->is_string())
    mi.extra = it->get<std::string>();
  return mi;
}

json board_to_json(const Board &b) {
  json arr = json::array();
  for (const auto &row : b) {
    json row_arr = json::array();
    for (CellColor cell : row) {
      if (cell == CellColor::Empty)
        row_arr.push_back(nullptr);
      else
        row_arr.push_back(to_wire(cell));
    }
    arr.push_back(std::move(row_arr));
  }
  return arr;
}

Board json_to_board(const json &j) {
  Board b{};
  if (!j.is_array()) return b;
  for (size_t y = 0; y < j.size() && y < 40; ++y) {
    if (!j[y].is_array()) continue;
    for (size_t x = 0; x < j[y].size() && x < 10; ++x) {
      if (j[y][x].is_string())
        b[y][x] = from_wire_cell(j[y][x].get<std::string>());
      // null or anything else: leave Empty
    }
  }
  return b;
}

json randomizer_state_to_json(const RandomizerState &r) {
  return std::visit(
      [](const auto &v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, UniformRandomizer>) {
          return json{{"type", to_wire(Randomizer::Uniform)}};
        } else if constexpr (std::is_same_v<T, SevenBagRandomizer>) {
          json bag = json::array();
          for (auto p : v.bag_state) bag.push_back(to_wire(p));
          return json{{"type", to_wire(Randomizer::SevenBag)},
                      {"bag_state", bag}};
        } else { // GeneralBagRandomizer
          json cur = json::object();
          json fil = json::object();
          for (int i = 0; i < 7; ++i) {
            // json object keys are std::string; string_view won't implicitly
            // convert on older nlohmann versions, so materialize here.
            std::string key{to_wire(static_cast<PieceType>(i))};
            cur[key] = v.current_bag[i];
            fil[key] = v.filled_bag[i];
          }
          return json{{"type", to_wire(Randomizer::GeneralBag)},
                      {"current_bag", cur},
                      {"filled_bag", fil}};
        }
      },
      r);
}

std::optional<RandomizerState> json_to_randomizer_state(const json &j) {
  if (!j.is_object()) return std::nullopt;
  auto kind = from_wire_randomizer(j_str(j, "type"));
  if (kind == Randomizer::Uniform)
    return RandomizerState{UniformRandomizer{}};
  if (kind == Randomizer::SevenBag) {
    SevenBagRandomizer s;
    if (j.contains("bag_state") && j["bag_state"].is_array())
      for (auto &p : j["bag_state"])
        if (p.is_string())
          if (auto pt = from_wire_piece(p.get<std::string>()))
            s.bag_state.push_back(*pt);
    return RandomizerState{std::move(s)};
  }
  if (kind == Randomizer::GeneralBag) {
    GeneralBagRandomizer g;
    auto load = [](const json &obj, std::array<int, 7> &out) {
      if (!obj.is_object()) return;
      for (int i = 0; i < 7; ++i) {
        std::string key{to_wire(static_cast<PieceType>(i))};
        if (obj.contains(key) && obj[key].is_number())
          out[i] = obj[key].get<int>();
      }
    };
    if (j.contains("current_bag")) load(j["current_bag"], g.current_bag);
    if (j.contains("filled_bag"))  load(j["filled_bag"],  g.filled_bag);
    return RandomizerState{std::move(g)};
  }
  return std::nullopt;
}

// --- Message encoders ------------------------------------------------------

json info_to_json(const Info &i) {
  return json{{"type", "info"},     {"name", i.name},
              {"version", i.version}, {"author", i.author},
              {"features", i.features}};
}

json ready_to_json(const Ready &) { return json{{"type", "ready"}}; }

json error_to_json(const Error &e) {
  return json{{"type", "error"}, {"reason", e.reason}};
}

json suggestion_to_json(const Suggestion &s) {
  json moves = json::array();
  for (const auto &m : s.moves) moves.push_back(move_to_json(m));
  json out{{"type", "suggestion"}, {"moves", moves}};
  if (s.move_info) out["move_info"] = move_info_to_json(*s.move_info);
  return out;
}

json rules_to_json(const Rules &r) {
  json out{{"type", "rules"}};
  if (r.randomizer) out["randomizer"] = to_wire(*r.randomizer);
  return out;
}

json start_to_json(const Start &s) {
  json queue = json::array();
  for (auto p : s.queue) queue.push_back(to_wire(p));

  json out{{"type", "start"},
           {"queue", queue},
           {"combo", s.combo},
           {"back_to_back", s.back_to_back},
           {"board", board_to_json(s.board)}};
  if (s.hold) out["hold"] = to_wire(*s.hold);
  else        out["hold"] = nullptr;
  if (s.randomizer) out["randomizer"] = randomizer_state_to_json(*s.randomizer);
  return out;
}

json stop_to_json(const Stop &)       { return json{{"type", "stop"}}; }
json suggest_to_json(const Suggest &) { return json{{"type", "suggest"}}; }
json play_to_json(const Play &p) {
  return json{{"type", "play"}, {"move", move_to_json(p.move)}};
}
json new_piece_to_json(const NewPiece &n) {
  return json{{"type", "new_piece"}, {"piece", to_wire(n.piece)}};
}
json quit_to_json(const Quit &) { return json{{"type", "quit"}}; }

// --- Message decoders ------------------------------------------------------

Info decode_info(const json &j) {
  Info i;
  i.name    = j.value("name", "");
  i.version = j.value("version", "");
  i.author  = j.value("author", "");
  if (j.contains("features") && j["features"].is_array())
    for (auto &f : j["features"])
      if (f.is_string()) i.features.push_back(f.get<std::string>());
  return i;
}

Error decode_error(const json &j) { return Error{j.value("reason", "")}; }

Suggestion decode_suggestion(const json &j) {
  Suggestion s;
  if (j.contains("moves") && j["moves"].is_array())
    for (auto &m : j["moves"]) s.moves.push_back(json_to_move(m));
  if (j.contains("move_info") && j["move_info"].is_object())
    s.move_info = json_to_move_info(j["move_info"]);
  return s;
}

Rules decode_rules(const json &j) {
  Rules r;
  if (j.contains("randomizer") && j["randomizer"].is_string())
    r.randomizer = from_wire_randomizer(j["randomizer"].get<std::string>());
  return r;
}

Start decode_start(const json &j) {
  Start s;
  if (j.contains("hold") && j["hold"].is_string())
    if (auto p = from_wire_piece(j["hold"].get<std::string>())) s.hold = *p;
  if (j.contains("queue") && j["queue"].is_array())
    for (auto &p : j["queue"])
      if (p.is_string())
        if (auto pt = from_wire_piece(p.get<std::string>()))
          s.queue.push_back(*pt);
  s.combo = j.value("combo", 0);
  s.back_to_back = j.value("back_to_back", false);
  if (j.contains("board")) s.board = json_to_board(j["board"]);
  if (j.contains("randomizer"))
    s.randomizer = json_to_randomizer_state(j["randomizer"]);
  return s;
}

Play decode_play(const json &j) {
  Play p;
  if (j.contains("move")) p.move = json_to_move(j["move"]);
  return p;
}

NewPiece decode_new_piece(const json &j) {
  NewPiece n{};
  if (auto p = from_wire_piece(j_str(j, "piece"))) n.piece = *p;
  return n;
}

} // namespace

// --- Public API ------------------------------------------------------------

std::string serialize(const Message &m) {
  json j = std::visit(
      [](const auto &v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Info>) return info_to_json(v);
        else if constexpr (std::is_same_v<T, Ready>) return ready_to_json(v);
        else if constexpr (std::is_same_v<T, Error>) return error_to_json(v);
        else if constexpr (std::is_same_v<T, Suggestion>) return suggestion_to_json(v);
        else if constexpr (std::is_same_v<T, Rules>) return rules_to_json(v);
        else if constexpr (std::is_same_v<T, Start>) return start_to_json(v);
        else if constexpr (std::is_same_v<T, Stop>) return stop_to_json(v);
        else if constexpr (std::is_same_v<T, Suggest>) return suggest_to_json(v);
        else if constexpr (std::is_same_v<T, Play>) return play_to_json(v);
        else if constexpr (std::is_same_v<T, NewPiece>) return new_piece_to_json(v);
        else if constexpr (std::is_same_v<T, Quit>) return quit_to_json(v);
        else return json{};
      },
      m);
  return j.dump();
}

std::optional<Message> parse(std::string_view text) {
  json j;
  try {
    j = json::parse(text);
  } catch (const json::exception &) {
    return std::nullopt;
  }
  if (!j.is_object() || !j.contains("type") || !j["type"].is_string())
    return std::nullopt;
  const std::string type = j["type"].get<std::string>();
  if (type == "info")       return Message{decode_info(j)};
  if (type == "ready")      return Message{Ready{}};
  if (type == "error")      return Message{decode_error(j)};
  if (type == "suggestion") return Message{decode_suggestion(j)};
  if (type == "rules")      return Message{decode_rules(j)};
  if (type == "start")      return Message{decode_start(j)};
  if (type == "stop")       return Message{Stop{}};
  if (type == "suggest")    return Message{Suggest{}};
  if (type == "play")       return Message{decode_play(j)};
  if (type == "new_piece")  return Message{decode_new_piece(j)};
  if (type == "quit")       return Message{Quit{}};
  return std::nullopt; // unknown type: per spec, ignore
}

} // namespace tbp
