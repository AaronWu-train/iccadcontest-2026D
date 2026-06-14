to run visualization

make build
CADD0040_VISUAL_TRACE_DIR=visual_trace \
  CADD0040_VISUAL_GREEDY_WARMUP=$steps \
  CADD0040_VISUAL_SA_ITERATIONS=$steps \
  ./build/cadd0040 testcases/visual_demo testcases/visual_demo/modified_clk_tree.structure --optimizer visual

python3 scripts/visualize_clock_tree_trace.py visual_trace

then open corresponding HTML file