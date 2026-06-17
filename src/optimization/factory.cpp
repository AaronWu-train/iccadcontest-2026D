#include "optimization/factory.hpp"

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "optimization/greedy/greedy_optimizer.hpp"
#include "optimization/milp/milp_optimizer.hpp"
#include "optimization/optimizer.hpp"
#include "optimization/sa/annealing_optimizer.hpp"
#include "optimization/sa/iterated_sa_optimizer.hpp"
#include "optimization/tabu/tabu_optimizer.hpp"
#include "optimization/two_step/two_step_optimizer.hpp"
#include "optimization/visual/clock_tree_trace_optimizer.hpp"

namespace cadd0040 {
namespace {

using OptimizerCreator = std::function<std::unique_ptr<Optimizer>()>;

const std::map<std::string, OptimizerCreator>& optimizer_registry() {
    static const std::map<std::string, OptimizerCreator> registry = {
        {"dummy", [] { return std::make_unique<DummyOptimizer>(); }},
        {"A1",
         [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::RandomActionSpace); }},
        {"A2", [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::ViolationPath); }},
        {"A3", [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::UpstreamWindow); }},
        {"A4", [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::CriticalEndpoint); }},
        {"A5", [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::UnionPool); }},
        {"A6", [] { return std::make_unique<TwoStepOptimizeOptimizer>(); }},
        {"A7", [] { return std::make_unique<AnnealingOptimizer>(); }},
        {"A8", [] { return std::make_unique<IteratedSaOptimizer>(); }},
        {"A9", [] { return std::make_unique<TabuOptimizer>(); }},
        {"A10",
         [] {
             return std::make_unique<TwoStepOptimizeOptimizer>(CandidatePolicy::RandomActionSpace,
                                                               "two-step-random", "");
         }},
        {"A11",
         [] {
             return std::make_unique<AnnealingOptimizer>(CandidatePolicy::RandomActionSpace,
                                                         "sa-random", "");
         }},
        {"A12",
         [] {
             return std::make_unique<IteratedSaOptimizer>(CandidatePolicy::RandomActionSpace,
                                                          "isa-random", "");
         }},
        {"A13",
         [] {
             return std::make_unique<TabuOptimizer>(CandidatePolicy::RandomActionSpace,
                                                    "tabu-random", "");
         }},
        {"greedy-random",
         [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::RandomActionSpace); }},
        {"greedy-violation-path",
         [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::ViolationPath); }},
        {"greedy-upstream-window",
         [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::UpstreamWindow); }},
        {"greedy-critical-endpoint",
         [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::CriticalEndpoint); }},
        {"greedy-union-pool",
         [] { return std::make_unique<GreedyOptimizer>(CandidatePolicy::UnionPool); }},
        {"two-step-optimize", [] { return std::make_unique<TwoStepOptimizeOptimizer>(); }},
        {"two-step-union-pool", [] { return std::make_unique<TwoStepOptimizeOptimizer>(); }},
        {"two-step-random",
         [] {
             return std::make_unique<TwoStepOptimizeOptimizer>(CandidatePolicy::RandomActionSpace,
                                                               "two-step-random", "");
         }},
        {"sa", [] { return std::make_unique<AnnealingOptimizer>(); }},
        {"sa-sampled-union-pool", [] { return std::make_unique<AnnealingOptimizer>(); }},
        {"sa-random",
         [] {
             return std::make_unique<AnnealingOptimizer>(CandidatePolicy::RandomActionSpace,
                                                         "sa-random", "");
         }},
        {"isa", [] { return std::make_unique<IteratedSaOptimizer>(); }},
        {"isa-sampled-union-pool", [] { return std::make_unique<IteratedSaOptimizer>(); }},
        {"isa-random",
         [] {
             return std::make_unique<IteratedSaOptimizer>(CandidatePolicy::RandomActionSpace,
                                                          "isa-random", "");
         }},
        {"tabu", [] { return std::make_unique<TabuOptimizer>(); }},
        {"tabu-union-pool", [] { return std::make_unique<TabuOptimizer>(); }},
        {"tabu-random",
         [] {
             return std::make_unique<TabuOptimizer>(CandidatePolicy::RandomActionSpace,
                                                    "tabu-random", "");
         }},
        {"milp", [] { return std::make_unique<MilpOptimizer>(); }},
        {"visual", [] { return std::make_unique<ClockTreeTraceOptimizer>(); }},
    };

    return registry;
}

}  // namespace

std::unique_ptr<Optimizer> make_optimizer(std::string_view name) {
    const auto& registry = optimizer_registry();

    const auto it = registry.find(std::string(name));
    if (it == registry.end()) {
        throw std::runtime_error("Unknown optimizer: " + std::string(name));
    }

    return it->second();
}

std::vector<std::string> available_optimizers() {
    static std::vector<std::string> names;

    if (names.empty()) {
        for (const auto& [name, _] : optimizer_registry()) {
            names.push_back(name);
        }
    }

    return names;
}

}  // namespace cadd0040
