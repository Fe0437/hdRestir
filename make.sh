#!/bin/bash
set -euo pipefail

PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ensure_just() {
    if command -v just >/dev/null 2>&1; then
        return
    fi

    echo "[INFO] just not found. Installing it..."

    if command -v brew >/dev/null 2>&1; then
        brew install just
    elif command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y just
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y just
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -Sy --noconfirm just
    elif command -v zypper >/dev/null 2>&1; then
        sudo zypper --non-interactive install just
    else
        echo "Error: could not find a supported package manager to install just." >&2
        exit 1
    fi

    if ! command -v just >/dev/null 2>&1; then
        echo "Error: just installation completed but the command is still unavailable." >&2
        exit 1
    fi
}

_python_has_dev() {
    "$1" -c "import sysconfig,os,sys; sys.exit(0 if os.path.isfile(os.path.join(sysconfig.get_path('include') or '','Python.h')) else 1)" 2>/dev/null
}

# OpenUSD's python bindings need a Python with C development files (Python.h).
# Find one, installing via the platform package manager if necessary, and hand
# its path to the recipes via the `python` variable.
PYEXE=""
ensure_python() {
    local cand exe
    for cand in python3.13 python3.12 python3.11 python3; do
        exe="$(command -v "$cand" 2>/dev/null)" || continue
        if _python_has_dev "$exe"; then PYEXE="$exe"; return; fi
    done

    echo "[INFO] Python with dev files not found. Installing it..."
    if command -v brew >/dev/null 2>&1; then
        brew install python@3.13
    elif command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update && sudo apt-get install -y python3 python3-dev
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y python3 python3-devel
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -Sy --noconfirm python
    else
        echo "Error: no supported package manager to install python." >&2; exit 1
    fi

    for cand in python3.13 python3.12 python3.11 python3; do
        exe="$(command -v "$cand" 2>/dev/null)" || continue
        if _python_has_dev "$exe"; then PYEXE="$exe"; return; fi
    done
    echo "Error: could not find or install a python with C development files." >&2
    exit 1
}


ensure_just
ensure_python

if [ "$#" -eq 0 ]; then
    exec just --justfile "$PROJ_ROOT/Justfile" --working-directory "$PROJ_ROOT" python="$PYEXE" build
fi

exec just --justfile "$PROJ_ROOT/Justfile" --working-directory "$PROJ_ROOT" python="$PYEXE" "$@"
