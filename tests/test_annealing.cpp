#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>
#include <vector>

#ifndef _WIN32
#include <cstdlib>
#endif

#include "evaluation.hpp"
#include "optimization/factory.hpp"
#include "optimization/greedy/greedy_optimizer.hpp"
#include "optimization/milp/milp_optimizer.hpp"
#include "optimization/optimizer_config.hpp"
#include "optimization/sa/annealing_optimizer.hpp"
#include "optimization/sa/iterated_sa_optimizer.hpp"
#include "optimization/timing_state.hpp"

namespace {

cadd0040::BufferLibrary make_buffer_library() {
    return cadd0040::BufferLibrary{
        {"SMALL", cadd0040::BufferCell{"SMALL", 0.5, 0.5, 0.25, {0.05, 0.06}, {0.02, 0.03}}},
        {"LARGE", cadd0040::BufferCell{"LARGE", 1.0, 1.0, 1.0, {0.02, 0.03}, {0.01, 0.015}}},
    };
}

cadd0040::ClockTree make_clock_tree() {
    cadd0040::ClockTree clock_tree;
    clock_tree.add_root("ROOT_CLK");
    clock_tree.add_node("BUF_L", "SMALL", cadd0040::NodeKind::Buffer, "ROOT_CLK");
    clock_tree.add_node("FF_L", "FIFO", cadd0040::NodeKind::FlipFlop, "BUF_L");
    clock_tree.add_node("BUF_C", "SMALL", cadd0040::NodeKind::Buffer, "ROOT_CLK");
    clock_tree.add_node("FF_C", "FIFO", cadd0040::NodeKind::FlipFlop, "BUF_C");
    return clock_tree;
}

cadd0040::DataPathGraph make_data_path_graph() {
    cadd0040::DataPathGraph graph;
    graph.set_clock_period(1.0);
    graph.add_edge("Path1", "FF_L", "FF_C");
    graph.set_delay("Path1", cadd0040::Corner::SS, 0.95);
    graph.set_delay("Path1", cadd0040::Corner::FF, 0.10);
    return graph;
}

cadd0040::Metrics metrics_from_timing(const cadd0040::TimingState& timing) {
    return timing.metrics();
}

cadd0040::OptimizerProgressEvent final_event_for_optimizer(const std::string& alias) {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();
    const cadd0040::Metrics baseline =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    cadd0040::DebugProgress debug_progress = cadd0040::DebugProgress::from_debug_flag(false);
    cadd0040::OptimizerProgressEvent final_event;
    cadd0040::OptimizerContext context{baseline, debug_progress};
    context.progress_interval = 1;
    context.progress_writer = [&](const cadd0040::OptimizerProgressEvent& event) {
        if (event.phase == "final" && event.event == "final") {
            final_event = event;
        }
    };
    auto optimizer = cadd0040::make_optimizer(alias);
    optimizer->run(clock_tree, data_path_graph, buffer_library, context);
    return final_event;
}

}  // namespace

TEST_CASE("TimingState initial metrics match full evaluation", "[timing]") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();

    cadd0040::TimingState timing(clock_tree, data_path_graph, buffer_library);
    const cadd0040::Metrics evaluated =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const cadd0040::Metrics modeled = metrics_from_timing(timing);

    CHECK(modeled.tns_ss == Catch::Approx(evaluated.tns_ss));
    CHECK(modeled.wns_ss == Catch::Approx(evaluated.wns_ss));
    CHECK(modeled.tns_ff == Catch::Approx(evaluated.tns_ff));
    CHECK(modeled.wns_ff == Catch::Approx(evaluated.wns_ff));
    CHECK(modeled.area == Catch::Approx(evaluated.area));
}

TEST_CASE("TimingState applies and undoes clock tree edits consistently", "[timing]") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();
    cadd0040::TimingState timing(clock_tree, data_path_graph, buffer_library);

    const cadd0040::NodeId parent = clock_tree.node_id("BUF_C");
    const cadd0040::NodeId child = clock_tree.node_id("FF_C");
    const cadd0040::EdgeId edge = clock_tree.edge_between(parent, child);
    REQUIRE(edge != cadd0040::kInvalidEdgeId);

    const auto insert_edit =
        clock_tree.insert_buffer_on_edge(edge, "NEW_BUF_0", "SMALL", buffer_library);
    REQUIRE(insert_edit);
    timing.apply(insert_edit);

    cadd0040::Metrics evaluated = cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    cadd0040::Metrics modeled = timing.metrics();
    CHECK(modeled.tns_ss == Catch::Approx(evaluated.tns_ss));
    CHECK(modeled.wns_ss == Catch::Approx(evaluated.wns_ss));
    CHECK(modeled.tns_ff == Catch::Approx(evaluated.tns_ff));
    CHECK(modeled.wns_ff == Catch::Approx(evaluated.wns_ff));
    CHECK(modeled.area == Catch::Approx(evaluated.area));

    const auto resize_edit =
        clock_tree.resize_buffer(clock_tree.node_id("NEW_BUF_0"), "LARGE", buffer_library);
    REQUIRE(resize_edit);
    timing.apply(resize_edit);

    evaluated = cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    modeled = timing.metrics();
    CHECK(modeled.tns_ss == Catch::Approx(evaluated.tns_ss));
    CHECK(modeled.wns_ss == Catch::Approx(evaluated.wns_ss));
    CHECK(modeled.tns_ff == Catch::Approx(evaluated.tns_ff));
    CHECK(modeled.wns_ff == Catch::Approx(evaluated.wns_ff));
    CHECK(modeled.area == Catch::Approx(evaluated.area));

    timing.undo(resize_edit);
    clock_tree.undo(resize_edit);
    timing.undo(insert_edit);
    clock_tree.undo(insert_edit);

    evaluated = cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    modeled = timing.metrics();
    CHECK(modeled.tns_ss == Catch::Approx(evaluated.tns_ss));
    CHECK(modeled.wns_ss == Catch::Approx(evaluated.wns_ss));
    CHECK(modeled.tns_ff == Catch::Approx(evaluated.tns_ff));
    CHECK(modeled.wns_ff == Catch::Approx(evaluated.wns_ff));
    CHECK(modeled.area == Catch::Approx(evaluated.area));
}

TEST_CASE("TimingState applies and undoes inserted buffer removal", "[timing]") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();
    cadd0040::TimingState timing(clock_tree, data_path_graph, buffer_library);

    const auto insert_edit = clock_tree.insert_buffer_on_edge(
        clock_tree.edge_between(clock_tree.node_id("BUF_C"), clock_tree.node_id("FF_C")),
        "NEW_BUF_0", "SMALL", buffer_library);
    REQUIRE(insert_edit);
    timing.apply(insert_edit);

    const auto remove_edit = clock_tree.remove_inserted_buffer(clock_tree.node_id("NEW_BUF_0"));
    REQUIRE(remove_edit);
    timing.apply(remove_edit);

    cadd0040::Metrics evaluated = cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    cadd0040::Metrics modeled = timing.metrics();
    CHECK(modeled.tns_ss == Catch::Approx(evaluated.tns_ss));
    CHECK(modeled.wns_ss == Catch::Approx(evaluated.wns_ss));
    CHECK(modeled.tns_ff == Catch::Approx(evaluated.tns_ff));
    CHECK(modeled.wns_ff == Catch::Approx(evaluated.wns_ff));
    CHECK(modeled.area == Catch::Approx(evaluated.area));

    timing.undo(remove_edit);
    clock_tree.undo(remove_edit);
    evaluated = cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    modeled = timing.metrics();
    CHECK(modeled.tns_ss == Catch::Approx(evaluated.tns_ss));
    CHECK(modeled.wns_ss == Catch::Approx(evaluated.wns_ss));
    CHECK(modeled.tns_ff == Catch::Approx(evaluated.tns_ff));
    CHECK(modeled.wns_ff == Catch::Approx(evaluated.wns_ff));
    CHECK(modeled.area == Catch::Approx(evaluated.area));
}

TEST_CASE("optimizer configs use legacy SA seconds override", "[optimization]") {
#ifndef _WIN32
    setenv("CADD0040_SA_SECONDS", "7", 1);
    CHECK(cadd0040::greedy_config_from_environment().time_budget.count() == 7);
    CHECK(cadd0040::milp_config_from_environment().time_budget.count() == 7);
    CHECK(cadd0040::sa_config_from_environment().time_budget.count() == 7);
    CHECK(cadd0040::isa_config_from_environment().time_budget.count() == 7);
    CHECK(cadd0040::two_step_config_from_environment().time_budget.count() == 7);
    CHECK(cadd0040::tabu_config_from_environment().time_budget.count() == 7);
    unsetenv("CADD0040_SA_SECONDS");
#endif
}

TEST_CASE("A1-A13 descriptive optimizer aliases are registered", "[optimization]") {
    const std::vector<std::string> aliases = {
        "greedy-random",
        "greedy-violation-path",
        "greedy-upstream-window",
        "greedy-critical-endpoint",
        "greedy-union-pool",
        "two-step-optimize",
        "two-step-union-pool",
        "two-step-random",
        "sa",
        "sa-sampled-union-pool",
        "sa-random",
        "isa",
        "isa-sampled-union-pool",
        "isa-random",
        "tabu",
        "tabu-union-pool",
        "tabu-random",
    };
    for (const auto& alias : aliases) {
        CHECK(cadd0040::make_optimizer(alias) != nullptr);
    }
}

TEST_CASE("A1-A13 numeric optimizer aliases are registered", "[optimization]") {
    const std::vector<std::string> aliases = {
        "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10", "A11", "A12", "A13",
    };
    for (const auto& alias : aliases) {
        CHECK(cadd0040::make_optimizer(alias) != nullptr);
    }
}

TEST_CASE("old optimizer aliases are not registered", "[optimization]") {
    const std::vector<std::string> aliases = {
        "greedy",
        "greedy-endpoint",
        "greedy-critical-ff",
        "greedy-root-window",
        "greedy-timing-area",
        "greedy-grasp",
        "greedy-repair-recover",
        "greedy-randomized-rcl",
        "sa-basic",
        "isa-basic",
        "anneal",
        "a1",
        "a9",
        "a10",
        "a13",
        "tabu-mixed",
    };
    for (const auto& alias : aliases) {
        CHECK_THROWS(cadd0040::make_optimizer(alias));
    }
}

TEST_CASE("A1-A13 optimizers run and leave an evaluable tree", "[optimization]") {
#ifndef _WIN32
    setenv("CADD0040_SA_SECONDS", "0", 1);
#endif

    const std::vector<std::string> aliases = {
        "A1", "A2", "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10", "A11", "A12", "A13",
    };
    for (const auto& alias : aliases) {
        const auto buffer_library = make_buffer_library();
        auto clock_tree = make_clock_tree();
        const auto data_path_graph = make_data_path_graph();
        const cadd0040::Metrics baseline =
            cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);

        cadd0040::DebugProgress debug_progress = cadd0040::DebugProgress::from_debug_flag(false);
        cadd0040::OptimizerContext context{baseline, debug_progress};
        auto optimizer = cadd0040::make_optimizer(alias);
        optimizer->run(clock_tree, data_path_graph, buffer_library, context);

        const cadd0040::Metrics final_metrics =
            cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
        CHECK(std::isfinite(cadd0040::score(final_metrics, baseline)));
        CHECK(clock_tree.contains_name("ROOT_CLK"));
        CHECK(clock_tree.contains_name("FF_L"));
        CHECK(clock_tree.contains_name("FF_C"));
    }

#ifndef _WIN32
    unsetenv("CADD0040_SA_SECONDS");
#endif
}

TEST_CASE("A6-A13 final progress reports expected policies", "[optimization]") {
#ifndef _WIN32
    setenv("CADD0040_SA_SECONDS", "0", 1);
#endif

    struct ExpectedPolicy {
        std::string alias;
        std::string candidate_policy;
        std::string accept_policy;
    };
    const std::vector<ExpectedPolicy> expected = {
        {"A6", "union_pool", "two_step_slack_then_score"},
        {"A7", "sampled_union_pool", "metropolis"},
        {"A8", "sampled_union_pool", "iterated_metropolis"},
        {"A9", "union_pool", "tabu_best_non_tabu"},
        {"A10", "random_action_space", "two_step_slack_then_score"},
        {"A11", "random_action_space", "metropolis"},
        {"A12", "random_action_space", "iterated_metropolis"},
        {"A13", "random_action_space", "tabu_best_non_tabu"},
        {"two-step-union-pool", "union_pool", "two_step_slack_then_score"},
        {"sa-sampled-union-pool", "sampled_union_pool", "metropolis"},
        {"isa-sampled-union-pool", "sampled_union_pool", "iterated_metropolis"},
        {"tabu-union-pool", "union_pool", "tabu_best_non_tabu"},
        {"two-step-random", "random_action_space", "two_step_slack_then_score"},
        {"sa-random", "random_action_space", "metropolis"},
        {"isa-random", "random_action_space", "iterated_metropolis"},
        {"tabu-random", "random_action_space", "tabu_best_non_tabu"},
    };
    for (const auto& entry : expected) {
        const cadd0040::OptimizerProgressEvent event = final_event_for_optimizer(entry.alias);
        CHECK(event.candidate_policy == entry.candidate_policy);
        CHECK(event.accept_policy == entry.accept_policy);
    }

#ifndef _WIN32
    unsetenv("CADD0040_SA_SECONDS");
#endif
}

TEST_CASE("GreedyOptimizer improves or preserves score on tiny testcase", "[optimization]") {
#ifndef _WIN32
    setenv("CADD0040_SA_SECONDS", "2", 1);
#endif

    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();

    const cadd0040::Metrics baseline =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double baseline_score = cadd0040::score(baseline, baseline);

    cadd0040::DebugProgress debug_progress = cadd0040::DebugProgress::from_debug_flag(false);
    cadd0040::OptimizerContext context{baseline, debug_progress};

    cadd0040::GreedyOptimizer optimizer;
    optimizer.run(clock_tree, data_path_graph, buffer_library, context);

    const cadd0040::Metrics final_metrics =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double final_score = cadd0040::score(final_metrics, baseline);

    CHECK(final_score >= baseline_score - 1e-9);
}

TEST_CASE("MilpOptimizer improves or preserves score on tiny testcase", "[optimization]") {
#ifndef _WIN32
    setenv("CADD0040_SA_SECONDS", "2", 1);
#endif

    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();

    const cadd0040::Metrics baseline =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double baseline_score = cadd0040::score(baseline, baseline);

    cadd0040::DebugProgress debug_progress = cadd0040::DebugProgress::from_debug_flag(false);
    cadd0040::OptimizerContext context{baseline, debug_progress};

    cadd0040::MilpOptimizer optimizer;
    optimizer.run(clock_tree, data_path_graph, buffer_library, context);

    const cadd0040::Metrics final_metrics =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double final_score = cadd0040::score(final_metrics, baseline);

    CHECK(final_score >= baseline_score - 1e-9);
}

TEST_CASE("AnnealingOptimizer improves or preserves score on tiny testcase", "[annealing]") {
#ifndef _WIN32
    setenv("CADD0040_SA_SECONDS", "2", 1);
#endif

    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();

    const cadd0040::Metrics baseline =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double baseline_score = cadd0040::score(baseline, baseline);

    cadd0040::DebugProgress debug_progress = cadd0040::DebugProgress::from_debug_flag(false);
    cadd0040::OptimizerContext context{baseline, debug_progress};

    cadd0040::AnnealingOptimizer optimizer;
    optimizer.run(clock_tree, data_path_graph, buffer_library, context);

    const cadd0040::Metrics final_metrics =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double final_score = cadd0040::score(final_metrics, baseline);

    CHECK(final_score >= baseline_score - 1e-9);
    CHECK(clock_tree.contains_name("ROOT_CLK"));
    CHECK(clock_tree.contains_name("FF_L"));
    CHECK(clock_tree.contains_name("FF_C"));
}

TEST_CASE("IteratedSaOptimizer improves or preserves score on tiny testcase", "[annealing]") {
#ifndef _WIN32
    setenv("CADD0040_SA_SECONDS", "2", 1);
#endif

    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();

    const cadd0040::Metrics baseline =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double baseline_score = cadd0040::score(baseline, baseline);

    cadd0040::DebugProgress debug_progress = cadd0040::DebugProgress::from_debug_flag(false);
    cadd0040::OptimizerContext context{baseline, debug_progress};

    cadd0040::IteratedSaOptimizer optimizer;
    optimizer.run(clock_tree, data_path_graph, buffer_library, context);

    const cadd0040::Metrics final_metrics =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double final_score = cadd0040::score(final_metrics, baseline);

    CHECK(final_score >= baseline_score - 1e-9);
    CHECK(clock_tree.contains_name("ROOT_CLK"));
    CHECK(clock_tree.contains_name("FF_L"));
    CHECK(clock_tree.contains_name("FF_C"));
}
