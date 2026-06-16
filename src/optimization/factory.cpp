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
#include "optimization/randomized_rcl/randomized_rcl_optimizer.hpp"
#include "optimization/repair_recover/repair_recover_optimizer.hpp"
#include "optimization/sa/annealing_optimizer.hpp"
#include "optimization/sa/iterated_sa_optimizer.hpp"
#include "optimization/tabu/tabu_optimizer.hpp"
#include "optimization/visual/clock_tree_trace_optimizer.hpp"

namespace cadd0040 {
namespace {

using OptimizerCreator = std::function<std::unique_ptr<Optimizer>()>;

const std::map<std::string, OptimizerCreator>& optimizer_registry() {
    static const std::map<std::string, OptimizerCreator> registry = {
        {"dummy", [] { return std::make_unique<DummyOptimizer>(); }},
        {"greedy-violation-path",
         [] { return std::make_unique<GreedyOptimizer>(GreedyCandidatePolicy::ViolationPath); }},
        {"sa", [] { return std::make_unique<AnnealingOptimizer>(); }},
        {"isa", [] { return std::make_unique<IteratedSaOptimizer>(); }},
        {"greedy-critical-endpoint",
         [] { return std::make_unique<GreedyOptimizer>(GreedyCandidatePolicy::CriticalEndpoint); }},
        {"greedy-upstream-window",
         [] { return std::make_unique<GreedyOptimizer>(GreedyCandidatePolicy::UpstreamWindow); }},
        {"greedy-repair-recover", [] { return std::make_unique<GreedyRepairRecoverOptimizer>(); }},
        {"greedy-randomized-rcl", [] { return std::make_unique<GreedyRandomizedRclOptimizer>(); }},
        {"tabu", [] { return std::make_unique<TabuOptimizer>(); }},
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
