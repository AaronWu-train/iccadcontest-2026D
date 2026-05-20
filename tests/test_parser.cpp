#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "parser.hpp"
#include "types.hpp"

// These tests document the expected clk_tree.structure parsing behavior.
// They intentionally build small temporary input files instead of relying on
// external testcase files, so the parser contract remains stable even before
// official contest testcases are added to the repository.

namespace {

// Helper for parser tests: writes one temporary input file and returns its path.
std::filesystem::path write_temp_file(const std::string& filename, const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / filename;
    std::ofstream output(path);
    output << content;
    return path;
}

}  // namespace

TEST_CASE("parse_clock_tree builds a clock tree from level notation") {
    // The parser uses [level] indentation to recover parent-child relationships.
    // SINK nodes become FlipFlop nodes; other entries become Buffer nodes.
    const auto path = write_temp_file(
        "cadd0040_clk_tree.structure",
        "Root: ROOT_CLK\n"
        "[1] BUF_0 (REALBUF_X8)\n"
        "[2] BUF_4 (REALBUF_X4)\n"
        "[3] FF_37 (FIFO) (SINK)\n"
        "[3] FF_12 (FIFO) (SINK)\n"
        "[2] FF_24 (FIFO) (SINK)\n");

    cadd0040::ClockTree clock_tree;
    cadd0040::parse_clock_tree(path, clock_tree);

    REQUIRE(clock_tree.size() == 6);
    REQUIRE(clock_tree.root_id() != cadd0040::kInvalidNodeId);
    CHECK(clock_tree.node(clock_tree.root_id()).name == "ROOT_CLK");

    const auto buf0_id = clock_tree.find_node("BUF_0");
    const auto buf4_id = clock_tree.find_node("BUF_4");
    const auto ff37_id = clock_tree.find_node("FF_37");
    const auto ff24_id = clock_tree.find_node("FF_24");

    REQUIRE(buf0_id != cadd0040::kInvalidNodeId);
    REQUIRE(buf4_id != cadd0040::kInvalidNodeId);
    REQUIRE(ff37_id != cadd0040::kInvalidNodeId);
    REQUIRE(ff24_id != cadd0040::kInvalidNodeId);

    CHECK(clock_tree.node(buf0_id).kind == cadd0040::NodeKind::Buffer);
    CHECK(clock_tree.node(ff37_id).kind == cadd0040::NodeKind::FlipFlop);
    CHECK(clock_tree.node(buf0_id).parent_id == clock_tree.root_id());
    CHECK(clock_tree.node(buf4_id).parent_id == buf0_id);
    CHECK(clock_tree.node(ff37_id).parent_id == buf4_id);
    CHECK(clock_tree.node(ff24_id).parent_id == buf0_id);
}

TEST_CASE("parse_clock_tree preserves preorder output order and depth") {
    // The output writer will later use preorder_with_depth() to emit
    // modified_clk_tree.structure, so parsing must preserve child order.
    const auto path = write_temp_file(
        "cadd0040_clk_tree_preorder.structure",
        "Root: ROOT_CLK\n"
        "[1] BUF_0 (REALBUF_X8)\n"
        "[2] FF_0 (FIFO) (SINK)\n"
        "[1] BUF_1 (REALBUF_X4)\n");

    cadd0040::ClockTree clock_tree;
    cadd0040::parse_clock_tree(path, clock_tree);

    const auto traversal = clock_tree.preorder_with_depth();

    REQUIRE(traversal.size() == 4);
    CHECK(clock_tree.node(traversal[0].node_id).name == "ROOT_CLK");
    CHECK(traversal[0].depth == 0);
    CHECK(clock_tree.node(traversal[1].node_id).name == "BUF_0");
    CHECK(traversal[1].depth == 1);
    CHECK(clock_tree.node(traversal[2].node_id).name == "FF_0");
    CHECK(traversal[2].depth == 2);
    CHECK(clock_tree.node(traversal[3].node_id).name == "BUF_1");
    CHECK(traversal[3].depth == 1);
}

TEST_CASE("parse_clock_tree rejects missing parent levels") {
    // A [2] line cannot appear before any [1] parent. Rejecting this early
    // prevents the solver from optimizing an invalid or disconnected tree.
    const auto path = write_temp_file(
        "cadd0040_bad_clk_tree.structure",
        "Root: ROOT_CLK\n"
        "[2] FF_0 (FIFO) (SINK)\n");

    cadd0040::ClockTree clock_tree;

    CHECK_THROWS(cadd0040::parse_clock_tree(path, clock_tree));
}
