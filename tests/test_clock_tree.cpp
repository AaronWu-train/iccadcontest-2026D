#include <catch2/catch_test_macros.hpp>

#include "clock_tree.hpp"

namespace {

cadd0040::BufferLibrary make_buffer_library() {
    return cadd0040::BufferLibrary{
        {"BUF_X1",
         cadd0040::BufferCell{
             .name = "BUF_X1",
             .width = 1.0,
             .height = 1.0,
             .area = 1.0,
             .ss_delays_by_fanout = {0.10},
             .ff_delays_by_fanout = {0.05},
         }},
        {"BUF_X4",
         cadd0040::BufferCell{
             .name = "BUF_X4",
             .width = 2.0,
             .height = 1.0,
             .area = 2.0,
             .ss_delays_by_fanout = {0.08, 0.09},
             .ff_delays_by_fanout = {0.04, 0.05},
         }},
    };
}

cadd0040::ClockTree make_clock_tree_with_two_sinks() {
    cadd0040::ClockTree clock_tree;
    clock_tree.add_root("ROOT_CLK");
    clock_tree.add_node("BUF_0", "BUF_X4", cadd0040::NodeKind::Buffer, "ROOT_CLK");
    clock_tree.add_node("FF_0", "FIFO", cadd0040::NodeKind::FlipFlop, "BUF_0");
    clock_tree.add_node("FF_1", "FIFO", cadd0040::NodeKind::FlipFlop, "BUF_0");
    return clock_tree;
}

}  // namespace

TEST_CASE("insert_buffer validates buffer cell fanout before mutating") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree_with_two_sinks();

    CHECK_FALSE(clock_tree.insert_buffer("BUF_0", "FF_0", "NEW_BAD", "MISSING_CELL",
                                         buffer_library));
    CHECK_FALSE(clock_tree.contains_name("NEW_BAD"));
    CHECK(clock_tree.node("FF_0").parent_id == clock_tree.node("BUF_0").id);

    CHECK(clock_tree.insert_buffer("BUF_0", "FF_0", "NEW_BUF_0", "BUF_X1", buffer_library));
    CHECK(clock_tree.contains_name("NEW_BUF_0"));
    CHECK(clock_tree.node("FF_0").parent_id == clock_tree.node("NEW_BUF_0").id);
}

TEST_CASE("resize_buffer validates replacement cell fanout before mutating") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree_with_two_sinks();

    CHECK_FALSE(clock_tree.resize_buffer("BUF_0", "BUF_X1", buffer_library));
    CHECK(clock_tree.node("BUF_0").cell_type == "BUF_X4");

    CHECK_FALSE(clock_tree.resize_buffer("BUF_0", "MISSING_CELL", buffer_library));
    CHECK(clock_tree.node("BUF_0").cell_type == "BUF_X4");

    CHECK(clock_tree.resize_buffer("BUF_0", "BUF_X4", buffer_library));
    CHECK(clock_tree.node("BUF_0").cell_type == "BUF_X4");
}
