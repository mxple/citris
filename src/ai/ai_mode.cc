#include "ai_mode.h"
#include "eval/cheese.h"
#include "eval/sprint.h"
#include "eval/atk.h"

std::unique_ptr<Evaluator> make_evaluator(const AIMode &mode) {
  return std::visit(
      [](const auto &m) -> std::unique_ptr<Evaluator> {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, AtkMode>)
          return std::make_unique<AtkEvaluator>();
        else if constexpr (std::is_same_v<T, WellMode>)
          return std::make_unique<SprintEvaluator>();
        else if constexpr (std::is_same_v<T, DownMode>)
          return std::make_unique<CheeseEvaluator>();
        else // PcMode — beam fallback uses TSD
          return std::make_unique<AtkEvaluator>();
      },
      mode);
}
