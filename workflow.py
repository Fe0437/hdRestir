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
    the current environment, using a throwaway probe project. Mirrors exactly
    what the real configure will do, so we never build USD from source when
    CMake could have found one (e.g. via a PATH-derived or conda prefix)."""
    import tempfile
    if not shutil.which("cmake"):
        return False  # no cmake yet; assume we need our own USD
    probe = tempfile.mkdtemp(prefix="hdrestir_usdprobe_")
    try:
        with open(os.path.join(probe, "CMakeLists.txt"), "w") as f:
            f.write("cmake_minimum_required(VERSION 3.22)\n"
                    "project(usdprobe LANGUAGES NONE)\n"
                    "find_package(pxr CONFIG REQUIRED)\n")
        result = subprocess.run(
            ["cmake", "-S", probe, "-B", os.path.join(probe, "build")],
            capture_output=True, text=True)
        return result.returncode == 0
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
    if any(_looks_like_usd_prefix(c.strip().strip('"')) for c in candidates):
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
        if _looks_like_usd_prefix(prefix):
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

def _prepare_build_dir_for_cmake(build_dir=BUILD_DIR):
    """Clear stale CMake cache if it was generated from a different source tree."""
    cache_file = os.path.join(build_dir, "CMakeCache.txt")
    if not os.path.isfile(cache_file):
        return

    expected = os.path.realpath(PROJECT_ROOT)
    cached_source = None
    with open(cache_file, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
                cached_source = line.split("=", 1)[1].strip()
                break

    if not cached_source:
        return

    if os.path.realpath(cached_source) != expected:
        print(
            "\n[cmake] Build cache points to another source tree. "
            "Removing stale cache from build/.\n"
        )
        for path in (cache_file, os.path.join(build_dir, "CMakeFiles")):
            if os.path.isdir(path):
                shutil.rmtree(path)
            elif os.path.isfile(path):
                os.remove(path)


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
    subprocess.check_call([build_python, "-m", "pip", "install", "-q",
                           "jinja2", "PyOpenGL", "PySide6"],
                          env={**os.environ, "PIP_USER": "0"})

    build_usd = os.path.join(USD_SRC_DIR, "build_scripts", "build_usd.py")
    usd_args = [
        "--build-variant", "release",
        "--usd-imaging", "--python", "--tools", "--usdview",
        "--no-examples", "--no-tutorials", "--no-docs", "--no-tests",
        "--no-materialx", "--no-openimageio", "--no-alembic", "--no-draco",
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

def build(debug=False):
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
    if OS == "Windows":
        # On Windows the project compiles with clang (targeting the MSVC ABI),
        # not the default cl.exe; select it explicitly so a fresh configure is
        # deterministic.
        cmake_conf += ["-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"]
    if OS == "Darwin":
        cmake_conf.append("-DCMAKE_OSX_ARCHITECTURES=arm64")

    run(cmake_conf)
    run(["cmake", "--build", "build"])


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


def launch(scene_path, *, render_settings=None):
    env_vars = _usd_env()
    _apply_capture_render_setting_overrides(
        env_vars,
        render_settings=render_settings or [],
    )

    settings_path = os.path.join(PROJECT_ROOT, "settings", "RenderSetup.usda")
    wrapper_path  = _make_session_wrapper(scene_path, settings_path)
    try:
        run([_usd_python(), _usd_tool_script("usdview"), wrapper_path,
             "--renderer", "Restir"], env=env_vars)
    finally:
        os.unlink(wrapper_path)


def capture(
    scene_path,
    output_path,
    *,
    render_settings=None,
):
    env_vars = _usd_env()
    _apply_capture_render_setting_overrides(
        env_vars,
        render_settings=render_settings or [],
    )

    resolved_output_path = _resolve_capture_output_path(output_path)
    os.makedirs(os.path.dirname(resolved_output_path), exist_ok=True)

    # GPU mode (default): usdrecord sets up an offscreen GL context via PySide
    # (built with usdview) and applies color correction, matching the reference
    # images. The Restir path tracer itself renders CPU-side.
    run([_usd_python(), _usd_tool_script("usdrecord"),
         "--renderer", "Restir", scene_path, resolved_output_path], env=env_vars)


def main():
    parser = argparse.ArgumentParser(description="HdRestir workflow helper")
    subparsers = parser.add_subparsers(dest="command")

    subparsers.add_parser("build")
    subparsers.add_parser("debug")

    launch_parser = subparsers.add_parser("launch")
    launch_parser.add_argument("scene", nargs="?", default=os.path.join(PROJECT_ROOT, "example_scenes", "scene.usda"))
    launch_parser.add_argument("--render-setting", action="append", default=[])

    capture_parser = subparsers.add_parser("capture")
    capture_parser.add_argument("scene", nargs="?", default=os.path.join(PROJECT_ROOT, "example_scenes", "scene.usda"))
    capture_parser.add_argument("output", nargs="?", default="capture.png")
    capture_parser.add_argument("--render-setting", action="append", default=[])

    args = parser.parse_args()
    command = args.command or "build"

    if command == "launch":
        extra_render_settings = []
        for item in args.render_setting:
            token_name, separator, value = item.partition("=")
            if not separator or not token_name:
                raise ValueError(f"Invalid --render-setting value: {item!r}. Expected token=value.")
            extra_render_settings.append((token_name, value))

        launch(args.scene, render_settings=extra_render_settings)
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
        )
        return
    if command == "debug":
        build(debug=True)
        return

    build()


if __name__ == "__main__":
    main()
