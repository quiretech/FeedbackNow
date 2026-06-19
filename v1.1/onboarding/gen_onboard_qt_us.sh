#!/usr/bin/env bash
# QuireTech US915: generate keys + register last row in AWS (test destination).
set -euo pipefail
cd "$(dirname "$0")/.."
python3 onboarding/provision.py all --preset seeed-us915 --target quiretech "$@"
