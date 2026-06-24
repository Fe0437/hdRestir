import argparse
import os, re, sys, subprocess, platform, shutil

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR    = os.path.join(PROJECT_ROOT, "build")
VENV_DIR     = os.path.join(BUILD_DIR, ".venv")
VCPKG_DIR    = os.path.join(PROJECT_ROOT, "vcpkg")
OS = platform.system()


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

def _venv_exe(name):
    if OS == "Windows":
        return os.path.join(VENV_DIR, "Scripts", name + ".exe")
    return os.path.join(VENV_DIR, "bin", name)

def _find_base_python():
    """Return path to a non-externally-managed Python suitable for venv creation."""
    python = _find_python_at_least(3, 11)
    if python:
        return python

    raise RuntimeError(
        "No suitable Python 3.11+ found. Install Python 3.11 or newer, or ensure the Python launcher is on PATH."
    )


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


# ── venv ──────────────────────────────────────────────────────────

def setup_venv():
    """Create build/.venv and install PySide6/PyOpenGL inside it.

    Returns (venv_python_path, venv_site_packages_path).
    Everything stays inside build/ — no global or user-level pip writes.
    """
    python_bin = _venv_exe("python3") if OS != "Windows" else _venv_exe("python")

    if not os.path.isfile(python_bin):
        base = _find_base_python()
        print(f"\n[venv] Creating project venv from {' '.join(base)} → {VENV_DIR}\n")
        subprocess.check_call([*base, "-m", "venv", VENV_DIR])

    pip_bin = _venv_exe("pip")
    try:
        subprocess.check_call([pip_bin, "install", "-q", "PySide6", "PyOpenGL", "numpy"])
    except (FileNotFoundError, subprocess.CalledProcessError):
        # Keep the venv pip launcher as the primary path, but fall back when
        # the launcher script is stale (e.g. broken shebang after env moves).
        subprocess.check_call([python_bin, "-m", "pip", "install", "-q", "PySide6", "PyOpenGL", "numpy"])

    # Resolve the site-packages directory from inside the venv
    r = subprocess.run(
        [python_bin, "-c",
         "import sysconfig; print(sysconfig.get_path('purelib'))"],
        capture_output=True, text=True, check=True,
    )
    return python_bin, r.stdout.strip()


# ── vcpkg ─────────────────────────────────────────────────────────

def setup_vcpkg():
    if not os.path.exists(VCPKG_DIR):
        run(["git", "clone", "https://github.com/microsoft/vcpkg.git"])
        bootstrap = "bootstrap-vcpkg.bat" if OS == "Windows" else "./bootstrap-vcpkg.sh"
        run([bootstrap], cwd=VCPKG_DIR)


# ── build ─────────────────────────────────────────────────────────

def build(debug=False):
    _venv_python, venv_site = setup_venv()
    setup_vcpkg()
    _prepare_build_dir_for_cmake()

    toolchain  = os.path.join(VCPKG_DIR, "scripts/buildsystems/vcpkg.cmake")
    triplet    = get_triplet()
    build_type = "RelWithDebInfo" if debug else "Release"

    cmake_conf = [
        "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
        f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
        f"-DVCPKG_TARGET_TRIPLET={triplet}",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        # Always compile the runtime metrics (AccumulationPass variance logging):
        # perf_test.py parses these logs for float-precision convergence numbers,
        # so they must be present in Release builds too. Cost is negligible.
        "-DMETRICS_ENABLED=ON",
    ]
    if OS == "Darwin":
        cmake_conf.append("-DCMAKE_OSX_ARCHITECTURES=arm64")

    # Expose venv's PySide6 to USD's cmake configure (FindPySide.cmake checks
    # `import PySide6` via execute_process, which inherits PYTHONPATH).
    existing = os.environ.get("PYTHONPATH", "")
    build_env = {
        "PYTHONPATH": os.pathsep.join(filter(None, [venv_site, existing]))
    }

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


def _get_launch_environment():
    venv_python, venv_site = setup_venv()

    triplet         = get_triplet()
    vcpkg_installed = os.path.join(BUILD_DIR, "vcpkg_installed", triplet)
    vcpkg_lib       = os.path.join(vcpkg_installed, "lib")
    vcpkg_bin       = os.path.join(vcpkg_installed, "bin")
    vcpkg_pypath    = os.path.join(vcpkg_installed, "lib", "python")

    # pxr modules (vcpkg) + PySide6/PyOpenGL (project venv)
    pythonpath = os.pathsep.join(filter(None, [
        vcpkg_pypath, venv_site, os.environ.get("PYTHONPATH", "")
    ]))

    env_vars = {
        "PYTHONPATH":          pythonpath,
        "PXR_PLUGINPATH_NAME": os.path.join(BUILD_DIR, "resources"),
        "TF_DEBUG":            "HD_RENDERER_PLUGIN",
    }

    if OS == "Darwin":
        env_vars["DYLD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [
            BUILD_DIR, vcpkg_lib, os.environ.get("DYLD_LIBRARY_PATH", "")
        ]))
    elif OS == "Linux":
        env_vars["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [
            BUILD_DIR, vcpkg_lib, os.environ.get("LD_LIBRARY_PATH", "")
        ]))
    elif OS == "Windows":
        env_vars["PATH"] = os.pathsep.join(filter(None, [
            BUILD_DIR, vcpkg_bin, os.environ.get("PATH", "")
        ]))

    return venv_python, vcpkg_bin, env_vars


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
    venv_python, vcpkg_bin, env_vars = _get_launch_environment()
    _apply_capture_render_setting_overrides(
        env_vars,
        render_settings=render_settings or [],
    )

    usdview       = os.path.join(vcpkg_bin, "usdview")
    settings_path = os.path.join(PROJECT_ROOT, "settings", "RenderSetup.usda")
    wrapper_path  = _make_session_wrapper(scene_path, settings_path)
    try:
        run([venv_python, usdview, wrapper_path, "--renderer", "Restir"], env=env_vars)
    finally:
        os.unlink(wrapper_path)


def capture(
    scene_path,
    output_path,
    *,
    render_settings=None,
):
    _venv_python, vcpkg_bin, env_vars = _get_launch_environment()
    _apply_capture_render_setting_overrides(
        env_vars,
        render_settings=render_settings or [],
    )

    resolved_output_path = _resolve_capture_output_path(output_path)
    os.makedirs(os.path.dirname(resolved_output_path), exist_ok=True)

    usdrecord = os.path.join(vcpkg_bin, "usdrecord")
    if not os.path.isfile(usdrecord):
        usdrecord = shutil.which("usdrecord")
    if not usdrecord:
        raise RuntimeError("usdrecord was not found in the build environment or on PATH.")

    run([usdrecord, scene_path, resolved_output_path, "--renderer", "Restir"], env=env_vars)


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
