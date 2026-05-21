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

ensure_just

if [ "$#" -eq 0 ]; then
    exec just --justfile "$PROJ_ROOT/Justfile" --working-directory "$PROJ_ROOT" build
fi

exec just --justfile "$PROJ_ROOT/Justfile" --working-directory "$PROJ_ROOT" "$@"
