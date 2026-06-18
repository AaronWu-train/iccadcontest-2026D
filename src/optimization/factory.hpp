#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "optimization/optimizer.hpp"

namespace cadd0040 {

inline constexpr std::string_view kDefaultOptimizerName = "tabu-random";

std::unique_ptr<Optimizer> make_optimizer(std::string_view name);

std::vector<std::string> available_optimizers();

}  // namespace cadd0040
