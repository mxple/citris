#pragma once

#include "checkpoint.h"
#include <filesystem>
#include <string>
#include <vector>

// Parse a .opener file into an Opener.
//
// File format (backward compatible — old files without new directives work):
//
//   name: DT Cannon
//   description: Double Triple cannon setup
//   ---
//   # Section between --- is a checkpoint.
//   # New optional directives:
//   id: base           — label this checkpoint for tree references
//   after: base         — this checkpoint follows "base" (tree child)
//   [strict]            — exact board match required (default)
//   [soft]              — checkpoint cells must be filled, extra OK
//   allow: I J L S Z   — only these pieces may be used
//   atleast: I J        — path must use at least these pieces
//   exclude: T          — these pieces may not be used
//   ....X.....
//   ...XXXX...
//   XX.XXXXXXX
//   ---
//
// Tree structure:
//   Checkpoints without 'after:' are root-level alternatives.
//   'after: <id>' links a checkpoint as a child of <id>.
//   Without any id/after directives, sections form a linear sequence.
//
// Rows: 'X' = filled, '.' = empty. Top-to-bottom in text, bottom-to-top on board.
std::optional<Opener> parse_opener_file(const std::string &contents);

// Load a single .opener file from disk.
std::optional<Opener> load_opener(const std::filesystem::path &path);

// Load all .opener files from a directory.
std::vector<Opener> load_openers_from_dir(const std::filesystem::path &dir);

// Return built-in openers (hardcoded).
std::vector<Opener> builtin_openers();
