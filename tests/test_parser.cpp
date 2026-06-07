#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
    if (!output.is_open()) {
        FAIL("Failed to open temporary file for writing: " << path.string());
    }
    output << content;
    return path;
}

}  // namespace

TEST_CASE("parse_clock_tree builds a clock tree from level notation") {
    // The parser uses [level] indentation to recover parent-child relationships.
    // SINK nodes become FlipFlop nodes; other entries become Buffer nodes.
    const auto path = write_temp_file("cadd0040_clk_tree.structure",
                                      "Root: ROOT_CLK\n"
                                      "[1] BUF_0 (REALBUF_X8)\n"
                                      "[2] BUF_4 (REALBUF_X4)\n"
                                      "[3] FF_37 (FIFO) (SINK)\n"
                                      "[3] FF_12 (FIFO) (SINK)\n"
                                      "[2] FF_24 (FIFO) (SINK)\n");

    cadd0040::ClockTree clock_tree;
    cadd0040::parse_clock_tree(path, clock_tree);

    REQUIRE(clock_tree.size() == 6);
    CHECK(clock_tree.root_name() == "ROOT_CLK");

    REQUIRE(clock_tree.contains_name("BUF_0"));
    REQUIRE(clock_tree.contains_name("BUF_4"));
    REQUIRE(clock_tree.contains_name("FF_37"));
    REQUIRE(clock_tree.contains_name("FF_24"));

    CHECK(clock_tree.node("BUF_0").kind == cadd0040::NodeKind::Buffer);
    CHECK(clock_tree.node("FF_37").kind == cadd0040::NodeKind::FlipFlop);
    CHECK(clock_tree.path_from_root("FF_37") ==
          std::vector<std::string>{"ROOT_CLK", "BUF_0", "BUF_4", "FF_37"});
    CHECK(clock_tree.path_from_root("FF_24") ==
          std::vector<std::string>{"ROOT_CLK", "BUF_0", "FF_24"});
}

TEST_CASE("parse_clock_tree preserves preorder output order and depth") {
    // The output writer will later use preorder_with_depth() to emit
    // modified_clk_tree.structure, so parsing must preserve child order.
    const auto path = write_temp_file("cadd0040_clk_tree_preorder.structure",
                                      "Root: ROOT_CLK\n"
                                      "[1] BUF_0 (REALBUF_X8)\n"
                                      "[2] FF_0 (FIFO) (SINK)\n"
                                      "[1] BUF_1 (REALBUF_X4)\n");

    cadd0040::ClockTree clock_tree;
    cadd0040::parse_clock_tree(path, clock_tree);

    const auto traversal = clock_tree.preorder_with_depth();

    REQUIRE(traversal.size() == 4);
    CHECK(traversal[0].node_name == "ROOT_CLK");
    CHECK(traversal[0].depth == 0);
    CHECK(traversal[1].node_name == "BUF_0");
    CHECK(traversal[1].depth == 1);
    CHECK(traversal[2].node_name == "FF_0");
    CHECK(traversal[2].depth == 2);
    CHECK(traversal[3].node_name == "BUF_1");
    CHECK(traversal[3].depth == 1);
}

TEST_CASE("parse_clock_tree output matches its original structure file") {
    const std::string original_structure =
        "Root: ROOT_CLK\n"
        "\t[1] BUF_0 (REALBUF_X8)\n"
        "\t\t[2] BUF_4 (REALBUF_X4)\n"
        "\t\t\t[3] FF_37 (FIFO) (SINK)\n"
        "\t\t\t[3] FF_12 (FIFO) (SINK)\n"
        "\t\t[2] FF_24 (FIFO) (SINK)\n"
        "\t[1] BUF_1 (REALBUF_X2)\n";
    const auto path = write_temp_file("cadd0040_clk_tree_round_trip.structure", original_structure);

    cadd0040::ClockTree clock_tree;
    cadd0040::parse_clock_tree(path, clock_tree);

    std::ostringstream output;
    output << clock_tree;

    CHECK(output.str() == original_structure);
}

TEST_CASE("parse_clock_tree rejects missing parent levels") {
    // A [2] line cannot appear before any [1] parent. Rejecting this early
    // prevents the solver from optimizing an invalid or disconnected tree.
    const auto path = write_temp_file("cadd0040_bad_clk_tree.structure",
                                      "Root: ROOT_CLK\n"
                                      "[2] FF_0 (FIFO) (SINK)\n");

    cadd0040::ClockTree clock_tree;

    CHECK_THROWS(cadd0040::parse_clock_tree(path, clock_tree));
}

TEST_CASE("parse_buffer_library builds fanout-indexed buffer cells") {
    const auto path = write_temp_file("cadd0040_buf.lib",
                                      "cell (REALBUF_X2) {\n"
                                      "SIZE 0.5 BY 0.5\n"
                                      "SS_DELAY 0.051 0.062 0.085 0.108\n"
                                      "FF_DELAY 0.020 0.029 0.039 0.05\n"
                                      "}\n"
                                      "\n"
                                      "cell(REALBUF_X8) {\n"
                                      "SIZE 2 BY 0.5\n"
                                      "SS_DELAY 0.015 0.022 0.032 0.05 0.076\n"
                                      "FF_DELAY 0.006 0.0098 0.0152 0.0247 0.0372\n"
                                      "}\n");

    cadd0040::BufferLibrary buffer_library;
    cadd0040::parse_buffer_library(path, buffer_library);

    REQUIRE(buffer_library.size() == 2);
    REQUIRE(buffer_library.find("REALBUF_X2") != buffer_library.end());
    REQUIRE(buffer_library.find("REALBUF_X8") != buffer_library.end());

    const auto& x2 = buffer_library.at("REALBUF_X2");
    CHECK(x2.name == "REALBUF_X2");
    CHECK(x2.width == Catch::Approx(0.5));
    CHECK(x2.height == Catch::Approx(0.5));
    CHECK(x2.area == Catch::Approx(0.25));
    REQUIRE(x2.ss_delays_by_fanout.size() == 4);
    REQUIRE(x2.ff_delays_by_fanout.size() == 4);
    CHECK(x2.ss_delays_by_fanout[0] == Catch::Approx(0.051));
    CHECK(x2.ss_delays_by_fanout[3] == Catch::Approx(0.108));
    CHECK(x2.ff_delays_by_fanout[0] == Catch::Approx(0.020));
    CHECK(x2.ff_delays_by_fanout[3] == Catch::Approx(0.05));

    const auto& x8 = buffer_library.at("REALBUF_X8");
    CHECK(x8.area == Catch::Approx(1.0));
    REQUIRE(x8.ss_delays_by_fanout.size() == 5);
    REQUIRE(x8.ff_delays_by_fanout.size() == 5);
    CHECK(x8.ss_delays_by_fanout[4] == Catch::Approx(0.076));
    CHECK(x8.ff_delays_by_fanout[4] == Catch::Approx(0.0372));
}

TEST_CASE("parse_buffer_library rejects incomplete cells") {
    const auto path = write_temp_file("cadd0040_bad_buf.lib",
                                      "cell (REALBUF_X2) {\n"
                                      "SIZE 0.5 BY 0.5\n"
                                      "SS_DELAY 0.051 0.062\n"
                                      "}\n");

    cadd0040::BufferLibrary buffer_library;

    CHECK_THROWS(cadd0040::parse_buffer_library(path, buffer_library));
}
