#!/usr/bin/env python3
"""Deprecated shim — use onboarding/provision.py instead."""

import sys
from pathlib import Path

_ONBOARDING = Path(__file__).resolve().parent
if str(_ONBOARDING) not in sys.path:
    sys.path.insert(0, str(_ONBOARDING))

from provision_lib import run_legacy_gen_euis_main

if __name__ == "__main__":
    raise SystemExit(run_legacy_gen_euis_main())
