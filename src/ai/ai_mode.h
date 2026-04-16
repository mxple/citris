#pragma once

#include "eval/eval.h"
#include <memory>
#include <variant>

// ---------------------------------------------------------------------------
// AI mode configs — one per "Prioritize" option in the debug UI
// ---------------------------------------------------------------------------

struct AtkMode {};  // TSD evaluator

struct WellMode {};

struct DownMode {};

struct PcMode {};  // PC solver first; falls back to AtkMode beam on failure

using AIMode = std::variant<AtkMode, WellMode, DownMode, PcMode>;

// Overrides shared by all beam modes
struct BeamOverrides {
  int width = 800;
  int depth = 12;
};

// Build the evaluator appropriate for the given mode.
// For PcMode, returns the beam-fallback evaluator (TsdEvaluator).
std::unique_ptr<Evaluator> make_evaluator(const AIMode &mode);
