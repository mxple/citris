#include "ai_mode.h"
#include "eval/cheese.h"
#include "eval/sprint.h"
#include "eval/atk.h"

std::unique_ptr<Evaluator> make_evaluator(AIMode mode) {
  switch (mode) {
  case AIMode::Atk:  return std::make_unique<AtkEvaluator>();
  case AIMode::Well: return std::make_unique<SprintEvaluator>();
  case AIMode::Down: return std::make_unique<CheeseEvaluator>();
  case AIMode::Pc:   return std::make_unique<AtkEvaluator>();
  }
  return std::make_unique<AtkEvaluator>();
}
