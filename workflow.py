import argparse
import importlib.util
import os, re, sys, subprocess, platform, shutil

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR    = os.path.join(PROJECT_ROOT, "build")
EXTERNAL_DIR = os.path.join(PROJECT_ROOT, "external")
USD_SRC_DIR  = os.path.join(EXTERNAL_DIR, "OpenUSD")
# Where we build OpenUSD ourselves when no existing install is discoverable.
# This is NOT assumed to be the install location — discovery reads the prefix
# CMake actually linked against (see usd_install_dir) and only falls back here.
USD_LOCAL_INSTALL_DIR = os.path.join(EXTERNAL_DIR, "usd-install")
# Env var used to pin / advertise the resolved install to child processes
# (notably CMake, which reads it in CMakeLists.txt) so the whole toolchain
# agrees on one OpenUSD regardless of where it lives.
USD_INSTALL_ENV = "HDRESTIR_USD_INSTALL"
USD_VERSION_TAG = "v26.05"
OS = platform.system()


# ── OpenUSD discovery ─────────────────────────────────────────────
#
# The install location is never hard-coded. CMake's find_package(pxr CONFIG) is
# the authoritative discovery — it searches CMAKE_PREFIX_PATH, the environment
# and a PATH-derived prefix, and records the prefix it linked against as
# `pxr_DIR` in build/CMakeCache.txt. We read that back rather than re-deriving
# it, so the build and the runtime tools always agree on the same OpenUSD.
#
# Two narrow jobs remain Python-side, because CMake does neither:
#   * deciding whether a from-source build is needed (have_usd)
#   * an explicit user override / pin, via $HDRESTIR_USD_INSTALL, which we feed
#     into CMake so it wins there too.

def _looks_like_usd_prefix(path):
    return bool(path) and os.path.isfile(os.path.join(path, "pxrConfig.cmake"))


def _required_pxr_version():
    """(minor, patch) required by USD_VERSION_TAG (e.g. 'v26.05' -> (26, 5))."""
    m = re.match(r"v(\d+)\.(\d+)", USD_VERSION_TAG)
    return (int(m.group(1)), int(m.group(2))) if m else None


def _pxr_version_from_prefix(path):
    """(minor, patch) recorded in an installed pxrConfig.cmake, or None."""
    cfg = os.path.join(path, "pxrConfig.cmake")
    if not os.path.isfile(cfg):
        return None
    minor = patch = None
    with open(cfg, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r'set\(PXR_MINOR_VERSION\s+"(\d+)"\)', line)
            if m:
                minor = int(m.group(1))
            m = re.search(r'set\(PXR_PATCH_VERSION\s+"(\d+)"\)', line)
            if m:
                patch = int(m.group(1))
    return (minor, patch) if minor is not None and patch is not None else None


def _usd_prefix_matches_pinned_version(path):
    """Whether the USD install at `path` is the version we're pinned to.

    An install we can't read the version from (unexpected pxrConfig.cmake
    layout) is treated as unusable rather than silently accepted, so a stale
    or foreign build never gets linked against by surprise."""
    required = _required_pxr_version()
    if required is None:
        return True
    return _pxr_version_from_prefix(path) == required


def _usd_prefix_from_cache():
    """The OpenUSD install prefix CMake actually linked against, or None.

    CMake stores pxr_DIR as the *cmake config dir*, which may be the install
    prefix itself or a subdirectory of it (e.g. share/pxr or share/cmake/pxr,
    depending on how USD was installed). We walk up until we find a directory
    that looks like a real prefix (has bin/ or lib/) to recover the right
    root."""
    cache_file = os.path.join(BUILD_DIR, "CMakeCache.txt")
    if not os.path.isfile(cache_file):
        return None
    with open(cache_file, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("pxr_DIR"):
                cmake_dir = line.split("=", 1)[1].strip()
                break
        else:
            return None

    # cmake_dir may be the install prefix itself (a from-source USD build puts
    # pxrConfig.cmake directly in <prefix>/) or a subdirectory (some installs
    # put it at <prefix>/share/pxr). The real install prefix has a bin/ directory
    # with the USD tools. Walk up from cmake_dir until we find that bin/.
    candidate = cmake_dir
    for _ in range(4):
        if os.path.isdir(os.path.join(candidate, "bin")):
            return candidate
        parent = os.path.dirname(candidate)
        if parent == candidate:
            break
        candidate = parent
    # Fallback: return cmake_dir itself if no bin/ ancestor found
    return cmake_dir if _looks_like_usd_prefix(cmake_dir) else None


def usd_install_dir():
    """Install prefix to use at runtime (for usdview/usdrecord paths).

    Resolution order: the prefix CMake configured against (authoritative) →
    the explicit $HDRESTIR_USD_INSTALL pin → the local from-source build dir.
    """
    return (_usd_prefix_from_cache()
            or os.environ.get(USD_INSTALL_ENV, "")
            or USD_LOCAL_INSTALL_DIR)


def _cmake_finds_usd():
    """Ask CMake — the authority — whether find_package(pxr CONFIG) succeeds in
    the current environment *at the pinned version*, using a throwaway probe
    project. Mirrors exactly what the real configure will do, so we never
    build USD from source when CMake could have found a matching one (e.g.
    via a PATH-derived or conda prefix) — but we also never adopt a
    differently-versioned USD found this way."""
    import tempfile
    if not shutil.which("cmake"):
        return False  # no cmake yet; assume we need our own USD
    required = _required_pxr_version()
    probe = tempfile.mkdtemp(prefix="hdrestir_usdprobe_")
    try:
        with open(os.path.join(probe, "CMakeLists.txt"), "w") as f:
            f.write("cmake_minimum_required(VERSION 3.22)\n"
                    "project(usdprobe LANGUAGES NONE)\n"
                    "find_package(pxr CONFIG REQUIRED)\n"
                    'message(STATUS "HDRESTIR_PXR_VERSION="'
                    '"${PXR_MINOR_VERSION}.${PXR_PATCH_VERSION}")\n')
        result = subprocess.run(
            ["cmake", "-S", probe, "-B", os.path.join(probe, "build")],
            capture_output=True, text=True)
        if result.returncode != 0:
            return False
        if required is None:
            return True
        m = re.search(r"HDRESTIR_PXR_VERSION=(\d+)\.(\d+)", result.stdout)
        return bool(m) and (int(m.group(1)), int(m.group(2))) == required
    finally:
        shutil.rmtree(probe, ignore_errors=True)


def have_usd():
    """Whether an OpenUSD install already exists, so setup_usd() can skip the
    from-source build. Recognises the common cases cheaply first:
      * an explicit $HDRESTIR_USD_INSTALL pin
      * the local from-source build
      * a USD already on CMAKE_PREFIX_PATH
      * pxr importable in this interpreter (full install, has pxrConfig.cmake)
    If none match, defer to CMake (the authority) before committing to a heavy
    from-source build, so a USD reachable only via PATH/conda is still used."""
    candidates = [os.environ.get(USD_INSTALL_ENV, ""), USD_LOCAL_INSTALL_DIR]
    candidates += os.environ.get("CMAKE_PREFIX_PATH", "").split(os.pathsep)
    candidates = [c.strip().strip('"') for c in candidates]
    if any(_looks_like_usd_prefix(c) and _usd_prefix_matches_pinned_version(c)
           for c in candidates):
        return True

    try:
        spec = importlib.util.find_spec("pxr")
    except (ImportError, ValueError):
        spec = None
    if spec and spec.origin:
        # <prefix>/lib/python/pxr/__init__.py -> <prefix>
        prefix = spec.origin
        for _ in range(4):
            prefix = os.path.dirname(prefix)
        if _looks_like_usd_prefix(prefix) and _usd_prefix_matches_pinned_version(prefix):
            return True

    return _cmake_finds_usd()


def _python_version_at_least(python_command, major, minor):
    result = subprocess.run(
        [*python_command, "-c", "import sys; print(f'{sys.version_info[0]}.{sys.version_info[1]}')"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return False

    version_text = result.stdout.strip()
    if not version_text:
        return False

    try:
        current_major, current_minor = (int(part) for part in version_text.split(".", 1))
    except ValueError:
        return False

    return (current_major, current_minor) >= (major, minor)


def _find_python_at_least(major, minor):
    candidates = []

    current_python = shutil.which("python3")
    if current_python:
        candidates.append([current_python])

    for python_minor in range(minor, 21):
        python_name = f"python{major}.{python_minor}"
        python_path = shutil.which(python_name)
        if python_path:
            candidates.append([python_path])

    if OS == "Windows":
        py_launcher = shutil.which("py")
        if py_launcher:
            for python_minor in range(minor, 21):
                candidates.append([py_launcher, f"-{major}.{python_minor}"])

    seen = set()
    for candidate in candidates:
        resolved = os.path.realpath(candidate[0])
        if resolved in seen:
            continue
        seen.add(resolved)
        if _python_version_at_least(candidate, major, minor):
            return candidate

    return None


def _ensure_python_version():
    if sys.version_info[:2] >= (3, 11):
        return

    preferred_python = _find_python_at_least(3, 11)
    if preferred_python:
        if os.path.realpath(preferred_python[0]) != os.path.realpath(sys.executable):
            os.execvp(preferred_python[0], [*preferred_python, *sys.argv])
        return

    raise RuntimeError(
        f"Expected Python 3.11 or newer, but got {sys.version.split()[0]} from {sys.executable}. "
        "Install python3.11+ or put it earlier in PATH."
    )


_ensure_python_version()


# ── helpers ───────────────────────────────────────────────────────

def get_triplet():
    if OS == "Windows": return "x64-windows"
    if OS == "Darwin":  return "arm64-osx"
    return "x64-linux"

def run(cmd, cwd=PROJECT_ROOT, env=None):
    e = os.environ.copy()
    if env:
        e.update(env)
    print(f"\n>> {' '.join(str(x) for x in cmd)}\n")
    subprocess.check_call(cmd, cwd=cwd, env=e)

def _wipe_cmake_cache(build_dir, reason):
    print(f"\n[cmake] {reason} Removing stale cache from build/.\n")
    for path in (os.path.join(build_dir, "CMakeCache.txt"),
                 os.path.join(build_dir, "CMakeFiles")):
        if os.path.isdir(path):
            shutil.rmtree(path)
        elif os.path.isfile(path):
            os.remove(path)


def _prepare_build_dir_for_cmake(build_dir=BUILD_DIR):
    """Clear a stale CMake cache before configuring.

    Two things make a cache unusable without a matching from-scratch reconfigure:
    generated from a different source tree, or pinning a CMAKE_TOOLCHAIN_FILE
    that no longer exists on disk (e.g. a leftover from before the project
    dropped its vcpkg toolchain — CMake would otherwise fail hard trying to
    include a missing file, rather than just re-resolving dependencies)."""
    cache_file = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache_file):
        return

    expected = os.path.realpath(PROJECT_ROOT)
    cached_source = None
    cached_toolchain = None
    with open(cache_file, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
                cached_source = line.split("=", 1)[1].strip()
            elif line.startswith("CMAKE_TOOLCHAIN_FILE:"):
                cached_toolchain = line.split("=", 1)[1].strip()

    if cached_toolchain and not os.path.isfile(cached_toolchain):
        _wipe_cmake_cache(
            build_dir,
            f"Cached CMAKE_TOOLCHAIN_FILE {cached_toolchain!r} no longer exists.")
        return

    if cached_source and os.path.realpath(cached_source) != expected:
        _wipe_cmake_cache(build_dir, "Build cache points to another source tree.")


# ── OpenUSD (from source) ─────────────────────────────────────────

def _find_vcvars():
    """Locate vcvars64.bat — OpenUSD builds with MSVC on Windows and build_usd.py
    must run inside a Visual Studio developer environment."""
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = os.path.join(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if os.path.isfile(vswhere):
        found = subprocess.run(
            [vswhere, "-latest", "-property", "installationPath"],
            capture_output=True, text=True).stdout.strip()
        if found:
            cand = os.path.join(found, "VC", "Auxiliary", "Build", "vcvars64.bat")
            if os.path.isfile(cand):
                return cand
    for edition in ("Community", "Professional", "Enterprise", "BuildTools"):
        cand = os.path.join(r"C:\Program Files\Microsoft Visual Studio\2022",
                            edition, "VC", "Auxiliary", "Build", "vcvars64.bat")
        if os.path.isfile(cand):
            return cand
    raise RuntimeError(
        "Visual Studio 2022 (vcvars64.bat) not found — required to build OpenUSD "
        "from source on Windows.")


def _bundle_python_runtime(python_exe):
    """Copy the Python shared library next to the USD libraries.

    USD built with python support links python3XX.dll, so anything that loads
    USD (the C++ tests / the plugin) needs it at runtime. Bundling it keeps the
    install self-contained — no python install required on PATH."""
    if OS != "Windows":
        return  # Unix: libpython is resolved via the system / USD's rpath
    pydir = os.path.dirname(python_exe)
    for name in os.listdir(pydir):
        if re.fullmatch(r"python3\d+\.dll", name, re.IGNORECASE):
            shutil.copy2(os.path.join(pydir, name),
                         os.path.join(USD_LOCAL_INSTALL_DIR, "lib"))
            return


def _fix_usd_tool_wrappers():
    """Repair USD's broken Windows bin/ launchers (usdview, usdrecord, …).

    On Windows the install writes each wrapper as `@python "%~dp0<ABS PATH>" %*`,
    gluing the `%~dp0` (bin dir) prefix onto an already-absolute path to the real
    .py — so the file is neither a valid script nor a valid batch command and the
    tool won't run. Rewrite both `bin/<tool>` and `bin/<tool>.cmd` to correctly
    invoke the real launcher, so `usdview <scene>` works once PYTHONPATH/PATH are
    set."""
    if OS != "Windows":
        return
    bindir = os.path.join(USD_LOCAL_INSTALL_DIR, "bin")
    if not os.path.isdir(bindir):
        return
    for name in os.listdir(bindir):
        if name.endswith(".cmd"):
            continue
        path = os.path.join(bindir, name)
        try:
            with open(path, encoding="utf-8", errors="replace") as f:
                first = f.readline().strip()
        except OSError:
            continue
        m = re.match(r'@python\s+"%~dp0(.+\.py)"', first)
        if not m:
            continue
        wrapper = f'@python "{m.group(1)}" %*\r\n'
        for target in (path, path + ".cmd"):
            with open(target, "w", encoding="utf-8", newline="") as f:
                f.write(wrapper)


def setup_usd():
    """Ensure an OpenUSD install is available, building one only if needed.

    If any OpenUSD install is already discoverable (see have_usd), it is used
    as-is — nothing is built and external/ is never touched. Otherwise OpenUSD
    (imaging + python + tools + usdview) is built from source into
    external/usd-install.

    The from-source build runs with the interpreter that launched workflow.py —
    make.bat / make.sh guarantee that is a Python with C development files
    (installing one via winget / brew if needed). The first run is heavy (USD +
    TBB + OpenSubdiv); everything lives under external/ so cleaning build/ never
    triggers a USD rebuild.

    When a build is done, the local install is exported via
    $HDRESTIR_USD_INSTALL so the CMake configure that follows links against it.
    """
    if have_usd():
        return

    os.makedirs(EXTERNAL_DIR, exist_ok=True)
    if not os.path.isdir(USD_SRC_DIR):
        run(["git", "clone", "--depth", "1", "--branch", USD_VERSION_TAG,
             "https://github.com/PixarAnimationStudios/OpenUSD.git", USD_SRC_DIR])

    build_python = sys.executable
    pip_install = [build_python, "-m", "pip", "install", "-q",
                   "jinja2", "PyOpenGL", "PySide6"]
    pip_env = {**os.environ, "PIP_USER": "0"}
    try:
        subprocess.check_call(pip_install, env=pip_env)
    except subprocess.CalledProcessError:
        # PEP 668: distro-packaged interpreters (e.g. Homebrew's python3) refuse
        # to pip-install into their own site-packages. This install only feeds
        # the throwaway USD-from-source build (never the plugin runtime), so
        # overriding that guard here is safe.
        subprocess.check_call([*pip_install, "--break-system-packages"], env=pip_env)

    build_usd = os.path.join(USD_SRC_DIR, "build_scripts", "build_usd.py")
    usd_args = [
        "--build-variant", "release",
        "--usd-imaging", "--python", "--tools", "--usdview",
        "--no-examples", "--no-tutorials", "--no-docs", "--no-tests",
        "--no-openimageio", "--no-alembic", "--no-draco",
        "--no-embree", "--no-prman", "--no-openvdb", "--no-ptex",
        USD_LOCAL_INSTALL_DIR,
    ]
    print(f"\n[usd] Building OpenUSD {USD_VERSION_TAG} -> {USD_LOCAL_INSTALL_DIR} "
          f"(first run only; this takes a while)\n")
    if OS == "Windows":
        vcvars = _find_vcvars()
        inner = subprocess.list2cmdline([build_python, build_usd, *usd_args])
        subprocess.check_call(["cmd", "/c", f'call "{vcvars}" && {inner}'],
                              cwd=PROJECT_ROOT)
    else:
        run([build_python, build_usd, *usd_args])

    _bundle_python_runtime(build_python)
    _fix_usd_tool_wrappers()

    # Advertise the freshly built install so the CMake configure that follows
    # links against it (CMakeLists.txt reads $HDRESTIR_USD_INSTALL first).
    os.environ[USD_INSTALL_ENV] = USD_LOCAL_INSTALL_DIR


# ── build ─────────────────────────────────────────────────────────

def _compiler_exists(command):
    if not command:
        return False
    exe = command[0] if isinstance(command, (list, tuple)) else command
    return bool(shutil.which(exe) or os.path.exists(exe))


def _compiler_version_text(command):
    try:
        result = subprocess.run([command, "--version"], capture_output=True, text=True)
    except OSError:
        return ""
    return result.stdout + result.stderr if result.returncode == 0 else ""


def _is_apple_clang(command):
    return "Apple clang" in _compiler_version_text(command)


def _clang_scan_deps_for(cxx_compiler):
    compiler_dir = os.path.dirname(os.path.abspath(cxx_compiler)) if os.path.isabs(cxx_compiler) else ""
    candidates = []
    if compiler_dir:
        candidates.append(os.path.join(compiler_dir, "clang-scan-deps"))
    path_scan_deps = shutil.which("clang-scan-deps")
    if path_scan_deps:
        candidates.append(path_scan_deps)

    for candidate in candidates:
        if candidate and os.path.exists(candidate):
            return candidate
    return None


def _c_compiler_for_cxx(cxx_compiler):
    base = os.path.basename(cxx_compiler)
    directory = os.path.dirname(cxx_compiler)
    c_base = base.replace("clang++", "clang", 1)
    if c_base == base:
        return None
    candidate = os.path.join(directory, c_base) if directory else c_base
    return candidate if _compiler_exists([candidate]) else None


def _brew_llvm_clangxx():
    """Resolve Homebrew's llvm keg via `brew --prefix llvm`, not a hardcoded path.

    Homebrew's llvm formula is keg-only (it conflicts with the system clang),
    so it's never on PATH after a plain `brew install llvm`; `brew --prefix`
    is the portable way to find it regardless of version or install root
    (/opt/homebrew vs /usr/local) — see external/rhi/ARCHITECTURE.md, which
    documents passing this compiler explicitly for exactly this reason.
    """
    if not shutil.which("brew"):
        return None
    try:
        result = subprocess.run(["brew", "--prefix", "llvm"], capture_output=True, text=True)
    except OSError:
        return None
    if result.returncode != 0:
        return None
    candidate = os.path.join(result.stdout.strip(), "bin", "clang++")
    return candidate if os.path.exists(candidate) else None


def _find_gpu_capable_clang_pair():
    """Return (cc, cxx, clang_scan_deps) for a portable Clang modules toolchain.

    Discovery is intentionally capability-based: respect explicit CC/CXX first,
    then search PATH for versioned and unversioned Clang names, then fall back
    to Homebrew's (keg-only, PATH-invisible) llvm via `brew --prefix`. Package-
    manager install prefixes differ across machines and operating systems, so
    hardcoded Cellar/opt paths do not belong here — `brew --prefix` resolves
    that portably instead.
    """
    cxx_candidates = []
    env_cxx = os.environ.get("CXX")
    if env_cxx:
        cxx_candidates.append(env_cxx)

    for major in range(20, 15, -1):
        cxx_candidates.append(f"clang++-{major}")
    cxx_candidates.append("clang++")

    brew_clangxx = _brew_llvm_clangxx()
    if brew_clangxx:
        cxx_candidates.append(brew_clangxx)

    seen = set()
    for cxx in cxx_candidates:
        cxx_path = shutil.which(cxx) or (cxx if os.path.exists(cxx) else None)
        if not cxx_path or cxx_path in seen:
            continue
        seen.add(cxx_path)
        if _is_apple_clang(cxx_path):
            continue

        scan_deps = _clang_scan_deps_for(cxx_path)
        if not scan_deps:
            continue

        env_cc = os.environ.get("CC")
        cc_path = None
        if env_cc:
            cc_path = shutil.which(env_cc) or (env_cc if os.path.exists(env_cc) else None)
        if not cc_path:
            cc_path = _c_compiler_for_cxx(cxx_path)
        if not cc_path:
            continue

        return cc_path, cxx_path, scan_deps

    return None


def _macos_sdkroot():
    """Resolve the macOS SDK path via xcrun, not a hardcoded path.

    external/rhi/ARCHITECTURE.md documents that SDKROOT must be exported on
    macOS so clang-scan-deps can find system headers (objc/runtime.h etc.)
    when compiling against a non-Apple Clang (e.g. Homebrew's llvm). Respects
    an already-exported SDKROOT; returns None on non-macOS or if xcrun can't
    resolve one, leaving the environment untouched.
    """
    if os.environ.get("SDKROOT"):
        return os.environ["SDKROOT"]
    if OS != "Darwin" or not shutil.which("xcrun"):
        return None
    try:
        result = subprocess.run(["xcrun", "--sdk", "macosx", "--show-sdk-path"],
                                capture_output=True, text=True)
    except OSError:
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def build(debug=False, gpu=True):
    setup_usd()
    _prepare_build_dir_for_cmake()

    build_type = "RelWithDebInfo" if debug else "Release"

    cmake_conf = [
        "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        # Run the python tests under THIS interpreter (the dev python USD's
        # bindings were built with), so usdrecord/pxr have a matching ABI.
        f"-DPython3_EXECUTABLE={sys.executable}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        # Always compile the runtime metrics (AccumulationPass variance logging):
        # perf_test.py parses these logs for float-precision convergence numbers,
        # so they must be present in Release builds too. Cost is negligible.
        "-DMETRICS_ENABLED=ON",
    ]
    if OS == "Darwin":
        cmake_conf.append("-DCMAKE_OSX_ARCHITECTURES=arm64")

    build_env = {}
    if gpu:
        # GPU-accelerated ReSTIR passes (external/rhi, LightRHI) use C++23
        # modules. Prefer any PATH/env-discoverable upstream Clang toolchain
        # that provides clang-scan-deps. Fail early when the environment does
        # not provide the required toolchain instead of silently changing build
        # features across machines.
        gpu_toolchain = _find_gpu_capable_clang_pair()
        if not gpu_toolchain:
            raise RuntimeError(
                "No upstream Clang toolchain with clang-scan-deps found. "
                "Set CC/CXX or PATH to a Clang installation that provides clang-scan-deps, "
                "or pass --no-gpu to build without GPU-accelerated passes."
            )
        clang, clangxx, clang_scan_deps = gpu_toolchain
        cmake_conf += [
            f"-DCMAKE_C_COMPILER={clang}",
            f"-DCMAKE_CXX_COMPILER={clangxx}",
            f"-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS={clang_scan_deps}",
        ]

        sdkroot = _macos_sdkroot()
        if sdkroot:
            build_env["SDKROOT"] = sdkroot
    else:
        cmake_conf.append("-DGPU_ENABLED=OFF")

    run(cmake_conf, env=build_env)
    run(["cmake", "--build", "build"], env=build_env)


# ── launch / capture ──────────────────────────────────────────────

def _make_session_wrapper(scene_path, settings_path):
    """Write a temporary USD file that sublayers scene + render settings.

    The scene is the stronger sublayer so its opinions win on any conflicting
    prims. The settings file adds /RenderSettings (with pipeline variants)
    which the scene normally doesn't define, so there is no conflict.
    The caller is responsible for deleting the returned path.
    """
    import tempfile
    scene_abs    = os.path.abspath(scene_path)
    settings_abs = os.path.abspath(settings_path)
    content = (
        "#usda 1.0\n"
        "(\n"
        "    subLayers = [\n"
        f"        @{scene_abs}@,\n"
        f"        @{settings_abs}@\n"
        "    ]\n"
        ")\n"
    )
    fd, path = tempfile.mkstemp(suffix=".usda", prefix="hdrestir_session_")
    with os.fdopen(fd, "w") as f:
        f.write(content)
    return path


def _usd_env():
    """Environment for running USD's Python tools (usdrecord/usdview) against the
    from-source USD: pxr on PYTHONPATH, and the USD shared libraries + the Restir
    plugin on the loader path. USD's own core+imaging plugins are found via its
    canonical default search, so only the Restir plugin dir needs adding to
    PXR_PLUGINPATH_NAME."""
    install = usd_install_dir()
    usd_lib = os.path.join(install, "lib")
    usd_bin = os.path.join(install, "bin")

    env_vars = {
        "PYTHONPATH": os.pathsep.join(filter(None, [
            os.path.join(usd_lib, "python"), os.environ.get("PYTHONPATH", "")])),
        "PXR_PLUGINPATH_NAME": os.path.join(BUILD_DIR, "resources"),
    }
    if OS == "Darwin":
        env_vars["DYLD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [
            BUILD_DIR, usd_lib, os.environ.get("DYLD_LIBRARY_PATH", "")]))
    elif OS == "Linux":
        env_vars["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [
            BUILD_DIR, usd_lib, os.environ.get("LD_LIBRARY_PATH", "")]))
    elif OS == "Windows":
        # This USD keeps its .dll's in lib/ (next to the import libs); TBB's .dll
        # is in bin/. Both go on PATH.
        env_vars["PATH"] = os.pathsep.join(filter(None, [
            BUILD_DIR, usd_lib, usd_bin, os.environ.get("PATH", "")]))
    return env_vars


def _usd_python():
    """The Python interpreter that matches the ABI USD was compiled against.

    Reads _Python3_EXECUTABLE from CMakeCache.txt (the interpreter CMake's
    find_package(Python3) resolved at configure time) so the pxr .so ABI
    always matches the runtime.  Falls back to sys.executable when no cache
    entry is present (e.g. before the first build)."""
    cache_file = os.path.join(BUILD_DIR, "CMakeCache.txt")
    if os.path.isfile(cache_file):
        with open(cache_file, encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.startswith("_Python3_EXECUTABLE:INTERNAL="):
                    exe = line.split("=", 1)[1].strip()
                    if exe and os.path.isfile(exe):
                        return exe
    return sys.executable


def _usd_tool_script(name):
    """Path to a USD Python tool (usdrecord/usdview), run with _usd_python().

    Prefer the real script from our from-source tree when present: USD's Windows
    install ships broken bin/ wrappers, so the source script is the reliable
    entry point. For an OpenUSD installed elsewhere (no source tree), fall back
    to the script shipped in the install's bin/."""
    src = os.path.join(USD_SRC_DIR, "pxr", "usdImaging", "bin", name, f"{name}.py")
    if os.path.isfile(src):
        return src
    bindir = os.path.join(usd_install_dir(), "bin")
    for candidate in (os.path.join(bindir, f"{name}.py"), os.path.join(bindir, name)):
        if os.path.isfile(candidate):
            return candidate
    raise RuntimeError(
        f"Could not locate the USD tool {name!r}. Looked in the from-source tree "
        f"({USD_SRC_DIR}) and {bindir}. Ensure OpenUSD's tools are installed.")


def _find_stage_camera(scene_path, env_vars):
    """Path of the first authored Camera prim in the composed stage, or None.

    usdview/usdrecord only auto-select a camera named after USD's hardcoded
    default ("main_cam"/"primary") — an asset that ships its own camera under
    any other name (common in production scenes) is silently ignored, and both
    tools fall back to a free camera that auto-frames the *entire* stage
    bounding box. For a scene built as an enclosed studio box (walls + a camera
    deliberately placed inside), that fallback frames the box from outside,
    where the opaque walls block everything — nothing renders. Finding the
    real camera and passing it explicitly (see launch()/capture()) fixes that
    without requiring the caller to know the prim path up front."""
    script = (
        "import sys\n"
        "from pxr import Usd, UsdGeom\n"
        "stage = Usd.Stage.Open(sys.argv[1])\n"
        "cams = [p.GetPath() for p in stage.Traverse() if p.IsA(UsdGeom.Camera)]\n"
        "print(str(cams[0]) if cams else '')\n"
    )
    result = subprocess.run([_usd_python(), "-c", script, scene_path],
                           capture_output=True, text=True, env={**os.environ, **env_vars})
    path = result.stdout.strip()
    return path or None


def _stage_has_lights(scene_path, env_vars):
    """True if the composed stage authors any UsdLux light prim.

    Scenes with no lights of their own (common in simple product-viz assets)
    rely entirely on usdrecord's default camera headlight for illumination —
    disabling that headlight unconditionally would render them black."""
    script = (
        "import sys\n"
        "from pxr import Usd, UsdLux\n"
        "stage = Usd.Stage.Open(sys.argv[1])\n"
        "has_light = any(p.HasAPI(UsdLux.LightAPI) for p in stage.Traverse())\n"
        "print('1' if has_light else '')\n"
    )
    result = subprocess.run([_usd_python(), "-c", script, scene_path],
                           capture_output=True, text=True, env={**os.environ, **env_vars})
    return bool(result.stdout.strip())


def _resolve_capture_output_path(output_path):
    if os.path.isabs(output_path):
        return output_path

    images_root = os.path.join(PROJECT_ROOT, "images")
    if output_path.startswith("images" + os.sep) or output_path == "images":
        return os.path.join(PROJECT_ROOT, output_path)

    return os.path.join(images_root, output_path)


# Render-setting keys are resolved against the token table in
# restir_render_settings.h, and the env-var name is derived from the TOKEN
# STRING with the exact same rule as MakeRenderSettingEnvironmentName in
# renderer.cpp. Deriving from the camelCase identifier instead (the old
# behaviour) silently produced dead env vars whenever the identifier did not
# match the token path (e.g. enableFireflyFilter vs restir:denoiser:fireflyFilter).

_RENDER_SETTINGS_HEADER = os.path.join(
    PROJECT_ROOT, "source", "renderer", "restir_render_settings.h")
_TOKEN_TUPLE_RE = re.compile(r'\(?\(\s*(\w+)[\s\\]*,[\s\\]*"([^"]+)"[\s\\]*\)\)?', re.DOTALL)


def _load_render_setting_tokens():
    """Return {cppIdentifier: tokenString} parsed from restir_render_settings.h."""
    with open(_RENDER_SETTINGS_HEADER, encoding="utf-8") as f:
        return dict(_TOKEN_TUPLE_RE.findall(f.read()))


def _resolve_render_setting_token(key, tokens):
    """Accept a cpp identifier ('risUseReservoir'), a token path
    ('ris:useReservoir') or a full token ('restir:ris:useReservoir') and
    return the full token string. Raises on unknown keys instead of
    silently exporting a dead env var."""
    if key in tokens:
        return tokens[key]
    token_values = set(tokens.values())
    if key in token_values:
        return key
    prefixed = f"restir:{key}"
    if prefixed in token_values:
        return prefixed
    known = sorted(tokens) + sorted(t[len("restir:"):] for t in token_values)
    raise ValueError(
        f"Unknown render setting {key!r}. Known keys (identifier or token "
        f"path): {', '.join(known)}")


def _render_setting_env_name(token):
    """Mirror MakeRenderSettingEnvironmentName in renderer.cpp exactly:
    drop the root namespace ('restir:'), then ':' -> '_' and camelCase
    boundaries get a '_', everything uppercased."""
    text = token.split(":", 1)[1] if ":" in token else token
    pieces = []
    for index, char in enumerate(text):
        if char == ":":
            pieces.append("_")
        else:
            if char.isupper() and index > 0 and text[index - 1] != ":":
                pieces.append("_")
            pieces.append(char.upper())
    return "HDRESTIR_" + "".join(pieces)


def _set_render_setting_override(env_vars, key, value, tokens=None):
    if tokens is None:
        tokens = _load_render_setting_tokens()
    token = _resolve_render_setting_token(key, tokens)
    env_vars[_render_setting_env_name(token)] = str(value)


def _apply_capture_render_setting_overrides(
    env_vars,
    *,
    render_settings,
):
    tokens = _load_render_setting_tokens()
    for key, value in render_settings:
        _set_render_setting_override(env_vars, key, value, tokens)


def launch(scene_path, *, render_settings=None, camera=None):
    env_vars = _usd_env()
    _apply_capture_render_setting_overrides(
        env_vars,
        render_settings=render_settings or [],
    )

    resolved_camera = camera or _find_stage_camera(scene_path, env_vars)

    settings_path = os.path.join(PROJECT_ROOT, "settings", "RenderSetup.usda")
    wrapper_path  = _make_session_wrapper(scene_path, settings_path)
    try:
        cmd = [_usd_python(), _usd_tool_script("usdview"), wrapper_path, "--renderer", "Restir"]
        if resolved_camera:
            cmd += ["--camera", resolved_camera]
        run(cmd, env=env_vars)
    finally:
        os.unlink(wrapper_path)


def capture(
    scene_path,
    output_path,
    *,
    render_settings=None,
    camera=None,
):
    env_vars = _usd_env()
    _apply_capture_render_setting_overrides(
        env_vars,
        render_settings=render_settings or [],
    )

    resolved_output_path = _resolve_capture_output_path(output_path)
    os.makedirs(os.path.dirname(resolved_output_path), exist_ok=True)

    resolved_camera = camera or _find_stage_camera(scene_path, env_vars)

    # GPU mode (default): usdrecord sets up an offscreen GL context via PySide
    # (built with usdview) and applies color correction, matching the reference
    # images. The Restir path tracer itself renders CPU-side.
    #
    # --disableCameraLight: usdrecord adds a camera headlight by default, which
    # isn't part of the authored scene — disabled so capture renders what the
    # scene actually contains. Only when the scene authors its own light(s):
    # some simple assets (no UsdLux prims at all) rely entirely on this
    # headlight for illumination and would render black without it.
    cmd = [_usd_python(), _usd_tool_script("usdrecord"), "--renderer", "Restir"]
    if _stage_has_lights(scene_path, env_vars):
        cmd += ["--disableCameraLight"]
    if resolved_camera:
        cmd += ["--camera", resolved_camera]
    cmd += [scene_path, resolved_output_path]
    run(cmd, env=env_vars)


def main():
    parser = argparse.ArgumentParser(description="HdRestir workflow helper")
    subparsers = parser.add_subparsers(dest="command")

    build_parser = subparsers.add_parser("build")
    build_parser.add_argument("--no-gpu", action="store_true",
                              help="Build without GPU-accelerated ReSTIR passes (skips the "
                                   "upstream-Clang/clang-scan-deps toolchain requirement).")
    debug_parser = subparsers.add_parser("debug")
    debug_parser.add_argument("--no-gpu", action="store_true",
                              help="Build without GPU-accelerated ReSTIR passes (skips the "
                                   "upstream-Clang/clang-scan-deps toolchain requirement).")

    launch_parser = subparsers.add_parser("launch")
    launch_parser.add_argument("scene", nargs="?", default=os.path.join(PROJECT_ROOT, "example_scenes", "scene.usda"))
    launch_parser.add_argument("--render-setting", action="append", default=[])
    launch_parser.add_argument("--camera", default=None,
                               help="Camera prim path or name. Defaults to the first Camera prim "
                                    "found in the stage, if any, since usdview otherwise only "
                                    "auto-selects a camera matching USD's hardcoded default name.")

    capture_parser = subparsers.add_parser("capture")
    capture_parser.add_argument("scene", nargs="?", default=os.path.join(PROJECT_ROOT, "example_scenes", "scene.usda"))
    capture_parser.add_argument("output", nargs="?", default="capture.png")
    capture_parser.add_argument("--render-setting", action="append", default=[])
    capture_parser.add_argument("--camera", default=None,
                                help="Camera prim path or name. Defaults to the first Camera prim "
                                     "found in the stage, if any.")

    args = parser.parse_args()
    command = args.command or "build"

    if command == "launch":
        extra_render_settings = []
        for item in args.render_setting:
            token_name, separator, value = item.partition("=")
            if not separator or not token_name:
                raise ValueError(f"Invalid --render-setting value: {item!r}. Expected token=value.")
            extra_render_settings.append((token_name, value))

        launch(args.scene, render_settings=extra_render_settings, camera=args.camera)
        return
    if command == "capture":
        extra_render_settings = []
        for item in args.render_setting:
            token_name, separator, value = item.partition("=")
            if not separator or not token_name:
                raise ValueError(f"Invalid --render-setting value: {item!r}. Expected token=value.")
            extra_render_settings.append((token_name, value))

        capture(
            args.scene,
            args.output,
            render_settings=extra_render_settings,
            camera=args.camera,
        )
        return
    if command == "debug":
        build(debug=True, gpu=not args.no_gpu)
        return

    build(gpu=not getattr(args, "no_gpu", False))


if __name__ == "__main__":
    main()
