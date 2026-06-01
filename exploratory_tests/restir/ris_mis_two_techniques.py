import math
import os
import random
import sys
import time
import json

try:
    import matplotlib
    matplotlib.use("TkAgg" if "--show" in sys.argv else "Agg")
    import matplotlib.pyplot as plt
    import matplotlib.gridspec as gridspec
except ImportError as exc:
    print("pip install matplotlib")
    print("Import error detail:", exc)
    sys.exit(1)

SEED = 123
random.seed(SEED)
SCRIPT_T0 = time.perf_counter()
LOG_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "ris_mis_two_techniques_progress.log",
)
STDOUT_LOGGING_ENABLED = True
FAST_MODE = "--fast" in sys.argv
STABLE_MODE = "--stable" in sys.argv

if FAST_MODE and STABLE_MODE:
    # Favor stable mode if both switches are accidentally passed.
    FAST_MODE = False

# Keeps proposal PDFs safely away from zero near the boundaries x~0 and x~1,
# reducing huge importance weights from tiny denominators.
UNIFORM_PROPOSAL_FLOOR = 0.08
INTEGRAND_SCALE = 100.0
PHAT_EPS = 1.0e-3


def safe_ratio(num, den, eps=1.0e-12):
    if den <= eps:
        return 0.0
    return num / den

with open(LOG_PATH, "w", encoding="utf-8") as flog:
    flog.write("RIS+MIS progress log\n")


def log_progress(message):
    global STDOUT_LOGGING_ENABLED
    elapsed = time.perf_counter() - SCRIPT_T0
    line = "[{:.1f}s] {}".format(elapsed, message)
    if STDOUT_LOGGING_ENABLED:
        try:
            print(line, flush=True)
        except BrokenPipeError:
            STDOUT_LOGGING_ENABLED = False
    with open(LOG_PATH, "a", encoding="utf-8") as flog:
        flog.write(line + "\n")

# Integrand and RIS target ----------------------------------------------------


def beta_pdf(x, a, b):
    if x <= 0.0 or x >= 1.0:
        return 0.0
    norm = math.gamma(a + b) / (math.gamma(a) * math.gamma(b))
    return norm * (x ** (a - 1.0)) * ((1.0 - x) ** (b - 1.0))


def _f_base(x):
    return 0.55 * beta_pdf(x, 3.0, 9.0) + 0.45 * beta_pdf(x, 9.0, 3.0)


def f(x):
    # Scaled two-lobe integrand to keep values/errors in a numerically comfortable range.
    return INTEGRAND_SCALE * _f_base(x)


def p_hat(x):
    # Matched target for RIS validation: p_hat ~= f (strictly positive offset).
    return f(x) + PHAT_EPS


def p_hat_bad(x):
    # Deliberately imperfect but still two-lobe proxy.
    # Keeps support and shape family similar to f, avoiding pathological variance spikes.
    core = 0.80 * beta_pdf(x, 4.0, 10.0) + 0.20 * beta_pdf(x, 10.0, 4.0)
    return INTEGRAND_SCALE * core + PHAT_EPS


def p_hat_quality(alpha, x):
    # alpha=0 -> poor proxy, alpha=1 -> near-perfect proxy.
    return (1.0 - alpha) * p_hat_bad(x) + alpha * p_hat(x)


def _burn_cycles(x, loops=2000):
    # Synthetic compute load to emulate an expensive integrand evaluation.
    acc = 0.0
    for i in range(loops):
        t = (i + 1) * 1.0e-5
        acc += math.sin(x + t) * math.cos(x - t)
    return acc


def f_costly(x):
    _burn_cycles(x)
    return f(x)


# Technique 1: Broad left-lobe proposal --------------------------------------

def q1_pdf_quality(mis_quality, x):
    # mis_quality=0: weak proposal (near-uniform). mis_quality=1: better left-lobe proposal.
    core = (1.0 - mis_quality) * beta_pdf(x, 2.0, 2.0) + mis_quality * beta_pdf(x, 2.0, 6.0)
    return (1.0 - UNIFORM_PROPOSAL_FLOOR) * core + UNIFORM_PROPOSAL_FLOOR


def q1_sample_quality(mis_quality):
    if random.random() < UNIFORM_PROPOSAL_FLOOR:
        return random.random()
    if random.random() < mis_quality:
        return random.betavariate(2.0, 6.0)
    return random.betavariate(2.0, 2.0)


# Technique 2: Broad right-lobe proposal -------------------------------------

def q2_pdf_quality(mis_quality, x):
    # mis_quality=0: weak proposal (near-uniform). mis_quality=1: better right-lobe proposal.
    core = (1.0 - mis_quality) * beta_pdf(x, 2.0, 2.0) + mis_quality * beta_pdf(x, 6.0, 2.0)
    return (1.0 - UNIFORM_PROPOSAL_FLOOR) * core + UNIFORM_PROPOSAL_FLOOR


def q2_sample_quality(mis_quality):
    if random.random() < UNIFORM_PROPOSAL_FLOOR:
        return random.random()
    if random.random() < mis_quality:
        return random.betavariate(6.0, 2.0)
    return random.betavariate(2.0, 2.0)


def q1_pdf(x):
    return q1_pdf_quality(1.0, x)


def q1_sample():
    return q1_sample_quality(1.0)


def q2_pdf(x):
    return q2_pdf_quality(1.0, x)


def q2_sample():
    return q2_sample_quality(1.0)


# Mixture proposal (MIS across two techniques) --------------------------------
C1 = 0.5
C2 = 0.5


def q_mix_pdf(x, mis_quality=1.0):
    return C1 * q1_pdf_quality(mis_quality, x) + C2 * q2_pdf_quality(mis_quality, x)


def sample_from_mixture(mis_quality=1.0):
    if random.random() < C1:
        x = q1_sample_quality(mis_quality)
        tech = 1
    else:
        x = q2_sample_quality(mis_quality)
        tech = 2
    return x, tech


def posterior_mis_weight(tech, x, mis_quality=1.0):
    # Technique posterior under the mixture; sums to 1 over techniques.
    qx = q_mix_pdf(x, mis_quality=mis_quality)
    if tech == 1:
        return safe_ratio(C1 * q1_pdf_quality(mis_quality, x), qx)
    return safe_ratio(C2 * q2_pdf_quality(mis_quality, x), qx)


# Estimators ------------------------------------------------------------------

def mean(values):
    return math.fsum(values) / len(values) if values else 0.0


def variance(values):
    if len(values) < 2:
        return 0.0
    # Welford's online algorithm is numerically stable and avoids catastrophic cancellation.
    mu = 0.0
    m2 = 0.0
    for k, x in enumerate(values, start=1):
        delta = x - mu
        mu += delta / k
        m2 += delta * (x - mu)
    return m2 / (len(values) - 1)


def std(values):
    return math.sqrt(variance(values))


def ci95(values):
    if not values:
        return 0.0
    return 1.96 * std(values) / math.sqrt(len(values))


def running_mean(values):
    out = []
    acc = 0.0
    for i, v in enumerate(values, 1):
        acc += v
        out.append(acc / i)
    return out


def benchmark_method(estimator, m, iters=4000, repeats=4, progress_label=None):
    # Average time per estimator call in microseconds.
    samples = []
    for rep in range(repeats):
        if progress_label is not None:
            log_progress("{}: repeat {}/{} start (iters={})".format(
                progress_label, rep + 1, repeats, iters))
        t0 = time.perf_counter()
        for _ in range(iters):
            estimator(m)
        dt = time.perf_counter() - t0
        samples.append(dt / iters)
        if progress_label is not None:
            log_progress("{}: repeat {}/{} done".format(
                progress_label, rep + 1, repeats))
    return mean(samples) * 1.0e6


def sweep_sample_count(estimator, m, n_values, reps=80, progress_label=None):
    # For each N, compute many sample-means to visualize convergence vs sample count.
    out = {}
    for n_idx, n in enumerate(n_values, start=1):
        if progress_label is not None:
            log_progress("{}: checkpoint N={} ({}/{}) start".format(
                progress_label, n, n_idx, len(n_values)))
        means = []
        rep_step = max(1, reps // 4)
        for rep in range(reps):
            acc = 0.0
            for _ in range(n):
                val = estimator(m)
                acc += val[0] if isinstance(val, tuple) else val
            means.append(acc / n)
            if progress_label is not None and ((rep + 1) % rep_step == 0 or rep + 1 == reps):
                log_progress("{}: checkpoint N={} reps {}/{}".format(
                    progress_label, n, rep + 1, reps))
        out[n] = means
    return out


def sweep_sample_count_prefix(estimator, m, n_values, reps=20, progress_label=None):
    # Efficient convergence sweep: for each repetition, run once up to max(N)
    # and record running means at requested checkpoints.
    checkpoints = sorted(n_values)
    max_n = checkpoints[-1]
    out = {n: [] for n in checkpoints}

    rep_step = max(1, reps // 4)
    for rep in range(reps):
        acc = 0.0
        ck_idx = 0
        next_ck = checkpoints[ck_idx]
        for i in range(1, max_n + 1):
            val = estimator(m)
            acc += val[0] if isinstance(val, tuple) else val
            if i == next_ck:
                out[next_ck].append(acc / i)
                ck_idx += 1
                if ck_idx >= len(checkpoints):
                    break
                next_ck = checkpoints[ck_idx]
        if progress_label is not None and ((rep + 1) % rep_step == 0 or rep + 1 == reps):
            log_progress("{}: reps {}/{} through max N={}".format(
                progress_label, rep + 1, reps, max_n))

    return out


def find_convergence_end_index(mis_err_line, ris_err_line, tol=5.0e-3, stable_window=2):
    """
    Find first index where BOTH methods are below tolerance for a stable window.
    Returns the index to use as endpoint; falls back to last index if not found.
    """
    n = len(mis_err_line)
    if n == 0:
        return 0
    for i in range(n):
        j = min(n, i + stable_window)
        ok = True
        for k in range(i, j):
            if mis_err_line[k] > tol or ris_err_line[k] > tol:
                ok = False
                break
        if ok:
            return j - 1
    return n - 1


def mixture_is_estimate(m, f_eval=f, mis_quality=1.0):
    # Average m i.i.d. samples from q_mix.
    acc = 0.0
    for _ in range(m):
        x, _ = sample_from_mixture(mis_quality=mis_quality)
        qx = q_mix_pdf(x, mis_quality=mis_quality)
        acc += safe_ratio(f_eval(x), qx)
    return acc / m


def two_tech_mis_estimate(m, f_eval=f, mis_quality=1.0):
    # Deterministic split: n1 from q1 and n2 from q2 with balance-heuristic MIS.
    if m == 1:
        # One-sample MIS fallback (random technique under mixture probabilities).
        x, _ = sample_from_mixture(mis_quality=mis_quality)
        qx = q_mix_pdf(x, mis_quality=mis_quality)
        return safe_ratio(f_eval(x), qx)

    n1 = m // 2
    n2 = m - n1
    total = 0.0

    if n1 > 0:
        for _ in range(n1):
            x = q1_sample_quality(mis_quality)
            p1 = q1_pdf_quality(mis_quality, x)
            denom = n1 * q1_pdf_quality(mis_quality, x) + n2 * q2_pdf_quality(mis_quality, x)
            w1 = safe_ratio(n1 * p1, denom)
            total += safe_ratio(w1 * f_eval(x), p1 * n1)

    if n2 > 0:
        for _ in range(n2):
            x = q2_sample_quality(mis_quality)
            p2 = q2_pdf_quality(mis_quality, x)
            denom = n1 * q1_pdf_quality(mis_quality, x) + n2 * q2_pdf_quality(mis_quality, x)
            w2 = safe_ratio(n2 * p2, denom)
            total += safe_ratio(w2 * f_eval(x), p2 * n2)

    return total


def ris_on_mixture_estimate(m, f_eval=f, p_hat_eval=p_hat, mis_quality=1.0):
    # Algorithm 1 style RIS where source density is q_mix.
    cands = []
    weights = []
    for _ in range(m):
        x, tech = sample_from_mixture(mis_quality=mis_quality)
        qx = q_mix_pdf(x, mis_quality=mis_quality)
        ph = p_hat_eval(x)
        w = (1.0 / m) * safe_ratio(ph, qx)
        cands.append((x, tech))
        weights.append(w)

    w_sum = sum(weights)
    if w_sum <= 0.0:
        return 0.0, 0.0, cands, weights, -1

    u = random.random() * w_sum
    chosen = 0
    acc = 0.0
    for i, w in enumerate(weights):
        acc += w
        if u <= acc:
            chosen = i
            break

    y = cands[chosen][0]
    ph_y = p_hat_eval(y)
    w_y = safe_ratio(w_sum, ph_y)
    return f_eval(y) * w_y, w_y, cands, weights, chosen


def ris_mis_generalized_estimate(m, f_eval=f, p_hat_eval=p_hat, mis_quality=1.0):
    # Generalized RIS+MIS: non-i.i.d candidates from q1/q2 with per-candidate m_i(x).
    if m == 1:
        # For one candidate, use the i.i.d mixture RIS form.
        return ris_on_mixture_estimate(1, f_eval=f_eval, p_hat_eval=p_hat_eval, mis_quality=mis_quality)

    n1 = m // 2
    n2 = m - n1
    cands = []
    weights = []

    if n1 > 0:
        for _ in range(n1):
            x = q1_sample_quality(mis_quality)
            p_src = q1_pdf_quality(mis_quality, x)
            denom = n1 * q1_pdf_quality(mis_quality, x) + n2 * q2_pdf_quality(mis_quality, x)
            m_i = safe_ratio(q1_pdf_quality(mis_quality, x), denom)
            w = safe_ratio(m_i * p_hat_eval(x), p_src)
            cands.append((x, 1, p_src, m_i))
            weights.append(w)

    if n2 > 0:
        for _ in range(n2):
            x = q2_sample_quality(mis_quality)
            p_src = q2_pdf_quality(mis_quality, x)
            denom = n1 * q1_pdf_quality(mis_quality, x) + n2 * q2_pdf_quality(mis_quality, x)
            m_i = safe_ratio(q2_pdf_quality(mis_quality, x), denom)
            w = safe_ratio(m_i * p_hat_eval(x), p_src)
            cands.append((x, 2, p_src, m_i))
            weights.append(w)

    w_sum = sum(weights)
    if w_sum <= 0.0:
        return 0.0, 0.0, cands, weights, -1

    u = random.random() * w_sum
    chosen = 0
    acc = 0.0
    for i, w in enumerate(weights):
        acc += w
        if u <= acc:
            chosen = i
            break

    y = cands[chosen][0]
    ph_y = p_hat_eval(y)
    w_y = safe_ratio(w_sum, ph_y)
    return f_eval(y) * w_y, w_y, cands, weights, chosen


# Experiment setup ------------------------------------------------------------
TRUE = INTEGRAND_SCALE
# Keep this list sparse and monotone for readability, and include high M.
if FAST_MODE:
    # Fast mode keeps endpoints (up to 800) but uses fewer M values to stay responsive.
    M_VALUES = [1, 4, 16, 64, 256, 800]
else:
    M_VALUES = [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 800]

# Timing benchmarks are expensive; use a conservative subset.
if FAST_MODE:
    M_TIMING_VALUES = [1, 2, 4, 8, 16, 32]
else:
    M_TIMING_VALUES = [1, 2, 4, 8, 16, 32, 64, 128]
if FAST_MODE:
    # Fast mode remains responsive but uses enough trials to reduce random trend inversions.
    N = 2200
    TIMING_ITERS = 350
    TIMING_REPEATS = 2
    COSTLY_ITERS = 100
    COSTLY_REPEATS = 2
elif STABLE_MODE:
    N = 6000
    TIMING_ITERS = 900
    TIMING_REPEATS = 4
    COSTLY_ITERS = 300
    COSTLY_REPEATS = 3
else:
    N = 5000
    TIMING_ITERS = 700
    TIMING_REPEATS = 4
    COSTLY_ITERS = 260
    COSTLY_REPEATS = 3

log_progress("Experiment setup: building sample tables (N={})".format(N))
mix_vals = {}
mis_vals = {}
ris_iid_vals = {}
ris_gen_vals = {}
for idx, m in enumerate(M_VALUES, start=1):
    log_progress("Experiment setup: M={} ({}/{}) sample tables start".format(m, idx, len(M_VALUES)))
    mix_vals[m] = [mixture_is_estimate(m) for _ in range(N)]
    log_progress("Experiment setup: M={} mix_vals done".format(m))
    mis_vals[m] = [two_tech_mis_estimate(m) for _ in range(N)]
    log_progress("Experiment setup: M={} mis_vals done".format(m))
    ris_iid_vals[m] = [ris_on_mixture_estimate(m)[0] for _ in range(N)]
    log_progress("Experiment setup: M={} ris_iid_vals done".format(m))
    ris_gen_vals[m] = [ris_mis_generalized_estimate(m)[0] for _ in range(N)]
    log_progress("Experiment setup: M={} ris_gen_vals done".format(m))

single_m = 16
log_progress("Experiment setup: single generalized RIS sample (M={})".format(single_m))
single_ris = ris_mis_generalized_estimate(single_m)

log_progress("Experiment setup: timing mixture IS")
timing_mix_us = [benchmark_method(mixture_is_estimate, m, iters=TIMING_ITERS, repeats=TIMING_REPEATS, progress_label="timing mix M={}".format(m)) for m in M_TIMING_VALUES]
log_progress("Experiment setup: timing MIS")
timing_mis_us = [benchmark_method(two_tech_mis_estimate, m, iters=TIMING_ITERS, repeats=TIMING_REPEATS, progress_label="timing MIS M={}".format(m)) for m in M_TIMING_VALUES]
log_progress("Experiment setup: timing generalized RIS")
timing_ris_gen_us = [benchmark_method(lambda mm: ris_mis_generalized_estimate(mm)[0], m, iters=TIMING_ITERS, repeats=TIMING_REPEATS, progress_label="timing RIS M={}".format(m)) for m in M_TIMING_VALUES]

# Cost-model benchmark: expensive f, cheap p_hat.
log_progress("Experiment setup: timing costly MIS")
timing_mis_costly_us = [
    benchmark_method(lambda mm: two_tech_mis_estimate(mm, f_eval=f_costly), m, iters=COSTLY_ITERS, repeats=COSTLY_REPEATS,
                     progress_label="timing costly MIS M={}".format(m))
    for m in M_TIMING_VALUES
]
log_progress("Experiment setup: timing costly RIS")
timing_ris_gen_costly_us = [
    benchmark_method(lambda mm: ris_mis_generalized_estimate(mm, f_eval=f_costly)[0], m, iters=COSTLY_ITERS, repeats=COSTLY_REPEATS,
                     progress_label="timing costly RIS M={}".format(m))
    for m in M_TIMING_VALUES
]


# Console summary -------------------------------------------------------------

def print_summary(name, values_by_m):
    print(name)
    print("{:>6} {:>10} {:>10} {:>12} {:>10} {:>10}".format(
        "M", "mean", "bias", "variance", "std", "95% CI"
    ))
    for m in M_VALUES:
        vals = values_by_m[m]
        mu = mean(vals)
        va = variance(vals)
        ci = ci95(vals)
        print("{:>6d} {:>10.6f} {:>+10.6f} {:>12.6f} {:>10.6f} {:>10.6f}".format(
            m, mu, mu - TRUE, va, math.sqrt(va), ci
        ))
    print()


print("==============================================================")
print("Two-technique MIS + RIS exploratory test")
print("f(x)=100*(0.55*Beta(3,9)+0.45*Beta(9,3)), true integral={:.1f}".format(TRUE))
print("q1/q2 use lobe-aware beta cores with a uniform floor (eps={:.2f}) for stability".format(UNIFORM_PROPOSAL_FLOOR))
print("q_mix=0.5*q1 + 0.5*q2")
grid = [i / 500.0 for i in range(1, 500)]
min_q1 = min(q1_pdf(x) for x in grid)
min_q2 = min(q2_pdf(x) for x in grid)
min_qmix = min(q_mix_pdf(x) for x in grid)
print("support check on (0,1): min q1={:.3e}, min q2={:.3e}, min q_mix={:.3e}".format(
    min_q1, min_q2, min_qmix
))
print("==============================================================")
print_summary("Mixture IS (average of M i.i.d samples from q_mix)", mix_vals)
print_summary("Two-technique MIS (balance heuristic, n1+n2=M)", mis_vals)
print_summary("RIS i.i.d from q_mix (uses m_i=1/M)", ris_iid_vals)
print_summary("RIS+MIS generalized (split n1+n2, per-candidate m_i)", ris_gen_vals)


# Plot data -------------------------------------------------------------------
xs = [i / 500.0 for i in range(501)]
fv = [f(x) for x in xs]
q1v = [q1_pdf(x) for x in xs]
q2v = [q2_pdf(x) for x in xs]
qmv = [q_mix_pdf(x) for x in xs]
phv = [p_hat(x) for x in xs]

mix_mean = [mean(mix_vals[m]) for m in M_VALUES]
mis_mean = [mean(mis_vals[m]) for m in M_VALUES]
ris_iid_mean = [mean(ris_iid_vals[m]) for m in M_VALUES]
ris_gen_mean = [mean(ris_gen_vals[m]) for m in M_VALUES]

mix_var = [variance(mix_vals[m]) for m in M_VALUES]
mis_var = [variance(mis_vals[m]) for m in M_VALUES]
ris_iid_var = [variance(ris_iid_vals[m]) for m in M_VALUES]
ris_gen_var = [variance(ris_gen_vals[m]) for m in M_VALUES]

mix_ci = [ci95(mix_vals[m]) for m in M_VALUES]
mis_ci = [ci95(mis_vals[m]) for m in M_VALUES]
ris_iid_ci = [ci95(ris_iid_vals[m]) for m in M_VALUES]
ris_gen_ci = [ci95(ris_gen_vals[m]) for m in M_VALUES]


# Figure 1: concise results view ----------------------------------------------
fig = plt.figure(figsize=(15, 6.5))
fig.suptitle("Two-technique MIS and RIS over mixed proposals (concise view)", fontsize=14, fontweight="bold")

gs = gridspec.GridSpec(1, 2, figure=fig, wspace=0.30, top=0.82, bottom=0.15, left=0.07, right=0.98)

ax1 = fig.add_subplot(gs[0, 0])
ax1.plot(xs, fv, lw=2.0, color="#1565c0", label="f(x)")
ax1.plot(xs, q1v, lw=1.8, color="#2e7d32", linestyle="--", label="q1(x) left-lobe beta")
ax1.plot(xs, q2v, lw=1.8, color="#ef6c00", linestyle="-.", label="q2(x) right-lobe beta")
ax1.plot(xs, qmv, lw=2.1, color="#6a1b9a", label="q_mix(x)")
ax1.plot(xs, phv, lw=1.8, color="#c62828", linestyle=":", label="p_hat(x) for RIS")
ax1.set_title("Two-lobe integrand, two proposals, mixed proposal, RIS target", fontsize=10)
ax1.set_xlabel("x")
ax1.set_ylabel("value")
ax1.set_ylim(-0.05, 2.2)
ax1.legend(fontsize=8, ncol=2)
ax1.text(
    0.5,
    -0.18,
    "q1 specializes on left lobe and q2 on right lobe. p_hat is intentionally close to f for RIS validation.",
    transform=ax1.transAxes,
    ha="center",
    va="top",
    fontsize=8,
    color="#444",
)

ax2 = fig.add_subplot(gs[0, 1])
ax2.plot(M_VALUES, mix_var, marker="o", color="#37474f", label="Mixture IS")
ax2.plot(M_VALUES, mis_var, marker="s", color="#2e7d32", label="Two-tech MIS")
ax2.plot(M_VALUES, ris_iid_var, marker="^", color="#c62828", label="RIS i.i.d q_mix")
ax2.plot(M_VALUES, ris_gen_var, marker="D", color="#1565c0", label="RIS+MIS generalized")
ax2.set_xscale("log", base=2)
ax2.set_yscale("log")
ax2.set_xticks(M_VALUES)
ax2.get_xaxis().set_major_formatter(plt.ScalarFormatter())
ax2.set_title("Variance vs M (main statistical signal)", fontsize=10)
ax2.set_xlabel("M")
ax2.set_ylabel("variance")
ax2.legend(fontsize=8)
ax2.text(
    0.5,
    -0.18,
    "All methods are unbiased here; this panel focuses only on variance reduction as M grows.",
    transform=ax2.transAxes,
    ha="center",
    va="top",
    fontsize=8,
    color="#444",
)


# Figure 3: unbiasedness + practical advantage (minimal story) ----------------
fig3 = plt.figure(figsize=(15, 6.5))
fig3.suptitle("Message: RIS+MIS stays unbiased and can be cheaper in expensive-shading", fontsize=14, fontweight="bold")
gs3 = gridspec.GridSpec(1, 2, figure=fig3, wspace=0.32, top=0.82, bottom=0.15, left=0.07, right=0.98)

b1 = fig3.add_subplot(gs3[0, 0])
b1.errorbar(M_VALUES, mis_mean, yerr=mis_ci, marker="s", color="#2e7d32", label="Two-tech MIS")
b1.errorbar(M_VALUES, ris_gen_mean, yerr=ris_gen_ci, marker="D", color="#1565c0", label="RIS+MIS generalized")
b1.axhline(TRUE, color="black", linestyle="--", lw=1.0)
b1.set_xscale("log", base=2)
b1.set_xticks(M_VALUES)
b1.get_xaxis().set_major_formatter(plt.ScalarFormatter())
b1.set_title("1) Unbiasedness check (means with 95% CI)", fontsize=10)
b1.set_xlabel("M")
b1.set_ylabel("estimate mean")
b1.legend(fontsize=8)
b1.text(
    0.5,
    -0.18,
    "Both curves overlap I_true: RIS+MIS does not introduce visible bias in this test.",
    transform=b1.transAxes,
    ha="center",
    va="top",
    fontsize=8,
    color="#444",
)

b2 = fig3.add_subplot(gs3[0, 1])
common_timing_m = [m for m in M_TIMING_VALUES if m in M_VALUES]
timing_idx = [M_VALUES.index(m) for m in common_timing_m]
timing_pos = [M_TIMING_VALUES.index(m) for m in common_timing_m]
mis_var_timing = [mis_var[i] for i in timing_idx]
ris_var_timing = [ris_gen_var[i] for i in timing_idx]
eff_mis = [mis_var_timing[i] * timing_mis_costly_us[timing_pos[i]] for i in range(len(common_timing_m))]
eff_ris = [ris_var_timing[i] * timing_ris_gen_costly_us[timing_pos[i]] for i in range(len(common_timing_m))]
ratio = [eff_ris[i] / eff_mis[i] if eff_mis[i] > 0 else float("inf") for i in range(len(common_timing_m))]
b2.plot(common_timing_m, ratio, marker="^", color="#c62828", lw=1.9)
b2.axhline(1.0, color="#444", linestyle=":", lw=1.0)
b2.set_xscale("log", base=2)
b2.set_yscale("log")
b2.set_xticks(common_timing_m)
b2.get_xaxis().set_major_formatter(plt.ScalarFormatter())
b2.set_title("2) Efficiency gain: (RIS+MIS) / MIS", fontsize=10)
b2.set_xlabel("M")
b2.set_ylabel("(variance*time) ratio")
for m, r in zip(common_timing_m, ratio):
    b2.text(m, r * 1.06, f"{r:.2f}x", ha="center", va="bottom", fontsize=8, color="#c62828")
b2.text(
    0.5,
    -0.18,
    "Below 1.0 is better for RIS+MIS. The drop with M shows the practical advantage when f is expensive.",
    transform=b2.transAxes,
    ha="center",
    va="top",
    fontsize=8,
    color="#444",
)


# Figure 4: varying number of estimates N ------------------------------------
n_sweep = [50, 100, 200, 500, 1000, 2000, 4000]
fixed_m = 16
sweep_mis = sweep_sample_count(two_tech_mis_estimate, fixed_m, n_sweep, reps=30)
sweep_ris = sweep_sample_count(ris_mis_generalized_estimate, fixed_m, n_sweep, reps=30)

mis_sweep_mean = [mean(sweep_mis[n]) for n in n_sweep]
ris_sweep_mean = [mean(sweep_ris[n]) for n in n_sweep]
mis_sweep_ci = [ci95(sweep_mis[n]) for n in n_sweep]
ris_sweep_ci = [ci95(sweep_ris[n]) for n in n_sweep]

mis_sweep_rmse = [math.sqrt(mean([(v - TRUE) ** 2 for v in sweep_mis[n]])) for n in n_sweep]
ris_sweep_rmse = [math.sqrt(mean([(v - TRUE) ** 2 for v in sweep_ris[n]])) for n in n_sweep]

fig4 = plt.figure(figsize=(15, 6.5))
fig4.suptitle("Varying the Number of Estimates N (fixed M=16)", fontsize=14, fontweight="bold")
gs4 = gridspec.GridSpec(1, 2, figure=fig4, wspace=0.30, top=0.82, bottom=0.15, left=0.07, right=0.98)

c1 = fig4.add_subplot(gs4[0, 0])
c1.errorbar(n_sweep, mis_sweep_mean, yerr=mis_sweep_ci, marker="s", color="#2e7d32", label="Two-tech MIS")
c1.errorbar(n_sweep, ris_sweep_mean, yerr=ris_sweep_ci, marker="D", color="#1565c0", label="RIS+MIS generalized")
c1.axhline(TRUE, color="black", linestyle="--", lw=1.0)
c1.set_xscale("log")
c1.set_title("Mean estimate vs N (95% CI over repeated runs)", fontsize=10)
c1.set_xlabel("N (number of estimates)")
c1.set_ylabel("estimate mean")
c1.legend(fontsize=8)
c1.text(
    0.5,
    -0.18,
    "As N grows, intervals tighten around 1.0: both estimators remain unbiased while uncertainty shrinks.",
    transform=c1.transAxes,
    ha="center",
    va="top",
    fontsize=8,
    color="#444",
)

c2 = fig4.add_subplot(gs4[0, 1])
c2.plot(n_sweep, mis_sweep_rmse, marker="s", color="#2e7d32", label="Two-tech MIS")
c2.plot(n_sweep, ris_sweep_rmse, marker="D", color="#1565c0", label="RIS+MIS generalized")
c2.set_xscale("log")
c2.set_yscale("log")
c2.set_title("RMSE of sample mean vs N", fontsize=10)
c2.set_xlabel("N (number of estimates)")
c2.set_ylabel("RMSE")
c2.legend(fontsize=8)
c2.text(
    0.5,
    -0.18,
    "Both curves drop roughly like 1/sqrt(N). More samples always improve stability.",
    transform=c2.transAxes,
    ha="center",
    va="top",
    fontsize=8,
    color="#444",
)


# Figure 5: target quality sweep ---------------------------------------------
quality_alphas = [0.0, 0.25, 0.5, 0.75, 1.0]
quality_m = 16
quality_n = 3500

quality_ris_vals = {}
for a in quality_alphas:
    ph = lambda x, aa=a: p_hat_quality(aa, x)
    quality_ris_vals[a] = [
        ris_mis_generalized_estimate(quality_m, p_hat_eval=ph)[0]
        for _ in range(quality_n)
    ]

quality_ris_var = [variance(quality_ris_vals[a]) for a in quality_alphas]
quality_ris_bias = [mean(quality_ris_vals[a]) - TRUE for a in quality_alphas]

grid_q = [i / 500.0 for i in range(1, 500)]
f_grid = [f(x) for x in grid_q]
f_norm = math.sqrt(sum(v * v for v in f_grid) / len(f_grid))
quality_rel_l2 = []
for a in quality_alphas:
    ph_vals = [p_hat_quality(a, x) for x in grid_q]
    err = math.sqrt(sum((u - v) ** 2 for u, v in zip(ph_vals, f_grid)) / len(f_grid))
    quality_rel_l2.append(err / max(f_norm, 1.0e-12))

quality_mis_var = variance(mis_vals[quality_m])
quality_mis_time = benchmark_method(two_tech_mis_estimate, quality_m, iters=1200, repeats=3)
quality_mis_cost = quality_mis_var * quality_mis_time

quality_ris_time = []
for a in quality_alphas:
    ph = lambda x, aa=a: p_hat_quality(aa, x)
    t = benchmark_method(
        lambda mm, p=ph: ris_mis_generalized_estimate(mm, f_eval=f_costly, p_hat_eval=p)[0],
        quality_m,
        iters=900,
        repeats=3,
    )
    quality_ris_time.append(t)

quality_ris_eff_ratio = [
    (quality_ris_var[i] * quality_ris_time[i]) / max(quality_mis_cost, 1.0e-12)
    for i in range(len(quality_alphas))
]

fig5 = plt.figure(figsize=(15, 6.5))
fig5.suptitle("Effect of target quality: how close p_hat is to f", fontsize=14, fontweight="bold")
gs5 = gridspec.GridSpec(1, 2, figure=fig5, wspace=0.30, top=0.82, bottom=0.15, left=0.07, right=0.98)

d1 = fig5.add_subplot(gs5[0, 0])
d1.plot(quality_alphas, quality_ris_var, marker="D", color="#1565c0", label="RIS+MIS variance")
d1.set_yscale("log")
d1.set_xlabel("target quality alpha (0=poor, 1=close to f)")
d1.set_ylabel("variance")
d1.set_title("RIS+MIS variance vs target quality", fontsize=10)
for a, b, l2 in zip(quality_alphas, quality_ris_bias, quality_rel_l2):
    d1.text(a, quality_ris_var[quality_alphas.index(a)] * 1.08, f"bias={b:+.3g}\nrelL2={l2:.2f}",
            ha="center", va="bottom", fontsize=7, color="#444")
d1.text(
    0.5,
    -0.18,
    "As p_hat gets closer to f (lower relL2), RIS+MIS variance drops strongly while bias stays near zero.",
    transform=d1.transAxes,
    ha="center",
    va="top",
    fontsize=8,
    color="#444",
)

d2 = fig5.add_subplot(gs5[0, 1])
d2.plot(quality_alphas, quality_ris_eff_ratio, marker="^", color="#c62828", lw=1.9)
d2.axhline(1.0, color="#444", linestyle=":", lw=1.0)
d2.set_yscale("log")
d2.set_xlabel("target quality alpha (0=poor, 1=close to f)")
d2.set_ylabel("efficiency ratio vs MIS\n(RIS+MIS var*time) / (MIS var*time)")
d2.set_title("Practical gain vs target quality (fixed M=16)", fontsize=10)
for a, r in zip(quality_alphas, quality_ris_eff_ratio):
    d2.text(a, r * 1.06, f"{r:.2f}x", ha="center", va="bottom", fontsize=8, color="#c62828")
d2.text(
    0.5,
    -0.18,
    "Below 1 is better than MIS. Better target quality gives larger RIS+MIS practical gains.",
    transform=d2.transAxes,
    ha="center",
    va="top",
    fontsize=8,
    color="#444",
)


# Figure 2: RIS worked table --------------------------------------------------
fig2, ax = plt.subplots(figsize=(18, 8))
ax.axis("off")
ax.set_title(
    "Single RIS+MIS generalized draw (M=16): per-candidate m_i(x), source pdf, and RIS weights",
    fontsize=12,
    fontweight="bold",
    loc="left",
    pad=12,
)

est, w_y, cands, weights, chosen = single_ris
w_sum = sum(weights)
rows = []
for i, ((x, tech, p_src, m_i), w) in enumerate(zip(cands, weights)):
    q1x = q1_pdf(x)
    q2x = q2_pdf(x)
    p_i = (w / w_sum) if w_sum > 0.0 else 0.0
    marker = "*" if i == chosen else ""
    rows.append([
        str(i) + marker,
        str(tech),
        "{:.6f}".format(x),
        "{:.4f}".format(q1x),
        "{:.4f}".format(q2x),
        "{:.4f}".format(p_src),
        "{:.6f}".format(m_i),
        "{:.6f}".format(p_hat(x)),
        "{:.8f}".format(w),
        "{:.6f}".format(p_i),
    ])

col_labels = [
    "i",
    "tech",
    "x_i",
    "q1(x_i)",
    "q2(x_i)",
    "source p_i(x_i)",
    "generalized m_i(x_i)",
    "p_hat(x_i)",
    "resample w_i",
    "select prob p_i",
]

tbl = ax.table(
    cellText=rows,
    colLabels=col_labels,
    loc="center",
    cellLoc="center",
    colWidths=[0.05, 0.05, 0.09, 0.08, 0.08, 0.11, 0.12, 0.1, 0.14, 0.12],
)
tbl.auto_set_font_size(False)
tbl.set_fontsize(9)
tbl.scale(1.0, 1.45)

for j in range(len(col_labels)):
    tbl[0, j].set_facecolor("#0d47a1")
    tbl[0, j].set_text_props(color="white", fontweight="bold", fontsize=9)

for i in range(1, len(rows) + 1):
    bg = "#e3f2fd" if i % 2 == 0 else "white"
    for j in range(len(col_labels)):
        tbl[i, j].set_facecolor(bg)

ax.text(
    0.01,
    0.06,
    "Chosen row is marked with *.  w_i = m_i(x_i)*p_hat(x_i)/p_i(x_i), W_Y=sum_j w_j / p_hat(Y).",
    transform=ax.transAxes,
    fontsize=10,
)
ax.text(
    0.01,
    0.02,
    "M=16  |  sum_j w_j={:.8f}  |  W_Y={:.6f}  |  estimate={:.6f}".format(w_sum, w_y, est),
    transform=ax.transAxes,
    fontsize=10,
)
ax.text(
    0.5,
    -0.03,
    "Here m_i(x) is the generalized MIS coefficient for non-i.i.d candidates from q1 and q2 (instead of fixed 1/M).",
    transform=ax.transAxes,
    ha="center",
    fontsize=8,
    color="#444",
)


# Save outputs ----------------------------------------------------------------
out = os.path.dirname(os.path.abspath(__file__))
p1 = os.path.join(out, "ris_mis_two_techniques_results.png")
p2 = os.path.join(out, "ris_mis_two_techniques_table.png")
p3 = os.path.join(out, "ris_mis_two_techniques_unbias_cost.png")
p4 = os.path.join(out, "ris_mis_two_techniques_vary_N.png")
p5 = os.path.join(out, "ris_mis_two_techniques_target_quality.png")
p6 = os.path.join(out, "ris_mis_two_techniques_target_quality_interactive.html")

log_progress("Saving static figures...")
fig.savefig(p1, dpi=130, bbox_inches="tight")
log_progress("Saved {}".format(os.path.basename(p1)))
fig2.savefig(p2, dpi=130, bbox_inches="tight")
log_progress("Saved {}".format(os.path.basename(p2)))
fig3.savefig(p3, dpi=130, bbox_inches="tight")
log_progress("Saved {}".format(os.path.basename(p3)))
fig4.savefig(p4, dpi=130, bbox_inches="tight")
log_progress("Saved {}".format(os.path.basename(p4)))
fig5.savefig(p5, dpi=130, bbox_inches="tight")
log_progress("Saved {}".format(os.path.basename(p5)))

# Interactive HTML with two sliders: target quality alpha and sample count M.
log_progress("Building interactive payload: setup curves and variance tables...")
interactive_m_values = [1, 4, 16, 64, 256, 800]
# Quality of the MIS proposal PDFs used to generate candidates.
# 0.0 = weak/near-uniform proposals, 1.0 = stronger lobe-aware proposals.
interactive_mis_pdf_qualities = [0.0, 0.25, 0.5, 0.75, 1.0]
# Dense near 1.0 to diagnose behavior when p_hat is very close to f.
interactive_quality_alphas = [0.0, 0.25, 0.5, 0.75, 0.85, 0.90, 0.95, 0.98, 1.0]
interactive_grid_x = [i / 400.0 for i in range(401)]
interactive_f_curve = [f(x) for x in interactive_grid_x]
interactive_q1_curves = [
    [q1_pdf_quality(q, x) for x in interactive_grid_x]
    for q in interactive_mis_pdf_qualities
]
interactive_q2_curves = [
    [q2_pdf_quality(q, x) for x in interactive_grid_x]
    for q in interactive_mis_pdf_qualities
]
interactive_qmix_curves = [
    [q_mix_pdf(x, mis_quality=q) for x in interactive_grid_x]
    for q in interactive_mis_pdf_qualities
]
interactive_bad_curve = [p_hat_bad(x) for x in interactive_grid_x]
interactive_phat_curves = [
    [p_hat_quality(a, x) for x in interactive_grid_x]
    for a in interactive_quality_alphas
]

interactive_mis_var_grid = []
interactive_ris_var_grid = []
if FAST_MODE:
    interactive_var_trials = 280
elif STABLE_MODE:
    interactive_var_trials = 350
else:
    interactive_var_trials = 220

for m in interactive_m_values:
    log_progress("Interactive variance grid: start M={} ({}/{})".format(
        m, interactive_m_values.index(m) + 1, len(interactive_m_values)))

    row_mis_q = []
    row_ris_q_alpha = []
    for q in interactive_mis_pdf_qualities:
        stage_q_t0 = time.perf_counter()
        log_progress("Interactive variance grid: M={} misQ={:.2f} start MIS var (trials={})".format(
            m, q, interactive_var_trials))
        mis_vals_q = [
            two_tech_mis_estimate(m, mis_quality=q)
            for _ in range(interactive_var_trials)
        ]
        mis_var_q = variance(mis_vals_q)
        row_mis_q.append(mis_var_q)
        log_progress("Interactive variance grid: M={} misQ={:.2f} MIS var done in {:.1f}s, var={:.3e}".format(
            m, q, time.perf_counter() - stage_q_t0, mis_var_q))

        row_alpha = []
        for a in interactive_quality_alphas:
            stage_t0 = time.perf_counter()
            log_progress("Interactive variance grid: M={} misQ={:.2f} alpha={:.2f} start".format(m, q, a))
            ph = lambda x, aa=a: p_hat_quality(aa, x)
            vals = [
                ris_mis_generalized_estimate(m, p_hat_eval=ph, mis_quality=q)[0]
                for _ in range(interactive_var_trials)
            ]
            ris_var = variance(vals)
            row_alpha.append(ris_var)
            log_progress("Interactive variance grid: M={} misQ={:.2f} alpha={:.2f} done in {:.1f}s, var={:.3e}".format(
                m, q, a, time.perf_counter() - stage_t0, ris_var))

        row_ris_q_alpha.append(row_alpha)

    interactive_mis_var_grid.append(row_mis_q)
    interactive_ris_var_grid.append(row_ris_q_alpha)
    log_progress("Interactive variance grid: completed M={}".format(m))

interactive_all_var_values = []
for mi, _m in enumerate(interactive_m_values):
    interactive_all_var_values.extend(interactive_mis_var_grid[mi])
    for qi, _q in enumerate(interactive_mis_pdf_qualities):
        interactive_all_var_values.extend(interactive_ris_var_grid[mi][qi])

interactive_var_y_min = min(interactive_all_var_values)
interactive_var_y_max = max(interactive_all_var_values)
interactive_var_y_range_log10 = [
    math.log10(interactive_var_y_min) - 0.2,
    math.log10(interactive_var_y_max) + 0.2,
]

interactive_payload = {
    "x": interactive_grid_x,
    "f": interactive_f_curve,
    "q1Curves": interactive_q1_curves,
    "q2Curves": interactive_q2_curves,
    "qmixCurves": interactive_qmix_curves,
    "bad": interactive_bad_curve,
    "phatCurves": interactive_phat_curves,
    "alphas": interactive_quality_alphas,
    "misQualities": interactive_mis_pdf_qualities,
    "mValues": interactive_m_values,
    "risVarByMQAlpha": interactive_ris_var_grid,
    "misVarByMQ": interactive_mis_var_grid,
    "varYRangeLog10": interactive_var_y_range_log10,
}

# Convergence-speed diagnostics for interactive view (same sliders: M and alpha).
# Empirical checkpoints + denser theory grid.
if FAST_MODE:
    interactive_n_conv = [32, 64, 128, 256, 512, 1024, 2048]
    interactive_n_conv_theory = [
        32, 64, 128, 256, 512, 1024, 2048,
        4096, 8192,
    ]
elif STABLE_MODE:
    interactive_n_conv = [64, 128, 256, 512, 1024, 2048]
    interactive_n_conv_theory = [
        64, 128, 256, 512, 1024, 2048,
        4096, 8192,
    ]
else:
    interactive_n_conv = [32, 64, 128, 256, 512, 1024, 2048]
    interactive_n_conv_theory = [
        32, 64, 128, 256, 512, 1024, 2048,
        4096, 8192,
    ]

# MIS convergence depends on M and on MIS-pdf quality.
log_progress("Building convergence curves for MIS...")
interactive_mis_conv_by_m_q = []
if FAST_MODE:
    interactive_conv_reps = 4
elif STABLE_MODE:
    interactive_conv_reps = 3
else:
    interactive_conv_reps = 3
for m in interactive_m_values:
    log_progress("MIS convergence: start M={}".format(m))
    row_q = []
    for q in interactive_mis_pdf_qualities:
        sweep = sweep_sample_count_prefix(
            lambda mm, qq=q: two_tech_mis_estimate(mm, mis_quality=qq),
            m,
            interactive_n_conv,
            reps=interactive_conv_reps,
            progress_label="MIS convergence M={} misQ={:.2f}".format(m, q),
        )
        err_line = [math.sqrt(mean([(v - TRUE) ** 2 for v in sweep[n]])) for n in interactive_n_conv]
        row_q.append(err_line)
    log_progress("MIS convergence: completed M={}".format(m))
    interactive_mis_conv_by_m_q.append(row_q)

# RIS+MIS convergence depends on M, MIS-pdf quality and target quality alpha.
log_progress("Building convergence curves for RIS+MIS...")
interactive_ris_conv_by_m_q_alpha = []
for m in interactive_m_values:
    log_progress("RIS+MIS convergence: start M={} ({}/{})".format(
        m, interactive_m_values.index(m) + 1, len(interactive_m_values)))
    row_q = []
    for q in interactive_mis_pdf_qualities:
        row_alpha = []
        for a in interactive_quality_alphas:
            log_progress("RIS+MIS convergence: start M={} misQ={:.2f} alpha={:.2f}".format(m, q, a))
            ph = lambda x, aa=a: p_hat_quality(aa, x)
            sweep = sweep_sample_count_prefix(
                lambda mm, qq=q, pph=ph: ris_mis_generalized_estimate(mm, p_hat_eval=pph, mis_quality=qq)[0],
                m,
                interactive_n_conv,
                reps=interactive_conv_reps,
                progress_label="RIS+MIS convergence M={} misQ={:.2f} alpha={:.2f}".format(m, q, a),
            )
            err_line = [math.sqrt(mean([(v - TRUE) ** 2 for v in sweep[n]])) for n in interactive_n_conv]
            row_alpha.append(err_line)
            log_progress("RIS+MIS convergence: completed M={} misQ={:.2f} alpha={:.2f}".format(m, q, a))
        row_q.append(row_alpha)
    interactive_ris_conv_by_m_q_alpha.append(row_q)
    log_progress("RIS+MIS convergence: completed M={}".format(m))

interactive_payload["nConv"] = interactive_n_conv
interactive_payload["nConvTheory"] = interactive_n_conv_theory
interactive_payload["misConvByMQ"] = interactive_mis_conv_by_m_q
interactive_payload["risConvByMQAlpha"] = interactive_ris_conv_by_m_q_alpha

# Endpoint N where both MIS and RIS+MIS are considered converged for selected
# (M, mis-pdf quality, target quality alpha).
interactive_conv_end_idx = []
for mi, _m in enumerate(interactive_m_values):
    row_q = []
    for qi, _q in enumerate(interactive_mis_pdf_qualities):
        row_alpha = []
        mis_line = interactive_mis_conv_by_m_q[mi][qi]
        for ai, _a in enumerate(interactive_quality_alphas):
            ris_line = interactive_ris_conv_by_m_q_alpha[mi][qi][ai]
            row_alpha.append(find_convergence_end_index(mis_line, ris_line, tol=0.1, stable_window=2))
        row_q.append(row_alpha)
    interactive_conv_end_idx.append(row_q)

interactive_payload["convEndIdx"] = interactive_conv_end_idx

log_progress("Serializing interactive HTML payload...")
interactive_html = f"""<!doctype html>
<html>
<head>
    <meta charset=\"utf-8\" />
    <title>RIS+MIS interactive target quality</title>
    <script src=\"https://cdn.plot.ly/plotly-2.35.2.min.js\"></script>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif; margin: 20px; }}
        .row {{ display: grid; grid-template-columns: 1fr 1fr; gap: 16px; }}
        .controls {{ margin: 12px 0 18px; padding: 12px; border: 1px solid #ddd; border-radius: 8px; }}
        label {{ font-weight: 600; display: block; margin-bottom: 6px; }}
        input[type=range] {{ width: 100%; }}
        .value {{ font-weight: 700; color: #0d47a1; }}
        .note {{ color: #444; font-size: 13px; margin-top: 8px; }}
    </style>
</head>
<body>
    <h2>RIS+MIS: qualità del target p_hat e impatto pratico</h2>
    <div class=\"controls\">
        <label>Slider 1 - Qualità target alpha (0 = target scarso, 1 = target vicino a f): <span id=\"alphaVal\" class=\"value\"></span></label>
        <input id=\"alphaSlider\" type=\"range\" min=\"0\" max=\"{len(interactive_quality_alphas)-1}\" step=\"1\" value=\"2\" />

        <label style=\"margin-top:10px\">Slider 2 - Qualità pdf MIS q (0 debole, 1 più informata): <span id=\"misQVal\" class=\"value\"></span></label>
        <input id=\"misQSlider\" type=\"range\" min=\"0\" max=\"{len(interactive_mis_pdf_qualities)-1}\" step=\"1\" value=\"2\" />

        <label style=\"margin-top:10px\">Slider 3 - Number of candidates M: <span id=\"mVal\" class=\"value\"></span></label>
        <input id=\"mSlider\" type=\"range\" min=\"0\" max=\"{len(interactive_m_values)-1}\" step=\"1\" value=\"2\" />

        <label style=\"margin-top:10px\">Slider 4 - Scala assoluta convergenza (Y max): <span id=\"absYVal\" class=\"value\"></span></label>
        <input id=\"absYSlider\" type=\"range\" min=\"-2\" max=\"2\" step=\"0.1\" value=\"0\" />

        <div class=\"note\">Significato di alpha: interpola tra due target per RIS. Alpha=0 usa un target poco informativo; alpha=1 usa un target molto vicino alla funzione reale f. Le curve q1 e q2 sono le pdf usate da MIS e sono volutamente solo correlate a f, non costruite come copie della funzione reale. Per ridurre rumore statistico e instabilita numerica, le pdf di proposta includono un piccolo floor uniforme (evita denominatori quasi nulli). La convergenza usa RMSE, non errore assoluto medio.</div>
        <div id=\"summary\" class=\"note\"></div>
    </div>

    <div class=\"row\">
        <div id=\"curvePlot\" style=\"height:470px;\"></div>
        <div id=\"ratioPlot\" style=\"height:470px;\"></div>
    </div>
    <div style=\"margin-top:14px;\">
        <div id=\"convPlot\" style=\"height:520px;\"></div>
    </div>
    <div style=\"margin-top:14px;\">
        <div id=\"convAbsPlot\" style=\"height:520px;\"></div>
    </div>

    <script>
        const data = {json.dumps(interactive_payload)};
        const alphaSlider = document.getElementById('alphaSlider');
        const misQSlider = document.getElementById('misQSlider');
        const mSlider = document.getElementById('mSlider');
        const absYSlider = document.getElementById('absYSlider');
        const alphaVal = document.getElementById('alphaVal');
        const misQVal = document.getElementById('misQVal');
        const mVal = document.getElementById('mVal');
        const absYVal = document.getElementById('absYVal');
        const summary = document.getElementById('summary');
        const convCache = new Map();
        let renderPending = false;

        function scheduleRender() {{
            if (renderPending) {{
                return;
            }}
            renderPending = true;
            requestAnimationFrame(() => {{
                renderPending = false;
                render();
            }});
        }}

        function getConvergencePlots(mi, qi, ai) {{
            const cacheKey = mi + ':' + qi + ':' + ai;
            if (convCache.has(cacheKey)) {{
                return convCache.get(cacheKey);
            }}

            const nConv = data.nConv;
            const nConvTheory = data.nConvTheory;
            const misConv = data.misConvByMQ[mi][qi];
            const risConv = data.risConvByMQAlpha[mi][qi][ai];
            const endIdx = data.convEndIdx[mi][qi][ai];
            const endN = nConv[endIdx];
            const nConvCut = nConv.slice(0, endIdx + 1);
            const misConvCut = misConv.slice(0, endIdx + 1);
            const risConvCut = risConv.slice(0, endIdx + 1);
            const nConvTheoryCut = nConvTheory.filter(n => n <= endN);

            // For an unbiased estimator: E|I_hat_N - I| ~ sqrt(2/pi)*sigma/sqrt(N).
            const c = Math.sqrt(2.0 / Math.PI);
            const misVar = data.misVarByMQ[mi][qi];
            const risVar = data.risVarByMQAlpha[mi][qi][ai];
            const misTheory = nConvTheoryCut.map(n => c * Math.sqrt(misVar / n));
            const risTheory = nConvTheoryCut.map(n => c * Math.sqrt(risVar / n));

            const plots = {{
                endN: endN,
                misFinalErr: misConvCut[misConvCut.length - 1],
                risFinalErr: risConvCut[risConvCut.length - 1],
                convTraces: [
                    {{x: nConvTheoryCut, y: misTheory, type:'scatter', mode:'lines', name:'MIS teoria ~1/sqrt(N)', line:{{color:'#2e7d32', dash:'dot'}}}},
                    {{x: nConvTheoryCut, y: risTheory, type:'scatter', mode:'lines', name:'RIS+MIS teoria ~1/sqrt(N)', line:{{color:'#1565c0', dash:'dot'}}}},
                    {{x: nConvCut, y: misConvCut, type:'scatter', mode:'lines+markers', name:'MIS empirico RMSE', line:{{color:'#2e7d32'}}}},
                    {{x: nConvCut, y: risConvCut, type:'scatter', mode:'lines+markers', name:'RIS+MIS empirico RMSE', line:{{color:'#1565c0'}}}}
                ],
                absTraces: [
                    {{x: nConvCut, y: misConvCut, type:'scatter', mode:'lines+markers', name:'MIS empirico RMSE', line:{{color:'#2e7d32'}}}},
                    {{x: nConvCut, y: risConvCut, type:'scatter', mode:'lines+markers', name:'RIS+MIS empirico RMSE', line:{{color:'#1565c0'}}}}
                ]
            }};

            convCache.set(cacheKey, plots);
            return plots;
        }}

        function render() {{
            const ai = parseInt(alphaSlider.value, 10);
            const qi = parseInt(misQSlider.value, 10);
            const mi = parseInt(mSlider.value, 10);
            const alpha = data.alphas[ai];
            const misQ = data.misQualities[qi];
            const m = data.mValues[mi];
            const absYExp = parseFloat(absYSlider.value);
            const absYMax = Math.pow(10.0, absYExp);

            alphaVal.textContent = alpha.toFixed(2);
            misQVal.textContent = misQ.toFixed(2);
            mVal.textContent = m;
            absYVal.textContent = absYMax.toExponential(1);

            Plotly.react('curvePlot', [
                {{x: data.x, y: data.f, type:'scatter', mode:'lines', name:'f(x)', line:{{color:'#1565c0', width:3}}}},
                {{x: data.x, y: data.q1Curves[qi], type:'scatter', mode:'lines', name:'q1 MIS pdf', line:{{color:'#2e7d32', dash:'dot'}}}},
                {{x: data.x, y: data.q2Curves[qi], type:'scatter', mode:'lines', name:'q2 MIS pdf', line:{{color:'#6a1b9a', dash:'dot'}}}},
                {{x: data.x, y: data.qmixCurves[qi], type:'scatter', mode:'lines', name:'q_mix MIS pdf', line:{{color:'#5d4037', dash:'dashdot'}}}},
                {{x: data.x, y: data.bad, type:'scatter', mode:'lines', name:'p_hat poor', line:{{color:'#ef6c00', dash:'dot'}}}},
                {{x: data.x, y: data.phatCurves[ai], type:'scatter', mode:'lines', name:'p_hat selected', line:{{color:'#c62828', dash:'dash'}}}}
            ], {{
                title:'Confronto funzioni: f, pdf MIS e target p_hat selezionato',
                xaxis:{{title:'asse x (dominio campionamento)', titlefont:{{size:14}}, tickfont:{{size:12}}}},
                yaxis:{{title:'valore funzione', titlefont:{{size:14}}, tickfont:{{size:12}}}},
                margin:{{l:55, r:20, t:50, b:45}},
                legend:{{orientation:'h', y:1.12, x:0.0}}
            }}, {{displayModeBar:false}});

            const risVarLine = data.risVarByMQAlpha[mi][qi];
            const misVar = data.misVarByMQ[mi][qi];
            const risVar = risVarLine[ai];
            const misVarLine = data.alphas.map(() => misVar);

            Plotly.react('ratioPlot', [
                {{x: data.alphas, y: misVarLine, type:'scatter', mode:'lines+markers', name:'MIS varianza', line:{{color:'#2e7d32'}}}},
                {{x: data.alphas, y: risVarLine, type:'scatter', mode:'lines+markers', name:'RIS+MIS varianza', line:{{color:'#1565c0'}}}},
                {{x:[data.alphas[ai]], y:[misVar], type:'scatter', mode:'markers', name:'MIS selezionato', marker:{{color:'#2e7d32', size:10}}, showlegend:false}},
                {{x:[data.alphas[ai]], y:[risVar], type:'scatter', mode:'markers', name:'RIS+MIS selezionato', marker:{{color:'#1565c0', size:10}}, showlegend:false}}
            ], {{
                title:'Confronto con MIS: varianza vs qualità target (scala Y globale fissa)',
                xaxis:{{title:'qualità target alpha (0 scarso, 1 vicino a f)', titlefont:{{size:14}}, tickfont:{{size:12}}}},
                yaxis:{{title:'varianza (più basso è meglio)', type:'log', range:data.varYRangeLog10, titlefont:{{size:14}}, tickfont:{{size:12}}}},
                margin:{{l:70, r:20, t:60, b:80}},
                legend:{{orientation:'h', y:1.12, x:0.0}}
            }}, {{displayModeBar:false}});

            const convPlots = getConvergencePlots(mi, qi, ai);
            const endN = convPlots.endN;

            Plotly.react('convPlot', [
                ...convPlots.convTraces
            ], {{
                title:'Convergenza integrale vs N fino a N_end=' + endN + ' (punti empirici + trend teorico)',
                xaxis:{{title:'N (numero di campioni)', type:'log', titlefont:{{size:14}}, tickfont:{{size:12}}}},
                yaxis:{{title:'RMSE della stima integrale', type:'log', titlefont:{{size:14}}, tickfont:{{size:12}}}},
                margin:{{l:80, r:20, t:55, b:70}},
                legend:{{orientation:'h', y:1.12, x:0.0}}
            }}, {{displayModeBar:false}});

            Plotly.react('convAbsPlot', [
                ...convPlots.absTraces
            ], {{
                title:'Convergenza su scala assoluta fissa (Y max impostabile)',
                xaxis:{{title:'N (numero di campioni)', type:'log', titlefont:{{size:14}}, tickfont:{{size:12}}}},
                yaxis:{{title:'RMSE (scala lineare)', type:'linear', range:[0, absYMax], titlefont:{{size:14}}, tickfont:{{size:12}}}},
                margin:{{l:80, r:20, t:55, b:70}},
                legend:{{orientation:'h', y:1.12, x:0.0}}
            }}, {{displayModeBar:false}});

            summary.textContent =
                'M=' + m +
                ', misQ=' + misQ.toFixed(2) +
                ', alpha=' + alpha.toFixed(2) +
                ' -> MIS var=' + misVar.toExponential(3) +
                ', RIS+MIS var=' + risVar.toExponential(3) +
                ', differenza assoluta=' + Math.abs(misVar - risVar).toExponential(3) +
                ', RMSE finale MIS=' + convPlots.misFinalErr.toExponential(3) +
                ', RMSE finale RIS+MIS=' + convPlots.risFinalErr.toExponential(3) +
                ', N_end=' + endN +
                ' (RMSE rispetto a I_true=' + {TRUE:.1f} + ', andamento atteso ~1/sqrt(N)).';
        }}

        alphaSlider.addEventListener('input', scheduleRender);
        misQSlider.addEventListener('input', scheduleRender);
        mSlider.addEventListener('input', scheduleRender);
        absYSlider.addEventListener('input', scheduleRender);
        render();
    </script>
</body>
</html>
"""

with open(p6, "w", encoding="utf-8") as fhtml:
    fhtml.write(interactive_html)

log_progress("Saved {}".format(os.path.basename(p6)))
print("Saved: " + p1)
print("Saved: " + p2)
print("Saved: " + p3)
print("Saved: " + p4)
print("Saved: " + p5)
print("Saved: " + p6)
log_progress("All outputs generated.")

if "--show" in sys.argv:
    plt.show()
