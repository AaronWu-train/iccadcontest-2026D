/**
 * @file milp_optimizer.cpp
 * @brief MILP-inspired deterministic optimizer implementation.
 */

#include "optimization/milp/milp_optimizer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <vector>

#include "optimization/sa/sa_common.hpp"
#include "optimization/sa/skew_model.hpp"

namespace cadd0040 {
namespace {

constexpr std::chrono::seconds kMilpTimeBudget{60};
constexpr std::size_t kMaxRounds = 4096;
constexpr std::size_t kViolationWindow = 96;
constexpr std::size_t kCandidateLimit = 4096;
constexpr std::size_t kResizeNodeLimit = 4096;

struct Violation {
    std::size_t path_idx = 0;
    double severity = 0.0;
};

struct Candidate {
    SkewMove move;
    double delta = 0.0;
};

std::chrono::seconds milp_time_budget() {
    if (const char* env_seconds = std::getenv("CADD0040_SA_SECONDS")) {
        return std::chrono::seconds(std::stoll(env_seconds));
    }
    return kMilpTimeBudget;
}

std::vector<std::size_t> incoming_edge_by_node(const SkewModel& model) {
    std::vector<std::size_t> incoming(model.node_count(), std::numeric_limits<std::size_t>::max());
    const auto& edges = model.tree_edges();
    for (std::size_t edge_idx = 0; edge_idx < edges.size(); ++edge_idx) {
        incoming[edges[edge_idx].child_idx] = edge_idx;
    }
    return incoming;
}

std::vector<int> fanout1_cells_by_area(const SkewModel& model) {
    std::vector<int> cell_indices;
    for (int cell_idx = 0; cell_idx < static_cast<int>(model.cell_count()); ++cell_idx) {
        const auto& cell = model.cells()[static_cast<std::size_t>(cell_idx)];
        if (!cell.ss_delays_by_fanout.empty() && !cell.ff_delays_by_fanout.empty()) {
            cell_indices.push_back(cell_idx);
        }
    }
    std::sort(cell_indices.begin(), cell_indices.end(), [&](int lhs, int rhs) {
        return model.cells()[static_cast<std::size_t>(lhs)].area <
               model.cells()[static_cast<std::size_t>(rhs)].area;
    });
    return cell_indices;
}

std::vector<Violation> worst_violations(const SkewModel& model) {
    std::vector<Violation> violations;
    violations.reserve(model.path_count());
    for (std::size_t path_idx = 0; path_idx < model.path_count(); ++path_idx) {
        const double ss = model.ss_slack()[path_idx];
        const double ff = model.ff_slack()[path_idx];
        const double severity = std::max(0.0, -ss) + std::max(0.0, -ff);
        if (severity > 0.0) {
            violations.push_back(Violation{path_idx, severity});
        }
    }
    std::sort(violations.begin(), violations.end(), [](const Violation& lhs, const Violation& rhs) {
        return lhs.severity > rhs.severity;
    });
    if (violations.size() > kViolationWindow) {
        violations.resize(kViolationWindow);
    }
    return violations;
}

bool consider_move(SkewModel& model, const Metrics& baseline_metrics, const SkewMove& move,
                   double before_score, Candidate& best) {
    if (!model.try_move(move)) {
        return false;
    }
    const double delta = model.score(baseline_metrics) - before_score;
    if (delta > best.delta) {
        best.move = move;
        best.delta = delta;
    }
    model.undo_move(move);
    return true;
}

bool apply_one_milp_round(SkewModel& model, const Metrics& baseline_metrics,
                          const std::vector<std::size_t>& incoming_edges,
                          const std::vector<int>& fanout1_cells) {
    Candidate best{SkewMove{SkewMoveKind::Insert}, 0.0};
    const double before_score = model.score(baseline_metrics);
    const auto violations = worst_violations(model);
    std::size_t candidates = 0;

    for (const auto& violation : violations) {
        const std::size_t path_idx = violation.path_idx;

        if (model.ss_slack()[path_idx] < 0.0) {
            const std::size_t capture = model.capture_node_index(path_idx);
            const std::size_t edge_idx = incoming_edges[capture];
            if (edge_idx < model.edge_count()) {
                for (const int cell_idx : fanout1_cells) {
                    consider_move(model, baseline_metrics,
                                  SkewMove{SkewMoveKind::Insert, edge_idx, 0, cell_idx},
                                  before_score, best);
                    if (++candidates >= kCandidateLimit) {
                        break;
                    }
                }
            }
        }

        if (candidates >= kCandidateLimit) {
            break;
        }

        if (model.ff_slack()[path_idx] < 0.0) {
            const std::size_t launch = model.launch_node_index(path_idx);
            const std::size_t edge_idx = incoming_edges[launch];
            if (edge_idx < model.edge_count()) {
                for (const int cell_idx : fanout1_cells) {
                    consider_move(model, baseline_metrics,
                                  SkewMove{SkewMoveKind::Insert, edge_idx, 0, cell_idx},
                                  before_score, best);
                    if (++candidates >= kCandidateLimit) {
                        break;
                    }
                }
            }
        }

        if (candidates >= kCandidateLimit) {
            break;
        }
    }

    if (violations.empty()) {
        for (std::size_t edge_idx = 0;
             edge_idx < model.edge_count() && candidates < kCandidateLimit; ++edge_idx) {
            const auto& inserted_cells = model.tree_edges()[edge_idx].inserted_cell_indices;
            if (inserted_cells.empty()) {
                continue;
            }
            const int insert_position = static_cast<int>(inserted_cells.size() - 1);
            consider_move(
                model, baseline_metrics,
                SkewMove{SkewMoveKind::Remove, edge_idx, 0, inserted_cells.back(), insert_position},
                before_score, best);
            ++candidates;
        }

        std::size_t resize_nodes = 0;
        for (std::size_t node_idx = 0;
             node_idx < model.node_count() && candidates < kCandidateLimit; ++node_idx) {
            const int old_cell_idx = model.cell_indices()[node_idx];
            if (old_cell_idx < 0) {
                continue;
            }
            ++resize_nodes;
            for (int cell_idx = 0; cell_idx < static_cast<int>(model.cell_count()); ++cell_idx) {
                consider_move(
                    model, baseline_metrics,
                    SkewMove{SkewMoveKind::Resize, 0, node_idx, cell_idx, 0, old_cell_idx},
                    before_score, best);
                if (++candidates >= kCandidateLimit) {
                    break;
                }
            }
            if (resize_nodes >= kResizeNodeLimit) {
                break;
            }
        }
    }

    if (best.delta <= 0.0) {
        return false;
    }
    return model.try_move(best.move);
}

}  // namespace

void MilpOptimizer::run(ClockTree& clock_tree, const DataPathGraph& data_path_graph,
                        const BufferLibrary& buffer_library, const OptimizerContext& context) {
    const Metrics& baseline_metrics = context.baseline_metrics;
    DebugProgress& debug = context.debug_progress;
    SkewModel model(clock_tree, data_path_graph, buffer_library);

    const auto incoming_edges = incoming_edge_by_node(model);
    const auto fanout1_cells = fanout1_cells_by_area(model);

    double current_score = model.score(baseline_metrics);
    double best_score = current_score;
    SkewModelState best_state = model.snapshot();
    Metrics best_metrics = sa::metrics_from_skew(best_state.metrics);

    const auto start_time = std::chrono::steady_clock::now();
    const auto deadline = start_time + milp_time_budget();

    std::size_t rounds = 0;
    while (rounds < kMaxRounds && std::chrono::steady_clock::now() < deadline) {
        if (!apply_one_milp_round(model, baseline_metrics, incoming_edges, fanout1_cells)) {
            break;
        }
        ++rounds;
        sa::maybe_update_best(model, baseline_metrics, current_score, best_score, best_state,
                              best_metrics);

        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
        debug.report_if_due(elapsed, best_metrics, baseline_metrics, current_score);
    }

    sa::materialize(clock_tree, best_state, model, buffer_library);
    model.restore(best_state);

    debug.log([&](std::ostream& os) {
        os << "MilpOptimizer: rounds = " << rounds << ", best score = " << best_score
           << ", restored score = " << model.score(baseline_metrics) << '\n';
    });
}

}  // namespace cadd0040
