#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "evaluation.hpp"

namespace {

cadd0040::BufferLibrary make_eval_buffer_library() {
    return cadd0040::BufferLibrary{
        {"FAST",
         cadd0040::BufferCell{
             .name = "FAST",
             .width = 1.0,
             .height = 1.0,
             .area = 1.0,
             .ss_delays_by_fanout = {0.04},
             .ff_delays_by_fanout = {0.01},
         }},
        {"SLOW",
         cadd0040::BufferCell{
             .name = "SLOW",
             .width = 2.0,
             .height = 1.0,
             .area = 2.0,
             .ss_delays_by_fanout = {0.10},
             .ff_delays_by_fanout = {0.04},
         }},
    };
}

cadd0040::ClockTree make_eval_clock_tree() {
    cadd0040::ClockTree clock_tree;
    clock_tree.add_root("ROOT_CLK");
    clock_tree.add_node("BUF_L", "SLOW", cadd0040::NodeKind::Buffer, "ROOT_CLK");
    clock_tree.add_node("FF_L", "FIFO", cadd0040::NodeKind::FlipFlop, "BUF_L");
    clock_tree.add_node("BUF_C", "FAST", cadd0040::NodeKind::Buffer, "ROOT_CLK");
    clock_tree.add_node("FF_C", "FIFO", cadd0040::NodeKind::FlipFlop, "BUF_C");
    return clock_tree;
}

cadd0040::DataPathGraph make_eval_data_path_graph() {
    cadd0040::DataPathGraph graph;
    graph.set_clock_period(0.30);

    graph.add_edge("PathSetup", "FF_L", "FF_C");
    graph.set_delay("PathSetup", cadd0040::Corner::SS, 0.25);
    graph.set_delay("PathSetup", cadd0040::Corner::FF, 0.02);

    graph.add_edge("PathHold", "FF_C", "FF_L");
    graph.set_delay("PathHold", cadd0040::Corner::SS, 0.10);
    graph.set_delay("PathHold", cadd0040::Corner::FF, 0.01);

    return graph;
}

}  // namespace

TEST_CASE("evaluate computes setup and hold metrics without cached clock times") {
    const auto buffer_library = make_eval_buffer_library();
    const auto clock_tree = make_eval_clock_tree();
    const auto graph = make_eval_data_path_graph();

    const auto metrics = cadd0040::evaluate(clock_tree, graph, buffer_library);

    CHECK(metrics.tns_ss == Catch::Approx(-0.034));
    CHECK(metrics.wns_ss == Catch::Approx(-0.034));
    CHECK(metrics.tns_ff == Catch::Approx(-0.035));
    CHECK(metrics.wns_ff == Catch::Approx(-0.035));
    CHECK(metrics.area == Catch::Approx(3.0));
}

TEST_CASE("score uses explicit baseline and guards zero denominators") {
    const cadd0040::Metrics baseline{
        .tns_ss = -10.0,
        .wns_ss = -1.0,
        .tns_ff = -4.0,
        .wns_ff = -0.5,
        .area = 100.0,
    };
    const cadd0040::Metrics improved{
        .tns_ss = -5.0,
        .wns_ss = -0.5,
        .tns_ff = -2.0,
        .wns_ff = -0.25,
        .area = 90.0,
    };

    CHECK(cadd0040::score(baseline, baseline) == Catch::Approx(0.0));
    CHECK(cadd0040::score(improved, baseline) == Catch::Approx(2.1));

    const cadd0040::Metrics zero_baseline{};
    CHECK(cadd0040::score(zero_baseline, zero_baseline) == Catch::Approx(0.0));
}
