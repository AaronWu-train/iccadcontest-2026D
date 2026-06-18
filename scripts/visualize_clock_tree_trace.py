#!/usr/bin/env python3
"""Generate a standalone HTML visualization from visual optimizer frames.json."""

import argparse
import json
from pathlib import Path


HTML_TEMPLATE = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Clock Tree Optimization Trace</title>
  <style>
    :root {
      color-scheme: light;
      --bg: #f5f7f8;
      --panel: #ffffff;
      --text: #16202a;
      --muted: #607080;
      --line: #bac4cf;
      --root: #34495e;
      --buffer: #4b7bec;
      --new-buffer: #e67e22;
      --modified-buffer: #f1c40f;
      --sink: #16a085;
      --accepted: #168a5b;
      --rejected: #bc3f3f;
      --kept: #46627f;
      --skipped: #7f8c8d;
    }

    * {
      box-sizing: border-box;
    }

    html {
      height: 100%;
    }

    body {
      height: 100%;
      margin: 0;
      overflow: hidden;
      background: var(--bg);
      color: var(--text);
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI",
        sans-serif;
    }

    .shell {
      display: grid;
      grid-template-rows: auto auto 1fr;
      height: 100vh;
      min-height: 0;
    }

    header {
      padding: 10px 16px 8px;
      background: var(--panel);
      border-bottom: 1px solid #d9e0e6;
    }

    h1 {
      margin: 0;
      font-size: 18px;
      font-weight: 700;
      letter-spacing: 0;
    }

    .subtitle {
      margin-top: 4px;
      color: var(--muted);
      font-size: 12px;
    }

    .toolbar {
      display: grid;
      grid-template-columns: auto auto auto 1fr auto;
      gap: 8px;
      align-items: center;
      padding: 8px 16px;
      background: #eef2f5;
      border-bottom: 1px solid #d9e0e6;
    }

    button {
      min-width: 38px;
      height: 30px;
      border: 1px solid #b8c3cc;
      background: var(--panel);
      color: var(--text);
      border-radius: 6px;
      font-size: 13px;
      cursor: pointer;
    }

    button:hover {
      border-color: #7f91a3;
    }

    input[type="range"] {
      width: 100%;
    }

    .counter {
      color: var(--muted);
      font-variant-numeric: tabular-nums;
      white-space: nowrap;
    }

    .main {
      display: grid;
      grid-template-columns: minmax(0, 1fr) 360px;
      min-height: 0;
    }

    .stage {
      min-height: 0;
      overflow: hidden;
      padding: 10px;
    }

    #treeSvg {
      display: block;
      width: 100%;
      height: 100%;
      min-height: 420px;
      background: var(--panel);
      border: 1px solid #d9e0e6;
      border-radius: 8px;
    }

    aside {
      border-left: 1px solid #d9e0e6;
      background: var(--panel);
      padding: 12px;
      overflow: auto;
    }

    .status {
      display: inline-flex;
      align-items: center;
      height: 26px;
      padding: 0 10px;
      border-radius: 999px;
      color: white;
      font-size: 13px;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0;
    }

    .status.accepted { background: var(--accepted); }
    .status.rejected { background: var(--rejected); }
    .status.kept { background: var(--kept); }
    .status.skipped { background: var(--skipped); }

    .section {
      margin-top: 12px;
    }

    .label {
      color: var(--muted);
      font-size: 11px;
      font-weight: 700;
      text-transform: uppercase;
      letter-spacing: 0;
    }

    .value {
      margin-top: 6px;
      font-size: 13px;
      line-height: 1.45;
    }

    .metrics {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 6px;
      margin-top: 6px;
    }

    .metric {
      padding: 7px;
      background: #f6f8fa;
      border: 1px solid #dce3e8;
      border-radius: 6px;
    }

    .metric .name {
      color: var(--muted);
      font-size: 10px;
    }

    .metric .number {
      margin-top: 4px;
      font-variant-numeric: tabular-nums;
      font-size: 12px;
      font-weight: 700;
    }

    .legend {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 6px;
      margin-top: 6px;
      font-size: 11px;
      color: var(--muted);
    }

    .legend-item {
      display: flex;
      align-items: center;
      gap: 6px;
      min-width: 0;
    }

    .swatch {
      width: 12px;
      height: 12px;
      border-radius: 3px;
      flex: 0 0 auto;
    }

    pre {
      white-space: pre;
      overflow: auto;
      max-height: 280px;
      padding: 9px;
      background: #101820;
      color: #e9eef2;
      border-radius: 6px;
      font-size: 11px;
      line-height: 1.35;
    }

    @media (max-width: 900px) {
      .toolbar {
        grid-template-columns: auto auto auto 1fr;
      }

      .counter {
        grid-column: 1 / -1;
      }

      .main {
        grid-template-columns: 1fr;
      }

      aside {
        border-left: 0;
        border-top: 1px solid #d9e0e6;
      }
    }
  </style>
</head>
<body>
  <div class="shell">
    <header>
      <h1>Clock Tree Optimization Trace</h1>
      <div class="subtitle">Direct ClockTree moves with accepted, rejected, and rollback frames</div>
    </header>

    <div class="toolbar">
      <button id="prevBtn" title="Previous frame" aria-label="Previous frame">&larr;</button>
      <button id="playBtn" title="Play or pause" aria-label="Play or pause">Play</button>
      <button id="nextBtn" title="Next frame" aria-label="Next frame">&rarr;</button>
      <input id="frameRange" type="range" min="0" value="0">
      <div id="counter" class="counter"></div>
    </div>

    <div class="main">
      <main class="stage">
        <svg id="treeSvg" role="img" aria-label="Clock tree visualization"></svg>
      </main>

      <aside>
        <span id="status" class="status kept">kept</span>

        <div class="section">
          <div class="label">Frame</div>
          <div id="frameTitle" class="value"></div>
        </div>

        <div class="section">
          <div class="label">Move</div>
          <div id="moveText" class="value"></div>
        </div>

        <div class="section">
          <div class="label">Affected Buffer</div>
          <div id="affectedText" class="value"></div>
          <div class="legend">
            <div class="legend-item"><span class="swatch" style="background: var(--modified-buffer)"></span>Accepted resize</div>
            <div class="legend-item"><span class="swatch" style="background: var(--new-buffer)"></span>Inserted</div>
            <div class="legend-item"><span class="swatch" style="background: var(--rejected)"></span>Rejected frame</div>
            <div class="legend-item"><span class="swatch" style="background: var(--accepted)"></span>Accepted frame</div>
          </div>
        </div>

        <div class="section">
          <div class="label">Metrics</div>
          <div class="metrics">
            <div class="metric"><div class="name">Score</div><div id="score" class="number"></div></div>
            <div class="metric"><div class="name">Area</div><div id="area" class="number"></div></div>
            <div class="metric"><div class="name">TNS SS</div><div id="tnsSs" class="number"></div></div>
            <div class="metric"><div class="name">WNS SS</div><div id="wnsSs" class="number"></div></div>
            <div class="metric"><div class="name">TNS FF</div><div id="tnsFf" class="number"></div></div>
            <div class="metric"><div class="name">WNS FF</div><div id="wnsFf" class="number"></div></div>
          </div>
        </div>

        <div class="section">
          <div class="label">Raw Clock Tree</div>
          <pre id="rawTree"></pre>
        </div>
      </aside>
    </div>
  </div>

  <script id="framesData" type="application/json">__FRAMES_JSON__</script>
  <script>
    const data = JSON.parse(document.getElementById("framesData").textContent);
    const frames = data.frames || [];
    const range = document.getElementById("frameRange");
    const prevBtn = document.getElementById("prevBtn");
    const nextBtn = document.getElementById("nextBtn");
    const playBtn = document.getElementById("playBtn");
    const counter = document.getElementById("counter");
    const svg = document.getElementById("treeSvg");
    let frameIndex = 0;
    let timer = null;

    range.max = Math.max(frames.length - 1, 0);

    function formatNumber(value) {
      return Number(value).toFixed(6);
    }

    function parseTree(text) {
      const lines = text.trimEnd().split(/\\n/).filter(Boolean);
      if (lines.length === 0) {
        return { name: "(empty)", cell: "", kind: "root", children: [] };
      }

      const rootName = lines[0].replace(/^Root:\\s*/, "").trim();
      const root = { name: rootName, cell: "ROOT", kind: "root", children: [] };
      const stack = [root];

      for (const line of lines.slice(1)) {
        const match = line.match(/^\\t*\\[(\\d+)\\]\\s+(\\S+)\\s+\\(([^)]*)\\)(?:\\s+\\(SINK\\))?/);
        if (!match) {
          continue;
        }

        const depth = Number(match[1]);
        const name = match[2];
        const cell = match[3];
        const isSink = line.includes("(SINK)");
        const kind = isSink ? "sink" : name.startsWith("NEW_VIS_BUF_") ? "new-buffer" : "buffer";
        const node = { name, cell, kind, children: [] };
        const parent = stack[depth - 1] || root;
        parent.children.push(node);
        stack[depth] = node;
        stack.length = depth + 1;
      }
      return root;
    }

    function eachNode(root, visitor) {
      const stack = [root];
      while (stack.length > 0) {
        const node = stack.pop();
        visitor(node);
        for (let i = node.children.length - 1; i >= 0; --i) {
          stack.push(node.children[i]);
        }
      }
    }

    function layoutTree(root) {
      let leafIndex = 0;
      let maxDepth = 0;

      function visit(node, depth) {
        node.depth = depth;
        maxDepth = Math.max(maxDepth, depth);
        if (node.children.length === 0) {
          node.x = leafIndex++;
          return node.x;
        }
        const childXs = node.children.map((child) => visit(child, depth + 1));
        node.x = childXs.reduce((sum, value) => sum + value, 0) / childXs.length;
        return node.x;
      }

      visit(root, 0);
      return { leafCount: Math.max(leafIndex, 1), maxDepth };
    }

    function affectedNameFromMove(move) {
      const patterns = [
        /(?:rollback after rejected )?insert\\s+(\\S+)/,
        /(?:rollback after rejected )?resize\\s+(\\S+)/,
        /(?:rollback after rejected )?remove\\s+(\\S+)/
      ];
      for (const pattern of patterns) {
        const match = move.match(pattern);
        if (match) {
          return match[1];
        }
      }
      return "";
    }

    function moveKind(move) {
      if (move.includes("insert ")) return "insert";
      if (move.includes("resize ")) return "resize";
      if (move.includes("remove ")) return "remove";
      return "";
    }

    function acceptedModifiedNames(untilIndex) {
      const names = new Set();
      for (let i = 0; i <= untilIndex; ++i) {
        const frame = frames[i];
        if (!frame || frame.status !== "accepted") {
          continue;
        }
        const name = affectedNameFromMove(frame.move || "");
        const kind = moveKind(frame.move || "");
        if (!name) {
          continue;
        }
        if (kind === "remove") {
          names.delete(name);
        } else {
          names.add(name);
        }
      }
      return names;
    }

    function shortName(name) {
      return name.replace("NEW_VIS_BUF_", "NVB_");
    }

    function nodeColor(node, modifiedNames) {
      const kind = node.kind;
      if (kind === "root") return "var(--root)";
      if (kind === "new-buffer") return "var(--new-buffer)";
      if (kind === "sink") return "var(--sink)";
      if (modifiedNames.has(node.name)) return "var(--modified-buffer)";
      return "var(--buffer)";
    }

    function nodeStroke(node, affectedName, frame) {
      if (node.name !== affectedName) {
        return "#ffffff";
      }
      if (frame.status === "rejected" || frame.phase === "rollback") {
        return "var(--rejected)";
      }
      if (frame.status === "accepted") {
        return "var(--accepted)";
      }
      return "#ffffff";
    }

    function nodeStrokeWidth(node, affectedName) {
      return node.name === affectedName ? "5" : "2";
    }

    function nodeTextColor(node, modifiedNames) {
      if (modifiedNames.has(node.name) && node.kind === "buffer") {
        return "var(--text)";
      }
      return "#ffffff";
    }

    function svgElement(name, attrs = {}) {
      const element = document.createElementNS("http://www.w3.org/2000/svg", name);
      for (const [key, value] of Object.entries(attrs)) {
        element.setAttribute(key, value);
      }
      return element;
    }

    function renderTree(frame, index) {
      const root = parseTree(frame.tree || "");
      const layout = layoutTree(root);
      const modifiedNames = acceptedModifiedNames(index);
      const affectedName = affectedNameFromMove(frame.move || "");
      const marginX = 38;
      const marginY = 34;
      const leafGap = 72;
      const levelGap = 70;
      const width = Math.max(760, marginX * 2 + layout.leafCount * leafGap);
      const height = Math.max(420, marginY * 2 + (layout.maxDepth + 1) * levelGap);

      svg.innerHTML = "";
      svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
      svg.setAttribute("width", "100%");
      svg.setAttribute("height", "100%");
      svg.setAttribute("preserveAspectRatio", "xMidYMid meet");

      eachNode(root, (node) => {
        node.px = marginX + node.x * leafGap;
        node.py = marginY + node.depth * levelGap;
      });

      eachNode(root, (node) => {
        for (const child of node.children) {
          svg.appendChild(svgElement("line", {
            x1: node.px,
            y1: node.py + 13,
            x2: child.px,
            y2: child.py - 16,
            stroke: "var(--line)",
            "stroke-width": "2"
          }));
        }
      });

      eachNode(root, (node) => {
        const group = svgElement("g");
        const fill = nodeColor(node, modifiedNames);
        const stroke = nodeStroke(node, affectedName, frame);
        const strokeWidth = nodeStrokeWidth(node, affectedName);
        const textFill = nodeTextColor(node, modifiedNames);
        if (node.kind === "sink") {
          group.appendChild(svgElement("circle", {
            cx: node.px,
            cy: node.py,
            r: 16,
            fill,
            stroke,
            "stroke-width": strokeWidth
          }));
        } else {
          group.appendChild(svgElement("rect", {
            x: node.px - 31,
            y: node.py - 17,
            width: 62,
            height: 34,
            rx: 7,
            fill,
            stroke,
            "stroke-width": strokeWidth
          }));
        }

        const name = svgElement("text", {
          x: node.px,
          y: node.py - 3,
          "text-anchor": "middle",
          fill: textFill,
          "font-size": "9",
          "font-weight": "700"
        });
        name.textContent = shortName(node.name);
        group.appendChild(name);

        const cell = svgElement("text", {
          x: node.px,
          y: node.py + 10,
          "text-anchor": "middle",
          fill: textFill,
          "font-size": "7"
        });
        cell.textContent = node.cell;
        group.appendChild(cell);

        svg.appendChild(group);
      });
    }

    function renderFrame(index) {
      if (frames.length === 0) {
        return;
      }
      frameIndex = Math.max(0, Math.min(index, frames.length - 1));
      const frame = frames[frameIndex];
      range.value = String(frameIndex);
      counter.textContent = `${frameIndex + 1} / ${frames.length}`;

      const status = document.getElementById("status");
      status.className = `status ${frame.status}`;
      status.textContent = frame.status;

      document.getElementById("frameTitle").textContent =
        `iteration ${frame.iteration} - ${frame.phase}`;
      document.getElementById("moveText").textContent = frame.move;
      document.getElementById("affectedText").textContent =
        affectedNameFromMove(frame.move || "") || "-";
      document.getElementById("score").textContent = formatNumber(frame.score);
      document.getElementById("area").textContent = formatNumber(frame.area);
      document.getElementById("tnsSs").textContent = formatNumber(frame.tns_ss);
      document.getElementById("wnsSs").textContent = formatNumber(frame.wns_ss);
      document.getElementById("tnsFf").textContent = formatNumber(frame.tns_ff);
      document.getElementById("wnsFf").textContent = formatNumber(frame.wns_ff);
      document.getElementById("rawTree").textContent = frame.tree;
      renderTree(frame, frameIndex);
    }

    function stopPlayback() {
      if (timer !== null) {
        clearInterval(timer);
        timer = null;
      }
      playBtn.textContent = "Play";
    }

    prevBtn.addEventListener("click", () => {
      stopPlayback();
      renderFrame(frameIndex - 1);
    });

    nextBtn.addEventListener("click", () => {
      stopPlayback();
      renderFrame(frameIndex + 1);
    });

    range.addEventListener("input", () => {
      stopPlayback();
      renderFrame(Number(range.value));
    });

    playBtn.addEventListener("click", () => {
      if (timer !== null) {
        stopPlayback();
        return;
      }
      playBtn.textContent = "Pause";
      timer = setInterval(() => {
        if (frameIndex >= frames.length - 1) {
          stopPlayback();
          return;
        }
        renderFrame(frameIndex + 1);
      }, 900);
    });

    window.addEventListener("keydown", (event) => {
      if (event.key === "ArrowLeft") {
        stopPlayback();
        renderFrame(frameIndex - 1);
      }
      if (event.key === "ArrowRight") {
        stopPlayback();
        renderFrame(frameIndex + 1);
      }
    });

    renderFrame(0);
  </script>
</body>
</html>
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "trace_dir",
        nargs="?",
        default="visual_trace",
        help="Directory containing frames.json from --optimizer visual",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="HTML output path. Defaults to <trace_dir>/index.html",
    )
    args = parser.parse_args()

    trace_dir = Path(args.trace_dir)
    frames_path = trace_dir / "frames.json"
    output_path = args.output if args.output is not None else trace_dir / "index.html"

    with frames_path.open("r", encoding="utf-8") as input_file:
        data = json.load(input_file)

    frames_json = json.dumps(data, ensure_ascii=False).replace("</", "<\\/")
    html = HTML_TEMPLATE.replace("__FRAMES_JSON__", frames_json)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(html, encoding="utf-8")
    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
