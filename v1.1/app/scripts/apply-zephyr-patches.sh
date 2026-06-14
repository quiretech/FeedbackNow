#!/usr/bin/env bash
# Apply in-repo Zephyr patches before west build. Safe to run repeatedly.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_DIR="$(cd "${SCRIPT_DIR}/../patches" && pwd)"

if [[ -z "${ZEPHYR_BASE:-}" ]]; then
  echo "ZEPHYR_BASE is not set. Source your NCS/Zephyr environment first." >&2
  exit 1
fi

apply_one() {
  local patch_file="$1"
  local rel="$2"
  local target="${ZEPHYR_BASE}/${rel}"

  if [[ ! -f "${target}" ]]; then
    return 1
  fi
  if grep -q "Only join waits on mlme_confirm_sem" "${target}"; then
    echo "already patched: ${target}"
    return 0
  fi
  echo "applying $(basename "${patch_file}") -> ${target}"
  git -C "${ZEPHYR_BASE}" apply "${patch_file}"
  return 0
}

applied=0
if apply_one "${PATCH_DIR}/zephyr-lorawan-mlme-join-only-sem.patch" \
   "subsys/lorawan/loramac-node/lorawan.c"; then
  applied=1
elif apply_one "${PATCH_DIR}/zephyr-lorawan-mlme-join-only-sem-v3.0.patch" \
     "subsys/lorawan/lorawan.c"; then
  applied=1
fi

if [[ "${applied}" -eq 0 ]]; then
  echo "No lorawan.c found under ${ZEPHYR_BASE}/subsys/lorawan" >&2
  exit 1
fi

echo "Zephyr LoRaWAN patches applied."
