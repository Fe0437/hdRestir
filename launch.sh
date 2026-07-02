#!/bin/bash
# Thin shim around make.sh so all dependency / path handling lives in one place.
# Runs the Justfile's `launch` recipe (usdview).
set -euo pipefail
exec "$(dirname "${BASH_SOURCE[0]}")/make.sh" launch "${1:-example_scenes/scene.usda}"
