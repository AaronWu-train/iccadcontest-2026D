#include "optimization/factory.hpp"

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "optimization/optimizer.hpp"
#include "optimization/sa/annealing_optimizer.hpp"

namespace cadd0040 {
namespace {

using OptimizerCreator = std::function<std::unique_ptr<Optimizer>()>;

const std::map<std::string, OptimizerCreator>& optimizer_registry() {
    static const std::map<std::string, OptimizerCreator> registry = {
        {"dummy", [] { return std::make_unique<DummyOptimizer>(); }},
        {"anneal", [] { return std::make_unique<AnnealingOptimizer>(); }},
        {"sa", [] { return std::make_unique<AnnealingOptimizer>(); }},
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