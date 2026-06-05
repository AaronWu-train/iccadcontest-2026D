#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#ifndef _WIN32
#include <cstdlib>
#endif

#include "evaluation.hpp"
#include "optimization/annealing_optimizer.hpp"
#include "optimization/skew_model.hpp"

namespace {

cadd0040::BufferLibrary make_buffer_library() {
    return cadd0040::BufferLibrary{
        {"SMALL",
         cadd0040::BufferCell{
             .name = "SMALL",
             .width = 0.5,
             .height = 0.5,
             .area = 0.25,
             .ss_delays_by_fanout = {0.05, 0.06},
             .ff_delays_by_fanout = {0.02, 0.03},
         }},
        {"LARGE",
         cadd0040::BufferCell{
             .name = "LARGE",
             .width = 1.0,
             .height = 1.0,
             .area = 1.0,
             .ss_delays_by_fanout = {0.02, 0.03},
             .ff_delays_by_fanout = {0.01, 0.015},
         }},
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

int cell_index_by_name(const cadd0040::SkewModel& model, const std::string& cell_name) {
    for (int cell_idx = 0; cell_idx < static_cast<int>(model.cell_count()); ++cell_idx) {
        if (model.cells()[static_cast<std::size_t>(cell_idx)].name == cell_name) {
            return cell_idx;
        }
    }
    return -1;
}

cadd0040::Metrics metrics_from_model(const cadd0040::SkewModel& model) {
    const auto model_metrics = model.metrics();
    return cadd0040::Metrics{
        .tns_ss = model_metrics.tns_ss,
        .wns_ss = model_metrics.wns_ss,
        .tns_ff = model_metrics.tns_ff,
        .wns_ff = model_metrics.wns_ff,
        .area = model_metrics.area,
    };
}

}  // namespace

TEST_CASE("SkewModel incremental metrics match full evaluation", "[annealing]") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();

    cadd0040::SkewModel model(clock_tree, data_path_graph, buffer_library);
    const cadd0040::Metrics evaluated =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const cadd0040::Metrics modeled = metrics_from_model(model);

    CHECK(modeled.tns_ss == Catch::Approx(evaluated.tns_ss));
    CHECK(modeled.wns_ss == Catch::Approx(evaluated.wns_ss));
    CHECK(modeled.tns_ff == Catch::Approx(evaluated.tns_ff));
    CHECK(modeled.wns_ff == Catch::Approx(evaluated.wns_ff));
    CHECK(modeled.area == Catch::Approx(evaluated.area));
}

TEST_CASE("SkewModel insert move updates metrics consistently", "[annealing]") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree();
    const auto data_path_graph = make_data_path_graph();

    cadd0040::SkewModel model(clock_tree, data_path_graph, buffer_library);

    std::size_t capture_edge_idx = model.edge_count();
    for (std::size_t edge_idx = 0; edge_idx < model.edge_count(); ++edge_idx) {
        const auto& edge = model.tree_edges()[edge_idx];
        if (model.node_names()[edge.child_idx] == "FF_C") {
            capture_edge_idx = edge_idx;
            break;
        }
    }
    REQUIRE(capture_edge_idx < model.edge_count());

    const int small_cell_idx = cell_index_by_name(model, "SMALL");
    REQUIRE(small_cell_idx >= 0);

    cadd0040::SkewMove move{
        .kind = cadd0040::SkewMoveKind::Insert,
        .edge_idx = capture_edge_idx,
        .cell_idx = small_cell_idx,
    };
    REQUIRE(model.try_move(move));

    clock_tree.insert_buffer("BUF_C", "FF_C", "NEW_BUF_0", "SMALL", buffer_library);

    const cadd0040::Metrics evaluated =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const cadd0040::Metrics modeled = metrics_from_model(model);

    CHECK(modeled.tns_ss == Catch::Approx(evaluated.tns_ss));
    CHECK(modeled.wns_ss == Catch::Approx(evaluated.wns_ss));
    CHECK(modeled.tns_ff == Catch::Approx(evaluated.tns_ff));
    CHECK(modeled.wns_ff == Catch::Approx(evaluated.wns_ff));
    CHECK(modeled.area == Catch::Approx(evaluated.area));
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

    cadd0040::AnnealingOptimizer optimizer;
    optimizer.run(clock_tree, data_path_graph, buffer_library, baseline);

    const cadd0040::Metrics final_metrics =
        cadd0040::evaluate(clock_tree, data_path_graph, buffer_library);
    const double final_score = cadd0040::score(final_metrics, baseline);

    CHECK(final_score >= baseline_score - 1e-9);
    CHECK(clock_tree.contains_name("ROOT_CLK"));
    CHECK(clock_tree.contains_name("FF_L"));
    CHECK(clock_tree.contains_name("FF_C"));
}
