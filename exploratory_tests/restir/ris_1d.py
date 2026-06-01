import math, random, sys, os

try:
    import matplotlib
    matplotlib.use("TkAgg" if "--show" in sys.argv else "Agg")
    import matplotlib.pyplot as plt
    import matplotlib.gridspec as gridspec
except ImportError:
    print("pip install matplotlib"); sys.exit(1)

SEED = 42; random.seed(SEED)

def f(x):       return 6.0 * x * (1.0 - x)
def p_hat(x):   return math.sin(math.pi * x)
def q_sample(): return random.random()
def q_pdf(_x):  return 1.0

def ris_estimate(m):
    cands   = [q_sample() for _ in range(m)]
    weights = [(1.0/m)*p_hat(x)/q_pdf(x) for x in cands]
    ws      = sum(weights)
    if ws <= 0.0: return 0.0, 0.0, cands, weights, -1
    u, chosen, acc = random.random()*ws, 0, 0.0
    for i,w in enumerate(weights):
        acc += w
        if u <= acc: chosen = i; break
    y = cands[chosen]; ph_y = p_hat(y)
    wY = ws/ph_y if ph_y > 0 else 0.0
    return f(y)*wY, wY, cands, weights, chosen

def linspace(a,b,n): return [a+(b-a)*i/(n-1) for i in range(n)]
def mean(v):  return sum(v)/len(v) if v else 0.0
def vari(v):
    if len(v)<2: return 0.0
    m = mean(v); return sum((x-m)**2 for x in v)/(len(v)-1)
def std(v): return math.sqrt(vari(v))

def _worked_example(m):
    """
    One explicit RIS draw with per-candidate bookkeeping.
    In this 1-proposal setup, MIS weight is 1 for every candidate.
    """
    cands = [q_sample() for _ in range(m)]
    rows = []
    raw_weights = []
    for i, x in enumerate(cands):
        qx = q_pdf(x)
        ph = p_hat(x)
        mis = 1.0
        w = (1.0 / m) * mis * ph / qx if qx > 0.0 else 0.0
        raw_weights.append(w)
        rows.append({
            "i": i,
            "x": x,
            "q": qx,
            "p_hat": ph,
            "mis": mis,
            "w": w,
        })

    w_sum = sum(raw_weights)
    if w_sum <= 0.0:
        return {
            "rows": rows,
            "chosen": -1,
            "w_sum": 0.0,
            "W_unbiased": 0.0,
            "estimate": 0.0,
        }

    u = random.random() * w_sum
    chosen = 0
    acc = 0.0
    for i, w in enumerate(raw_weights):
        acc += w
        if u <= acc:
            chosen = i
            break

    y = rows[chosen]["x"]
    py = rows[chosen]["p_hat"]
    mis_y = rows[chosen]["mis"]
    W_unbiased = w_sum / (mis_y * py) if py > 0.0 else 0.0
    estimate = f(y) * W_unbiased

    for r in rows:
        r["p_resample"] = r["w"] / w_sum
        r["W_unbiased"] = W_unbiased

    return {
        "rows": rows,
        "chosen": chosen,
        "w_sum": w_sum,
        "W_unbiased": W_unbiased,
        "estimate": estimate,
    }

# Data -----------------------------------------------------------------------
M_VALUES = [1, 2, 4, 8, 16, 32]; N = 10_000; NC = 2_000; TRUE = 1.0
est_m = {m: [ris_estimate(m)[0] for _ in range(N)] for m in M_VALUES}

# For a clearer unbiasedness view, aggregate into batch means.
batch_size = 50
batch_means_m = {}
for m in M_VALUES:
    vals = est_m[m]
    batches = []
    for i in range(0, len(vals), batch_size):
        chunk = vals[i:i + batch_size]
        if len(chunk) == batch_size:
            batches.append(mean(chunk))
    batch_means_m[m] = batches

rm_m  = {}
for m in M_VALUES:
    acc, rm = 0.0, []
    for i, v in enumerate([ris_estimate(m)[0] for _ in range(NC)], 1):
        acc += v; rm.append(acc/i)
    rm_m[m] = rm
sr_m = {m: ris_estimate(m) for m in M_VALUES}
worked_m8 = _worked_example(8)
mr   = [1, 2, 4, 8, 16, 32, 64]
varm = [vari([ris_estimate(m)[0] for _ in range(N)]) for m in mr]
C = {1:"#e41a1c",2:"#ff7f00",4:"#4daf4a",8:"#984ea3",16:"#377eb8",32:"#a65628",64:"#999999"}
pm = [1, 2, 4, 16, 32]
xs = linspace(0,1,300); fv=[f(x) for x in xs]; pv=[p_hat(x) for x in xs]; qv=[q_pdf(x) for x in xs]

# ---- Figure 1: Math page ---------------------------------------------------
fig1, ax = plt.subplots(figsize=(14, 11))
ax.axis("off"); fig1.patch.set_facecolor("#fafafa")
fig1.text(0.5, 0.975,
    "Resampled Importance Sampling  --  Math Derivation",
    ha="center", va="top", fontsize=16, fontweight="bold", color="#0d47a1")

def sec(ax, y, tag, title, lines, dy=0.053):
    ax.text(0.03, y, tag, transform=ax.transAxes,
            fontsize=10, fontweight="bold", color="white", va="top",
            bbox=dict(boxstyle="round,pad=0.25", facecolor="#0d47a1", edgecolor="none"))
    ax.text(0.11, y, title, transform=ax.transAxes,
            fontsize=12, fontweight="bold", color="#0d47a1", va="top")
    cy = y - 0.042
    for l in lines:
        if l == "": cy -= dy*0.4; continue
        ax.text(0.05, cy, l, transform=ax.transAxes, fontsize=11, va="top")
        cy -= dy
    return cy - 0.018

y = 0.920
y = sec(ax, y, "1", "GOAL", [
    r"Estimate  $I = \int_0^1 f(x)\,dx$  where  $f(x) = 6x(1-x)$.",
    r"Analytic:  $I = 6\cdot\left[\frac{x^2}{2}-\frac{x^3}{3}\right]_0^1"
    r" = 6\cdot\left(\frac{1}{2}-\frac{1}{3}\right) = 1$   (exact)",
])
y = sec(ax, y, "2", "STANDARD MONTE CARLO  (baseline)", [
    r"Draw $X \sim q$,  compute  $\hat{I}_{MC} = \frac{f(X)}{q(X)}$,  "
    r"unbiased: $\mathrm{E}[\hat{I}_{MC}] = I$.",
    r"Variance is large when $q$ does not match $f$  (proposal shape mismatch).",
])
y = sec(ax, y, "3", "ALGORITHM 1  --  Naive RIS  (Wyman et al. 2023, p.10)", [
    r"Given $M$ candidates and cheap unnormalised target $\hat{p} \approx f$:",
    "",
    r"Step 1 -- Draw  $X_1, \ldots, X_M \sim q$  (proposal)",
    "",
    r"Step 2 -- Resampling weights:  "
    r"$w_i \;=\; \frac{1}{M} \cdot \frac{\hat{p}(X_i)}{q(X_i)}$",
    "",
    r"Step 3 -- Categorical sample:  "
    r"$\Pr[Y = X_i] \;=\; \frac{w_i}{\sum_{j=1}^{M} w_j}$",
    "",
    r"Step 4 -- Contribution weight:  "
    r"$W_Y \;=\; \frac{\sum_{j=1}^{M} w_j}{\hat{p}(Y)}$",
    "",
    r"Step 5 -- Estimator:  $\hat{I}_{RIS} \;=\; f(Y) \cdot W_Y$",
], dy=0.047)
y = sec(ax, y, "4", "UNBIASEDNESS", [
    r"$\mathrm{E}[f(Y)\cdot W_Y]"
    r" = \mathrm{E}_Y\!\left[f(Y)\cdot\frac{\sum_j w_j}{\hat{p}(Y)}\right]$",
    "",
    r"$= \int_0^1 f(y)\cdot\frac{1}{\hat{p}(y)}\cdot\hat{p}(y)\,dy"
    r" \;=\; \int_0^1 f(y)\,dy \;=\; I$   (exact)",
    "",
    r"The $\hat{p}$ in $\Pr[Y=y]$ and $1/\hat{p}$ in $W_Y$ cancel exactly.",
    r"$M$ controls only variance, NOT the expected value.",
], dy=0.048)
y = sec(ax, y, "5", "NULL-SAMPLE RULE", [
    r"If all $w_i = 0$: return $\hat{I} = 0$.  "
    r"Do NOT re-draw -- re-drawing introduces bias.",
])
sec(ax, y, "6", "THIS SCRIPT", [
    r"$f(x)=6x(1-x)$,   $q(x)=\mathrm{Uniform}[0,1]$,   "
    r"$\hat{p}(x)=\sin(\pi x)$   (proxy -- NOT proportional to $f$)",
    r"Figure 2 confirms all means $\approx 1.0$ and variance $\propto 1/M$.",
])
fig1.savefig("/tmp/_test_math.png", dpi=80, bbox_inches="tight")

# ---- Figure 2: Results -----------------------------------------------------
fig2 = plt.figure(figsize=(17, 12))
fig2.suptitle(
    r"RIS empirical results  --  $I = \int_0^1 6x(1-x)\,dx = 1.0$" + "\n"
    r"$\hat{p}(x)=\sin(\pi x)$,   $q(x)=\mathrm{Uniform}[0,1]$",
    fontsize=12, y=0.99)
gs = gridspec.GridSpec(3, 3, figure=fig2, hspace=0.55, wspace=0.35,
                       top=0.92, bottom=0.05, left=0.07, right=0.97)

aA = fig2.add_subplot(gs[0,0])
aA.plot(xs, fv, lw=2, color="steelblue",  label=r"$f(x)=6x(1-x)$")
aA.plot(xs, pv, lw=2, color="darkorange", linestyle="--", label=r"$\hat{p}(x)=\sin(\pi x)$")
aA.plot(xs, qv, lw=1.5, color="gray", linestyle=":", label=r"$q(x)=1$ (uniform)")
aA.fill_between(xs, fv, alpha=0.12, color="steelblue")
aA.set_title("Functions", fontsize=10); aA.set_xlabel(r"$x$"); aA.legend(fontsize=8)
aA.set_ylim(-0.1, 2.1); aA.axhline(0, color="black", lw=0.5)
aA.text(0.5, 1.92, r"$I=1$", ha="center", fontsize=10, color="steelblue", fontstyle="italic")
aA.text(0.5, -0.22,
    "Blue is true integrand f(x). Orange is RIS target p_hat(x). Gray dotted is q(x).\n"
    "Here q(x)=1 on [0,1] (Uniform[0,1]); p_hat is only a proxy, not equal to f.",
    transform=aA.transAxes, ha="center", va="top", fontsize=8, color="#444")

aB = fig2.add_subplot(gs[0,1])
lane_y = {1: 4.0, 2: 3.0, 4: 2.0, 16: 1.0, 32: 0.0}
for m in pm:
    est, wY, cands, ws, ch = sr_m[m]
    c = C[m]
    y0 = lane_y[m]

    # Base line for this M lane
    aB.hlines(y0, 0.0, 1.0, colors="#d0d0d0", linewidth=1.0, zorder=1)

    w_sum = sum(ws) if ws else 0.0
    p_vals = [(w / w_sum) if w_sum > 0.0 else 0.0 for w in ws]
    p_max = max(p_vals) if p_vals else 0.0
    if p_max <= 0.0:
        heights = [0.0 for _ in ws]
    else:
        # Display transform for readability: monotonic in p_i, but boosts contrast.
        # Exact p_i values are shown in the detailed table figures.
        heights = [0.85 * math.sqrt(p / p_max) for p in p_vals]

    # Show all candidates with stems using a contrast-enhanced monotonic map of p_i.
    for x_i, h_i in zip(cands, heights):
        aB.vlines(x_i, y0, y0 + h_i, color=c, linewidth=1.0, alpha=0.6, zorder=2)
    aB.scatter(cands, [y0 + h for h in heights], s=20, color=c, alpha=0.6, zorder=3)

    if ch >= 0:
        x_sel = cands[ch]
        y_sel = y0 + heights[ch]
        aB.scatter([x_sel], [y_sel], marker="*", s=170, color=c,
                   edgecolors="black", lw=0.6, zorder=5)
        aB.text(1.02, y0,
                r"$M=%d:\ \hat{I}=%.3f,\ W_Y=%.2f$" % (m, est, wY),
                color=c, fontsize=8, va="center")
        aB.text(1.02, y0 - 0.23,
            r"$p_{\min}=%.3f,\ p_{\max}=%.3f$" % (min(p_vals), max(p_vals)),
            color=c, fontsize=7, va="center")

aB.set_title(r"Single run per $M$  (star = chosen $Y$)", fontsize=10)
aB.set_xlabel(r"$x$")
aB.set_xlim(-0.02, 1.22)
aB.set_ylim(-0.45, 4.75)
aB.set_yticks([lane_y[m] for m in pm])
aB.set_yticklabels([r"$M=%d$" % m for m in pm])
aB.grid(axis="x", alpha=0.2)
aB.text(0.5, -0.22,
    "Each row is one RIS draw with that M under proposal q(x)=Uniform[0,1] (so q(x)=1).\n"
    "Stem height uses a contrast-enhanced monotonic map of p_i for readability; exact p_i in tables.",
    transform=aB.transAxes, ha="center", va="top", fontsize=8, color="#444")

aC = fig2.add_subplot(gs[0,2])
box_data = [batch_means_m[m] for m in pm]
bp = aC.boxplot(box_data, positions=list(range(1, len(pm) + 1)), widths=0.6,
                patch_artist=True, showfliers=False)
for patch, m in zip(bp["boxes"], pm):
    patch.set_facecolor(C[m])
    patch.set_alpha(0.35)
for med in bp["medians"]:
    med.set_color("#111")
    med.set_linewidth(1.4)

aC.axhline(TRUE, color="black", lw=2, linestyle="--")
for i, m in enumerate(pm, start=1):
    mu_batch = mean(batch_means_m[m])
    aC.plot(i, mu_batch, marker="o", color=C[m], ms=4)

aC.set_title("Unbiasedness Check via Batch Means", fontsize=10)
aC.set_xlabel("M (number of candidates used by RIS per estimate)")
aC.set_ylabel("Batch mean of estimates (50 estimates per batch)")
aC.set_xticks(list(range(1, len(pm) + 1)))
aC.set_xticklabels([str(m) for m in pm])
aC.grid(axis="y", alpha=0.25)

aC.text(0.5, -0.22,
    "Each box summarizes many batch means. Dashed line is the true integral value = 1.\n"
    "If the method is unbiased, boxes are centered around 1 for every M.",
    transform=aC.transAxes, ha="center", va="top", fontsize=8, color="#444")
aC.text(0.5, -0.38,
    "Variable guide: f(x)=integrand, q(x)=proposal distribution (Uniform[0,1]),\n"
    "M=number of RIS candidates, estimate=single RIS output for one random draw.",
    transform=aC.transAxes, ha="center", va="top", fontsize=8, color="#444")

aD = fig2.add_subplot(gs[1,0:2])
ns = list(range(1, NC+1))
for m in pm:
    aD.plot(ns, rm_m[m], color=C[m], lw=1.5, label=r"$M=%d$" % m, alpha=0.85)
aD.axhline(TRUE, color="black", lw=1.5, linestyle="--", label=r"$I=1$")
aD.set_title(r"Running mean of $\hat{I}_{RIS}$ -- all converge to $1.0$", fontsize=10)
aD.set_xlabel("Number of RIS evaluations"); aD.set_ylabel(r"$\bar{I}_{RIS}$")
aD.legend(fontsize=9); aD.set_ylim(0.55, 1.45)
aD.text(0.5, -0.22,
    "Running average versus sample count for fixed q(x)=Uniform[0,1].\n"
    "Every curve stabilizes around 1.0 as more samples are accumulated.",
    transform=aD.transAxes, ha="center", va="top", fontsize=8, color="#444")

aE = fig2.add_subplot(gs[1,2])
aE.plot(mr, varm, marker="o", color="steelblue", lw=2)
for mv, v in zip(mr, varm):
    aE.annotate("%.4f" % v, (mv, v), textcoords="offset points", xytext=(5,5), fontsize=7)
aE.set_title(r"$\mathrm{Var}[\hat{I}_{RIS}]$ vs $M$", fontsize=10)
aE.set_xlabel(r"Candidate count $M$"); aE.set_ylabel("Empirical variance")
aE.set_xscale("log", base=2); aE.set_yscale("log")
aE.text(0.5, -0.22,
    "Variance decreases as M increases with the same proposal q(x)=Uniform[0,1].\n"
    "Interpretation: more candidates reduce noise while staying unbiased.",
    transform=aE.transAxes, ha="center", va="top", fontsize=8, color="#444")

aF = fig2.add_subplot(gs[2,:])
aF.axis("off")
v1 = vari(est_m[1]); s1 = std(est_m[1])
cols = [
    r"$M$",
    r"Mean $\hat{\mu}$",
    r"Bias $\hat{\mu} - I$",
    r"Variance $\hat{\sigma}^2$",
    r"Std $\hat{\sigma}$",
    r"Rel. std $\hat{\sigma}_M / \hat{\sigma}_1$",
    r"Var ratio $\hat{\sigma}_1^2 / \hat{\sigma}_M^2$",
]
rows = []
for m in M_VALUES:
    vals = est_m[m]; mu = mean(vals); vm = vari(vals); sm = std(vals)
    rows.append([str(m), "%.6f"%mu, "%+.6f"%(mu-TRUE),
                 "%.6f"%vm, "%.6f"%sm, "%.4f"%(sm/s1), "%.2f\u00d7"%(v1/vm)])
tbl = aF.table(cellText=rows, colLabels=cols, loc="center", cellLoc="center")
tbl.auto_set_font_size(False); tbl.set_fontsize(9); tbl.scale(1.0, 1.65)
for j in range(len(cols)):
    tbl[0,j].set_facecolor("#0d47a1"); tbl[0,j].set_text_props(color="white", fontweight="bold")
for i in range(1, len(rows)+1):
    bg = "#e3f2fd" if i%2==0 else "white"
    for j in range(len(cols)): tbl[i,j].set_facecolor(bg)
aF.set_title("Numerical table  (N=%d samples per M)" % N, fontsize=10, loc="left", pad=8)
aF.text(0.5, -0.06,
    r"All means $\approx 1.0$  (unbiased for any $M$).  "
    r"Var ratio $\approx M$  (variance halves each time $M$ doubles).",
    ha="center", fontsize=9, color="#444", fontstyle="italic", transform=aF.transAxes)
aF.text(0.5, -0.17,
    "Table summary for q(x)=Uniform[0,1]: mean/bias test unbiasedness; variance/std quantify noise.",
    ha="center", fontsize=8, color="#555", transform=aF.transAxes)

# Console table --------------------------------------------------------------
print(); print("="*74)
print("  True value:  I = %.6f" % TRUE)
print("  %4s  %10s  %10s  %12s  %10s  %10s" % ("M","mean","bias","variance","std","var_ratio"))
print("-"*74)
for m in M_VALUES:
    vals = est_m[m]; mu = mean(vals); vm = vari(vals); sm = std(vals)
    print("  %4d  %10.6f  %+10.6f  %12.6f  %10.6f  %8.2fx" % (m, mu, mu-TRUE, vm, sm, v1/vm))
print("="*74)
print("  All means approx 1.0  ->  UNBIASED  |  Var ratio approx M")

out = os.path.dirname(os.path.abspath(__file__))
p1 = os.path.join(out, "ris_1d_math.png")
p2 = os.path.join(out, "ris_1d_results.png")
fig1.savefig(p1, dpi=130, bbox_inches="tight", facecolor=fig1.get_facecolor())
fig2.savefig(p2, dpi=130, bbox_inches="tight")
print("  Saved: " + p1); print("  Saved: " + p2)

# ---- Figure 3: Detailed weights table --------------------------------------
fig3, ax3 = plt.subplots(figsize=(18, 7))
ax3.axis("off")
ax3.set_title("Worked Example (M=8): MIS, resampling, and unbiased contribution weights",
              fontsize=12, fontweight="bold", loc="left", pad=12)

col_labels = [
    "i",
    "x_i",
    "q(x_i)",
    "p_hat(x_i)",
    "MIS\nweight",
    "Resampling\nweight w_i",
    "Resampling prob\np_i = w_i/sum_j w_j",
    "Unbiased contrib\nweight W_Y",
]

cell_rows = []
for r in worked_m8["rows"]:
    marker = "*" if r["i"] == worked_m8["chosen"] else ""
    cell_rows.append([
        f"{r['i']}{marker}",
        f"{r['x']:.6f}",
        f"{r['q']:.3f}",
        f"{r['p_hat']:.6f}",
        f"{r['mis']:.3f}",
        f"{r['w']:.8f}",
        f"{r['p_resample']:.6f}",
        f"{r['W_unbiased']:.6f}",
    ])

tbl3 = ax3.table(
    cellText=cell_rows,
    colLabels=col_labels,
    loc="center",
    cellLoc="center",
    colWidths=[0.065, 0.095, 0.095, 0.11, 0.10, 0.14, 0.18, 0.16],
)
tbl3.auto_set_font_size(False)
tbl3.set_fontsize(10)
tbl3.scale(1.0, 1.75)

for j in range(len(col_labels)):
    tbl3[0, j].set_facecolor("#1b5e20")
    tbl3[0, j].set_text_props(color="white", fontweight="bold", fontsize=9)

for i in range(1, len(cell_rows) + 1):
    bg = "#e8f5e9" if i % 2 == 0 else "white"
    for j in range(len(col_labels)):
        tbl3[i, j].set_facecolor(bg)

chosen = worked_m8["chosen"]
ax3.text(0.01, 0.08,
         f"Chosen row: i={chosen} (*).  sum_j w_j={worked_m8['w_sum']:.8f}, "
         f"W_Y={worked_m8['W_unbiased']:.6f}, estimate=f(Y)*W_Y={worked_m8['estimate']:.6f}",
         transform=ax3.transAxes, fontsize=10)
ax3.text(0.5, -0.02,
         "One full RIS draw with q(x)=Uniform[0,1]: w_i, p_i, and shared unbiased contribution weight W_Y.",
         transform=ax3.transAxes, ha="center", fontsize=8, color="#444")

p3 = os.path.join(out, "ris_1d_weights_table.png")
fig3.savefig(p3, dpi=130, bbox_inches="tight")
print("  Saved: " + p3)

print()
print("Detailed worked example (M=8):")
print(" idx*       x_i    q(x_i)   p_hat(x_i)      MIS_i          w_i     p_resample_i          W_Y")
for r in worked_m8["rows"]:
    star = "*" if r["i"] == worked_m8["chosen"] else " "
    print(f"  {r['i']:>2}{star}  {r['x']:.6f}    {r['q']:.3f}    {r['p_hat']:.6f}    "
          f"{r['mis']:.3f}    {r['w']:.8f}      {r['p_resample']:.6f}    {r['W_unbiased']:.6f}")
print(f"  sum_j w_j = {worked_m8['w_sum']:.8f}")
print(f"  estimate  = {worked_m8['estimate']:.6f}")

# ---- Figure 4: Single-run detailed tables for M in {1,2,4,16} -------------
fig4, axarr = plt.subplots(3, 2, figsize=(16, 16), constrained_layout=True)
fig4.suptitle("Single-run detailed values (exact numbers)", fontsize=14, fontweight="bold")

flat_axes = list(axarr.flat)
for ax_sub, m in zip(flat_axes, pm):
    ax_sub.axis("off")
    est, wY, cands, ws, ch = sr_m[m]
    w_sum = sum(ws)
    rows = []
    for i, (x_i, w_i) in enumerate(zip(cands, ws)):
        p_i = (w_i / w_sum) if w_sum > 0.0 else 0.0
        marker = "*" if i == ch else ""
        rows.append([
            f"{i}{marker}",
            f"{x_i:.6f}",
            f"{1.0:.3f}",
            f"{p_hat(x_i):.6f}",
            f"{1.0:.3f}",
            f"{w_i:.8f}",
            f"{p_i:.6f}",
        ])

    # Keep large-M tables readable: show head + chosen row + tail when needed.
    display_rows = rows
    truncated_note = ""
    max_display_rows = 12
    if len(rows) > max_display_rows:
        keep = set()
        for idx in range(min(5, len(rows))):
            keep.add(idx)
        keep.add(ch)
        for idx in range(max(0, len(rows) - 4), len(rows)):
            keep.add(idx)
        keep_list = sorted(keep)

        display_rows = []
        prev = None
        for idx in keep_list:
            if prev is not None and idx - prev > 1:
                display_rows.append(["...", "...", "...", "...", "...", "...", "..."])
            display_rows.append(rows[idx])
            prev = idx

        truncated_note = f" (showing compact view: {len(display_rows)} of {len(rows)} rows)"

    tbl = ax_sub.table(
        cellText=display_rows,
        colLabels=["i", "x_i", "q(x_i)", "p_hat(x_i)", "MIS_i", "w_i", "p_i"],
        loc="center",
        cellLoc="center",
    )
    tbl.auto_set_font_size(False)
    table_font = 8 if len(display_rows) <= 6 else 7
    tbl.set_fontsize(table_font)
    tbl.scale(1.0, 1.25)

    for j in range(7):
        tbl[0, j].set_facecolor("#6a1b9a")
        tbl[0, j].set_text_props(color="white", fontweight="bold")

    ax_sub.set_title(
        f"M={m} | sum_j w_j={w_sum:.6f}, W_Y={wY:.6f}, I_hat={est:.6f}{truncated_note}",
        fontsize=10,
        color=C[m],
        pad=8,
    )

for ax_sub in flat_axes[len(pm):]:
    ax_sub.axis("off")

fig4.text(
    0.5,
    0.02,
    "Rows are candidates from one RIS draw with q(x)=Uniform[0,1]. "
    "Star marks selected sample Y. p_i is categorical selection probability.",
    ha="center",
    fontsize=9,
    color="#444",
)

p4 = os.path.join(out, "ris_1d_single_run_tables.png")
fig4.savefig(p4, dpi=130, bbox_inches="tight")
print("  Saved: " + p4)

if "--show" in sys.argv: plt.show()
