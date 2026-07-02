#!/usr/bin/env python3
"""Ensure ImageMagick's `magick` CLI is available, installing it if needed.

`magick compare -metric RMSE` powers the HDR regression tests
(tests/test_hdr_capture_mse.py). The `just` recipes that run those tests depend
on this script, so the tool is installed on demand — cross-platform — without
the launchers (make.bat / make.sh) having to sniff the task name to decide
whether to install it.

The platform branching lives here, in Python, rather than in the Justfile:
`just` recipe bodies on Windows may run under either cmd.exe or sh (make.bat
prefers sh when git-bash is present), so OS-attributed recipe bodies would be
unreliable. A single Python entry point sidesteps that entirely.
"""

import shutil
import subprocess
import sys


def _installed():
    return shutil.which("magick") is not None


def _run(cmd):
    print(f"[INFO] {' '.join(cmd)}", flush=True)
    return subprocess.call(cmd) == 0


def _install():
    if sys.platform == "darwin":
        return bool(shutil.which("brew")) and _run(["brew", "install", "imagemagick"])

    if sys.platform.startswith("win"):
        return bool(shutil.which("winget")) and _run([
            "winget", "install", "-e", "--id", "ImageMagick.ImageMagick",
            "--accept-package-agreements", "--accept-source-agreements",
        ])

    # Linux / other *nix: try the common package managers in turn.
    if shutil.which("apt-get"):
        return _run(["sudo", "apt-get", "update"]) and _run(
            ["sudo", "apt-get", "install", "-y", "imagemagick"])
    if shutil.which("dnf"):
        return _run(["sudo", "dnf", "install", "-y", "ImageMagick"])
    if shutil.which("pacman"):
        return _run(["sudo", "pacman", "-Sy", "--noconfirm", "imagemagick"])
    if shutil.which("zypper"):
        return _run(["sudo", "zypper", "--non-interactive", "install", "imagemagick"])
    return False


def main():
    if _installed():
        return 0

    print("[INFO] ImageMagick not found. Installing it...", flush=True)
    if not _install():
        sys.stderr.write(
            "Error: could not install ImageMagick automatically "
            "(no supported package manager found). Install it manually.\n")
        return 1

    # A freshly installed `magick` may live in a directory not yet on this
    # session's PATH. It cannot be added to the sibling ctest process's
    # environment from here, so just warn: a second run (or a new terminal)
    # will pick it up.
    if not _installed():
        sys.stderr.write(
            "[WARN] ImageMagick was installed but `magick` is not visible in "
            "this session yet. Re-run the command (or open a new terminal).\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
