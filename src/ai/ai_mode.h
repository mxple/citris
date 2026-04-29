#pragma once

#include "eval/eval.h"
#include <memory>

enum class AIMode { Atk, Well, Down, Pc };

struct BeamOverrides {
  int width = 800;
  int depth = 12;
};

// Build the evaluator appropriate for the given mode.
// For Pc, returns the beam-fallback evaluator (AtkEvaluator).
std::unique_ptr<Evaluator> make_evaluator(AIMode mode);
