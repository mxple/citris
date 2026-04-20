#include "tbp/codec.h"

#include <nlohmann/json.hpp>

#include <array>
#include <stdexcept>
#include <string>
#include <variant>

namespace tbp {

namespace {

using json = nlohmann::json;

// --- Sub-object helpers -----------------------------------------------------

json piece_location_to_json(const PieceLocation &l) {
  return json{{"type", l.type},
              {"orientation", l.orientation},
              {"x", l.x},
              {"y", l.y}};
}

PieceLocation json_to_piece_location(const json &j) {
  PieceLocation l;
  l.type = j.value("type", "I");
  l.orientation = j.value("orientation", "north");
  l.x = j.value("x", 0);
  l.y = j.value("y", 0);
  return l;
}

json move_to_json(const Move &m) {
  return json{{"location", piece_location_to_json(m.location)},
              {"spin", m.spin}};
}

Move json_to_move(const json &j) {
  Move m;
  if (j.contains("location"))
    m.location = json_to_piece_location(j["location"]);
  m.spin = j.value("spin", "none");
  return m;
}

json move_info_to_json(const MoveInfo &mi) {
  json j = json::object();
  if (mi.nodes) j["nodes"] = *mi.nodes;
  if (mi.nps) j["nps"] = *mi.nps;
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
    for (const auto &cell : row) {
      if (cell)
        row_arr.push_back(*cell);
      else
        row_arr.push_back(nullptr);
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
        b[y][x] = j[y][x].get<std::string>();
      // null or anything else: leave empty
    }
  }
  return b;
}

constexpr std::array<const char *, 7> kPieceLetters = {"I", "O", "T", "S", "Z",
                                                       "J", "L"};

json randomizer_state_to_json(const RandomizerState &r) {
  return std::visit(
      [](const auto &v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, UniformRandomizer>) {
          return json{{"type", "uniform"}};
        } else if constexpr (std::is_same_v<T, SevenBagRandomizer>) {
          return json{{"type", "seven_bag"}, {"bag_state", v.bag_state}};
        } else { // GeneralBagRandomizer
          json cur = json::object();
          json fil = json::object();
          for (int i = 0; i < 7; ++i) {
            cur[kPieceLetters[i]] = v.current_bag[i];
            fil[kPieceLetters[i]] = v.filled_bag[i];
          }
          return json{{"type", "general_bag"},
                      {"current_bag", cur},
                      {"filled_bag", fil}};
        }
      },
      r);
}

std::optional<RandomizerState> json_to_randomizer_state(const json &j) {
  if (!j.is_object()) return std::nullopt;
  auto type = j.value("type", "");
  if (type == "uniform") return RandomizerState{UniformRandomizer{}};
  if (type == "seven_bag") {
    SevenBagRandomizer s;
    if (j.contains("bag_state") && j["bag_state"].is_array())
      for (auto &p : j["bag_state"])
        if (p.is_string()) s.bag_state.push_back(p.get<std::string>());
    return RandomizerState{std::move(s)};
  }
  if (type == "general_bag") {
    GeneralBagRandomizer g;
    auto load = [](const json &obj, std::array<int, 7> &out) {
      if (!obj.is_object()) return;
      for (int i = 0; i < 7; ++i) {
        if (obj.contains(kPieceLetters[i]) && obj[kPieceLetters[i]].is_number())
          out[i] = obj[kPieceLetters[i]].get<int>();
      }
    };
    if (j.contains("current_bag")) load(j["current_bag"], g.current_bag);
    if (j.contains("filled_bag")) load(j["filled_bag"], g.filled_bag);
    return RandomizerState{std::move(g)};
  }
  return std::nullopt;
}

// --- Message encoders -------------------------------------------------------

json info_to_json(const Info &i) {
  return json{{"type", "info"}, {"name", i.name}, {"version", i.version},
              {"author", i.author}, {"features", i.features}};
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
  if (r.randomizer) out["randomizer"] = *r.randomizer;
  return out;
}

json start_to_json(const Start &s) {
  json out{{"type", "start"},
           {"queue", s.queue},
           {"combo", s.combo},
           {"back_to_back", s.back_to_back},
           {"board", board_to_json(s.board)}};
  if (s.hold) out["hold"] = *s.hold;
  else out["hold"] = nullptr;
  if (s.randomizer) out["randomizer"] = randomizer_state_to_json(*s.randomizer);
  return out;
}

json stop_to_json(const Stop &) { return json{{"type", "stop"}}; }
json suggest_to_json(const Suggest &) { return json{{"type", "suggest"}}; }
json play_to_json(const Play &p) {
  return json{{"type", "play"}, {"move", move_to_json(p.move)}};
}
json new_piece_to_json(const NewPiece &n) {
  return json{{"type", "new_piece"}, {"piece", n.piece}};
}
json quit_to_json(const Quit &) { return json{{"type", "quit"}}; }

// --- Message decoders -------------------------------------------------------

Info decode_info(const json &j) {
  Info i;
  i.name = j.value("name", "");
  i.version = j.value("version", "");
  i.author = j.value("author", "");
  if (j.contains("features") && j["features"].is_array())
    for (auto &f : j["features"])
      if (f.is_string()) i.features.push_back(f.get<std::string>());
  return i;
}

Error decode_error(const json &j) {
  return Error{j.value("reason", "")};
}

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
    r.randomizer = j["randomizer"].get<std::string>();
  return r;
}

Start decode_start(const json &j) {
  Start s;
  if (j.contains("hold") && j["hold"].is_string())
    s.hold = j["hold"].get<std::string>();
  if (j.contains("queue") && j["queue"].is_array())
    for (auto &p : j["queue"])
      if (p.is_string()) s.queue.push_back(p.get<std::string>());
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
  return NewPiece{j.value("piece", "")};
}

} // namespace

// --- Public API -------------------------------------------------------------

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
  if (type == "info") return Message{decode_info(j)};
  if (type == "ready") return Message{Ready{}};
  if (type == "error") return Message{decode_error(j)};
  if (type == "suggestion") return Message{decode_suggestion(j)};
  if (type == "rules") return Message{decode_rules(j)};
  if (type == "start") return Message{decode_start(j)};
  if (type == "stop") return Message{Stop{}};
  if (type == "suggest") return Message{Suggest{}};
  if (type == "play") return Message{decode_play(j)};
  if (type == "new_piece") return Message{decode_new_piece(j)};
  if (type == "quit") return Message{Quit{}};
  return std::nullopt; // unknown type: per spec, ignore
}

} // namespace tbp
