#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <stdexcept>

#include "clock_tree.hpp"

namespace {

cadd0040::BufferLibrary make_buffer_library() {
    return cadd0040::BufferLibrary{
        {"BUF_X1", cadd0040::BufferCell{"BUF_X1", 1.0, 1.0, 1.0, {0.10}, {0.05}}},
        {"BUF_X4", cadd0040::BufferCell{"BUF_X4", 2.0, 1.0, 2.0, {0.08, 0.09}, {0.04, 0.05}}},
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

    CHECK_FALSE(
        clock_tree.insert_buffer("BUF_0", "FF_0", "NEW_BAD", "MISSING_CELL", buffer_library));
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

TEST_CASE("remove_buffer bypasses single-fanout buffers") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree_with_two_sinks();

    REQUIRE(clock_tree.insert_buffer("BUF_0", "FF_0", "NEW_BUF_0", "BUF_X1", buffer_library));
    CHECK(clock_tree.area(buffer_library) == Catch::Approx(3.0));
    CHECK(clock_tree.clock_skew("FF_1", "FF_0", buffer_library, cadd0040::Corner::SS) ==
          Catch::Approx(0.10));

    REQUIRE(clock_tree.remove_buffer("NEW_BUF_0"));

    CHECK_FALSE(clock_tree.contains_name("NEW_BUF_0"));
    CHECK(clock_tree.node("FF_0").parent_id == clock_tree.node("BUF_0").id);
    CHECK(clock_tree.area(buffer_library) == Catch::Approx(2.0));
    CHECK(clock_tree.clock_skew("FF_1", "FF_0", buffer_library, cadd0040::Corner::SS) ==
          Catch::Approx(0.0));
}

TEST_CASE("id-based clock tree edits can be undone") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree_with_two_sinks();

    const cadd0040::NodeId parent = clock_tree.node_id("BUF_0");
    const cadd0040::NodeId child = clock_tree.node_id("FF_0");
    const cadd0040::EdgeId edge = clock_tree.edge_between(parent, child);
    REQUIRE(edge != cadd0040::kInvalidEdgeId);

    const auto insert_edit =
        clock_tree.insert_buffer_on_edge(edge, "NEW_BUF_0", "BUF_X1", buffer_library);
    REQUIRE(insert_edit);
    CHECK(clock_tree.node("NEW_BUF_0").origin == cadd0040::NodeOrigin::Inserted);
    CHECK(clock_tree.node("NEW_BUF_0").alive);
    CHECK(clock_tree.node("FF_0").parent_id == clock_tree.node("NEW_BUF_0").id);

    clock_tree.undo(insert_edit);
    CHECK_FALSE(clock_tree.contains_name("NEW_BUF_0"));
    CHECK(clock_tree.node("FF_0").parent_id == clock_tree.node("BUF_0").id);
    CHECK_FALSE(clock_tree.node(insert_edit.buffer_id).alive);

    const auto resize_edit =
        clock_tree.resize_buffer(clock_tree.node_id("BUF_0"), "BUF_X4", buffer_library);
    CHECK_FALSE(resize_edit);
}

TEST_CASE("only inserted buffers can be removed through optimizer edit API") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree_with_two_sinks();

    CHECK_FALSE(clock_tree.remove_inserted_buffer(clock_tree.node_id("BUF_0")));

    REQUIRE(clock_tree.insert_buffer("BUF_0", "FF_0", "NEW_BUF_0", "BUF_X1", buffer_library));
    const auto remove_edit = clock_tree.remove_inserted_buffer(clock_tree.node_id("NEW_BUF_0"));
    REQUIRE(remove_edit);
    CHECK_FALSE(clock_tree.contains_name("NEW_BUF_0"));
    CHECK_FALSE(clock_tree.node(remove_edit.buffer_id).alive);
    CHECK(clock_tree.node("FF_0").parent_id == clock_tree.node("BUF_0").id);

    clock_tree.undo(remove_edit);
    CHECK(clock_tree.contains_name("NEW_BUF_0"));
    CHECK(clock_tree.node("NEW_BUF_0").alive);
    CHECK(clock_tree.node("FF_0").parent_id == clock_tree.node("NEW_BUF_0").id);
}

TEST_CASE("clock_skew lazily computes capture minus launch clock arrival for flip-flops") {
    const auto buffer_library = make_buffer_library();
    auto clock_tree = make_clock_tree_with_two_sinks();

    CHECK(clock_tree.clock_skew("FF_1", "FF_0", buffer_library, cadd0040::Corner::SS) ==
          Catch::Approx(0.0));

    REQUIRE(clock_tree.insert_buffer("BUF_0", "FF_0", "NEW_BUF_0", "BUF_X1", buffer_library));

    CHECK(clock_tree.clock_skew("FF_1", "FF_0", buffer_library, cadd0040::Corner::SS) ==
          Catch::Approx(0.10));
    CHECK(clock_tree.clock_skew("FF_1", "FF_0", buffer_library, cadd0040::Corner::FF) ==
          Catch::Approx(0.05));
    CHECK(clock_tree.clock_delay("FF_0", buffer_library, cadd0040::Corner::SS) ==
          Catch::Approx(0.19));
    CHECK_THROWS_AS(clock_tree.clock_skew("BUF_0", "FF_0", buffer_library, cadd0040::Corner::SS),
                    std::invalid_argument);
}
