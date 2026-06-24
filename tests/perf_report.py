#!/usr/bin/env python3
"""Generate an interactive HTML convergence report from stored performance baselines.

Reads every performance_reference/{scene}/{pipeline}.json file and produces a
self-contained HTML page with Chart.js charts — one chart per scene, one line
per pipeline.  No server required; just open the output file in a browser.

Usage:
    python tests/perf_report.py                      # writes perf_report.html
    python tests/perf_report.py --output my.html     # custom output path
    python tests/perf_report.py --open               # open in browser after generating
"""

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
PERF_REF_DIR = PROJECT_ROOT / "performance_reference"
DEFAULT_OUTPUT = PROJECT_ROOT / "perf_report.html"

# Stable colors for known pipelines; unknown ones get the fallback palette.
PIPELINE_COLORS = {
    "PathTracer": "#4e79a7",
    "RIS":        "#f28e2b",
}
FALLBACK_PALETTE = [
    "#e15759", "#76b7b2", "#59a14f", "#edc948",
    "#b07aa1", "#ff9da7", "#9c755f", "#bab0ac",
]


def load_all_data() -> dict:
    """Return {scene_name: {pipeline: stored_dict}} for every stored JSON."""
    scenes: dict = {}
    for ref_file in sorted(PERF_REF_DIR.glob("*/*.json")):
        scene_name = ref_file.parent.name
        pipeline = ref_file.stem
        with open(ref_file) as f:
            data = json.load(f)
        scenes.setdefault(scene_name, {})[pipeline] = data
    return scenes


def assign_colors(scenes: dict) -> dict:
    """Return {pipeline: hex_color} for every pipeline seen across all scenes."""
    all_pipelines = sorted({p for s in scenes.values() for p in s})
    fallback_iter = iter(FALLBACK_PALETTE)
    return {
        p: PIPELINE_COLORS.get(p, next(fallback_iter, "#888888"))
        for p in all_pipelines
    }


_HTML_TEMPLATE = """\
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>HdRestir — Convergence Report</title>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: system-ui, -apple-system, sans-serif;
      background: #f0f2f5;
      color: #1a1a2e;
      padding: 2rem;
    }
    header { margin-bottom: 2rem; }
    header h1 { font-size: 1.6rem; font-weight: 700; margin-bottom: 0.25rem; }
    header .meta { color: #6b7280; font-size: 0.85rem; }

    .scene-tabs {
      display: flex; flex-wrap: wrap; gap: 0.5rem; margin-bottom: 1.5rem;
    }
    .tab {
      padding: 0.4rem 1.1rem;
      border: 1.5px solid #d1d5db;
      border-radius: 20px;
      cursor: pointer;
      background: white;
      font-size: 0.9rem;
      font-weight: 500;
      color: #374151;
      transition: all 0.15s;
    }
    .tab:hover { border-color: #6366f1; color: #6366f1; }
    .tab.active { background: #6366f1; border-color: #6366f1; color: white; }

    .card {
      background: white;
      border: 1px solid #e5e7eb;
      border-radius: 12px;
      padding: 1.5rem;
      margin-bottom: 1.5rem;
      box-shadow: 0 1px 4px rgba(0,0,0,.05);
    }
    .card-title {
      font-size: 1rem; font-weight: 600; margin-bottom: 1rem; color: #374151;
    }
    .chart-wrap { position: relative; height: 420px; }

    .no-curve-notice {
      color: #9ca3af; font-style: italic; padding: 2rem 0; text-align: center;
    }

    table {
      width: 100%; border-collapse: collapse; font-size: 0.9rem;
    }
    thead th {
      text-align: left; padding: 0.65rem 1rem;
      background: #f9fafb; color: #6b7280; font-weight: 600;
      border-bottom: 1px solid #e5e7eb;
      font-size: 0.8rem; text-transform: uppercase; letter-spacing: .04em;
    }
    tbody td {
      padding: 0.65rem 1rem; border-bottom: 1px solid #f3f4f6; vertical-align: middle;
    }
    tbody tr:last-child td { border-bottom: none; }
    tbody tr:hover td { background: #fafafa; }
    .pip-dot {
      display: inline-block; width: 10px; height: 10px;
      border-radius: 50%; margin-right: 0.5rem; flex-shrink: 0;
    }
    .pip-cell { display: flex; align-items: center; font-weight: 600; }
    .badge { display: inline-block; padding: 0.2rem 0.6rem; border-radius: 12px; font-size: 0.78rem; font-weight: 600; }
    .badge-ok       { background: #d1fae5; color: #065f46; }
    .badge-exceeded { background: #fee2e2; color: #991b1b; }
    .mono { font-family: ui-monospace, monospace; font-size: 0.85rem; }
    .dim { color: #9ca3af; }
  </style>
</head>
<body>

<header>
  <h1>HdRestir — Convergence Report</h1>
  <p class="meta" id="meta"></p>
</header>

<div class="scene-tabs" id="scene-tabs"></div>

<div class="card" id="chart-card">
  <div class="card-title" id="chart-title">Convergence curve — RMSE vs reference</div>
  <div class="chart-wrap" id="chart-wrap">
    <canvas id="main-chart"></canvas>
  </div>
</div>

<div class="card">
  <div class="card-title">Summary</div>
  <table>
    <thead>
      <tr>
        <th>Pipeline</th>
        <th>Status</th>
        <th>Samples needed</th>
        <th>Render time (winning)</th>
        <th>Final RMSE</th>
        <th>Target</th>
        <th>Resolution level</th>
        <th>Stored</th>
      </tr>
    </thead>
    <tbody id="summary-body"></tbody>
  </table>
</div>

<script>
const DATA   = __DATA_PLACEHOLDER__;
const COLORS = __COLORS_PLACEHOLDER__;

const DEFAULT_COLORS = [
  "#e15759","#76b7b2","#59a14f","#edc948","#b07aa1","#ff9da7","#9c755f","#bab0ac"
];

let chartInstance = null;

// ── helpers ──────────────────────────────────────────────────────────────────

function fmtTime(s) {
  if (s == null) return "—";
  return s >= 60 ? (s / 60).toFixed(1) + " min" : s.toFixed(1) + " s";
}

function fmtSamples(n) {
  return n != null ? n.toLocaleString() : "—";
}

function pipelineColor(name, idx) {
  return COLORS[name] || DEFAULT_COLORS[idx % DEFAULT_COLORS.length];
}

// ── chart ─────────────────────────────────────────────────────────────────────

function buildChart(sceneName) {
  const scene = DATA.scenes[sceneName];
  const pipelines = Object.keys(scene);

  // Pipelines that have a convergence curve
  const curvedPipelines = pipelines.filter(p => scene[p].curve && scene[p].curve.length > 0);

  const wrap = document.getElementById("chart-wrap");

  if (curvedPipelines.length === 0) {
    wrap.innerHTML = '<p class="no-curve-notice">No convergence curve data for this scene.<br>Run <code>just perf-store &lt;pipeline&gt; "" &lt;variance&gt;</code> to record curves.</p>';
    return;
  }

  // Restore canvas if it was replaced by the notice
  if (!wrap.querySelector("canvas")) {
    wrap.innerHTML = '<canvas id="main-chart"></canvas>';
  }

  const datasets = [];

  curvedPipelines.forEach((pipeline, i) => {
    const pd = scene[pipeline];
    const color = pipelineColor(pipeline, i);
    datasets.push({
      label: pipeline,
      data: pd.curve.map(pt => ({ x: pt.samples, y: pt.rmse_vs_ref })),
      borderColor: color,
      backgroundColor: color + "22",
      pointBackgroundColor: color,
      pointRadius: 5,
      pointHoverRadius: 8,
      borderWidth: 2.5,
      tension: 0.15,
      fill: false,
    });
  });

  // Dashed target line (if any pipeline has target_variance)
  const firstTarget = pipelines.map(p => scene[p]).find(d => d.target_variance != null);
  if (firstTarget) {
    const allX = datasets.flatMap(d => d.data.map(pt => pt.x));
    const minX = Math.min(...allX);
    const maxX = Math.max(...allX);
    datasets.push({
      label: "Target (" + firstTarget.target_variance + ")",
      data: [{ x: minX, y: firstTarget.target_variance }, { x: maxX, y: firstTarget.target_variance }],
      borderColor: "#dc2626",
      borderDash: [6, 4],
      borderWidth: 1.5,
      pointRadius: 0,
      tension: 0,
      fill: false,
    });
  }

  const ctx = document.getElementById("main-chart").getContext("2d");
  if (chartInstance) chartInstance.destroy();

  chartInstance = new Chart(ctx, {
    type: "line",
    data: { datasets },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: { mode: "index", intersect: false },
      scales: {
        x: {
          type: "logarithmic",
          title: { display: true, text: "Sample count", color: "#6b7280", font: { size: 13 } },
          ticks: {
            color: "#6b7280",
            callback: v => {
              const log2 = Math.log2(v);
              return Number.isInteger(log2) ? v : "";
            },
          },
          grid: { color: "#f3f4f6" },
        },
        y: {
          title: { display: true, text: "RMSE vs reference", color: "#6b7280", font: { size: 13 } },
          min: 0,
          ticks: { color: "#6b7280" },
          grid: { color: "#f3f4f6" },
        },
      },
      plugins: {
        legend: {
          labels: { usePointStyle: true, font: { size: 13 }, color: "#374151" },
        },
        tooltip: {
          backgroundColor: "rgba(17,24,39,0.92)",
          titleColor: "#f9fafb",
          bodyColor: "#d1d5db",
          padding: 10,
          callbacks: {
            title: items => "Samples: " + items[0].raw.x.toLocaleString(),
            label: item => {
              if (!item.raw || item.raw.x == null) return item.dataset.label;
              const pipeline = item.dataset.label;
              const pd = scene[pipeline];
              const pt = pd && pd.curve && pd.curve.find(p => p.samples === item.raw.x);
              const time = pt ? "  (" + fmtTime(pt.render_time_seconds) + " render)" : "";
              return "  " + pipeline + ": " + item.raw.y.toFixed(6) + time;
            },
          },
        },
      },
    },
  });
}

// ── summary table ─────────────────────────────────────────────────────────────

function buildTable(sceneName) {
  const scene = DATA.scenes[sceneName];
  const pipelines = Object.keys(scene);
  const tbody = document.getElementById("summary-body");
  tbody.innerHTML = "";

  // Sort: converged first, then by samples_needed ascending
  const sorted = [...pipelines].sort((a, b) => {
    const da = scene[a], db = scene[b];
    if (da.exceeded !== db.exceeded) return da.exceeded ? 1 : -1;
    return (da.samples_needed ?? Infinity) - (db.samples_needed ?? Infinity);
  });

  sorted.forEach((pipeline, i) => {
    const d = scene[pipeline];
    const color = pipelineColor(pipeline, i);
    const ok = !d.exceeded;
    const badge = ok
      ? '<span class="badge badge-ok">✓ Converged</span>'
      : '<span class="badge badge-exceeded">⚠ Exceeded</span>';
    const rmse = d.final_noise_rmse != null ? d.final_noise_rmse.toFixed(6) : "—";
    const stored = d.stored_at ? d.stored_at.slice(0, 10) : "—";
    const resLabel = ["full", "½", "¼", "⅛", "1/16"][Math.min(d.resolution_level ?? 1, 4)];

    const tr = document.createElement("tr");
    tr.innerHTML =
      '<td><span class="pip-cell"><span class="pip-dot" style="background:' + color + '"></span>' + pipeline + '</span></td>' +
      "<td>" + badge + "</td>" +
      '<td class="mono">' + fmtSamples(d.samples_needed) + "</td>" +
      '<td class="mono">' + fmtTime(d.render_time_seconds) + "</td>" +
      '<td class="mono">' + rmse + "</td>" +
      '<td class="mono dim">' + (d.target_variance ?? "—") + "</td>" +
      '<td class="dim">' + resLabel + "</td>" +
      '<td class="dim">' + stored + "</td>";
    tbody.appendChild(tr);
  });
}

// ── scene switching ───────────────────────────────────────────────────────────

function renderScene(sceneName) {
  document.querySelectorAll(".tab").forEach(t =>
    t.classList.toggle("active", t.dataset.scene === sceneName)
  );
  document.getElementById("chart-title").textContent =
    "Convergence curve — " + sceneName;
  buildChart(sceneName);
  buildTable(sceneName);
}

// ── init ─────────────────────────────────────────────────────────────────────

(function init() {
  document.getElementById("meta").textContent =
    "Generated " + DATA.generated_at + "   •   " +
    Object.keys(DATA.scenes).length + " scene(s)   •   " +
    Object.values(DATA.scenes).flatMap(Object.keys).filter((v,i,a)=>a.indexOf(v)===i).length + " pipeline(s)";

  const tabsEl = document.getElementById("scene-tabs");
  const sceneNames = Object.keys(DATA.scenes);

  sceneNames.forEach(name => {
    const btn = document.createElement("button");
    btn.className = "tab";
    btn.dataset.scene = name;
    btn.textContent = name;
    btn.onclick = () => renderScene(name);
    tabsEl.appendChild(btn);
  });

  if (sceneNames.length > 0) renderScene(sceneNames[0]);
})();
</script>
</body>
</html>
"""


def generate(output_path: Path, open_browser: bool = False):
    scenes = load_all_data()
    if not scenes:
        print("No stored baselines found in performance_reference/")
        print("Run 'just perf-store' first.")
        sys.exit(1)

    colors = assign_colors(scenes)

    data_payload = {
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "scenes": scenes,
    }

    html = _HTML_TEMPLATE.replace(
        "__DATA_PLACEHOLDER__", json.dumps(data_payload, indent=2)
    ).replace(
        "__COLORS_PLACEHOLDER__", json.dumps(colors)
    )

    output_path.write_text(html, encoding="utf-8")
    print(f"Report written to: {output_path}")

    if open_browser:
        import webbrowser
        webbrowser.open(output_path.as_uri())


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT),
                        help="Output HTML file path (default: %(default)s)")
    parser.add_argument("--open", action="store_true",
                        help="Open the report in the default browser after generating")
    args = parser.parse_args()
    generate(Path(args.output), open_browser=args.open)


if __name__ == "__main__":
    main()
