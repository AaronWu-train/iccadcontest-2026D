/**
 * @file sa_common.hpp
 * @brief Shared helpers for simulated-annealing optimizers.
 */

#pragma once

#include <random>

#include "clock_tree.hpp"
#include "evaluation.hpp"
#include "optimization/skew_model.hpp"
#include "types.hpp"

namespace cadd0040 {
namespace sa {

std::mt19937& rng();

int pick_insert_cell(SkewModel& model);

SkewMove random_move(SkewModel& model);

void materialize(ClockTree& clock_tree, const SkewModelState& state, const SkewModel& model,
                 const BufferLibrary& buffer_library);

Metrics metrics_from_skew(const SkewModelMetrics& model_metrics);

void maybe_update_best(SkewModel& model, const Metrics& baseline_metrics, double& current_score,
                       double& best_score, SkewModelState& best_state, Metrics& best_metrics);

void restart_from_best(SkewModel& model, double& current_score, double best_score,
                       const SkewModelState& best_state);

}  // namespace sa
}  // namespace cadd0040
