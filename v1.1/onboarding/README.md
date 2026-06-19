# Onboarding & provisioning

Layout:

- **`flexbox_euis/`** — production EUI registries (`eui_registry.csv`, `eui_registry_EU868.csv`).
- **`flexbox_euis/demo_units/`** — test registries when using `provision.py --test` (does not touch master CSVs).
- **`targets.json`** — AWS IoT Core for LoRaWAN destination presets (`--target`).
- **`provision.py`** — primary CLI: keys, build profile, flash, AWS registration, validate.
- **`aws/`** — batch registration and collision checks (called by `provision.py`).
- **`chirpstack/`** — legacy ChirpStack gRPC utilities (optional).
- **`docs/`** — command cheat sheets and samples.

## Provision CLI (from repo root `v1.1`)

```bash
python onboarding/provision.py list

# New unit: keys + headers + registry row + prj.conf/overlay
python onboarding/provision.py keys --preset lr-us915
python onboarding/provision.py keys --preset seeed-eu868 --test   # demo_units/ only

# One-step: keys → validate → optional flash → AWS (last row when --target set)
python onboarding/provision.py all --preset seeed-us915 --target quiretech
python onboarding/provision.py all --preset seeed-eu868 --target fbn-main --flash
python onboarding/provision.py all --preset lr-us915 --target fbn-eu --dry-run

# Re-onboard a specific unit from the registry
python onboarding/provision.py aws --target fbn-main --preset seeed-eu868 --unit UNIT-0042

# Bulk AWS (requires --confirm)
python onboarding/provision.py aws --target fbn-eu --preset seeed-us915 --all-rows --confirm

python onboarding/provision.py validate
```

**Presets:** `lr-us915` (LR62E, US915) · `seeed-us915` · `seeed-eu868` (Seeed WIO, US915 or EU868)

**AWS accounts:** run `provision list` — `quiretech`, `fbn-main` (fbnow-admin), `fbn-eu` (fbn-prod-eu SSO), `fbn-admin`. Legacy names like `fbn-prod-us` still work as aliases.

**Decoupled:** `--preset` = hardware + LoRaWAN region + registry CSV. `--target` = which AWS account/destination. Any combination supported if that account has device profiles for that region in `targets.json`.

**Defaults:** With `--target`, AWS registers **only the last CSV row** unless you pass `--unit ASSET_ID` or `--all-rows --confirm`.

`gen_euis.py` remains as a deprecated shim; prefer `provision.py`.

## Python (must not be NCS toolchain)

**Do not use the NCS toolchain Python** (`C:\ncs\toolchains\...\bin\python`). It’s incomplete (e.g. no `_socket`) and breaks grpc/chirpstack-api. Use your **system Python** instead.

On Windows, force the system Python with the **`py` launcher**:

```powershell
# One-time: create venv with system Python and install deps
py -3 -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r onboarding\requirements.txt

# Run scripts (venv stays active)
python onboarding\chirpstack\chirpstack_onboard.py --device-profile-id <UUID>
```

If you don’t use a venv, install and run with the same launcher so you don’t pick up NCS’s `python`/`pip`:

```powershell
py -3 -m pip install -r onboarding\requirements.txt
py -3 onboarding\chirpstack\chirpstack_onboard.py --device-profile-id <UUID>
```

If `py -3` isn’t found, install Python from python.org and use the full path, e.g. `"C:\Users\<you>\AppData\Local\Programs\Python\Python312\python.exe" -m pip install ...`.

## Run from repo root (v1.1)

```bash
# Onboard devices from CSV (need --device-profile-id from ChirpStack UI)
python onboarding/chirpstack/chirpstack_onboard.py --device-profile-id <UUID>

# List then delete all devices in the app (optional --dry-run)
python onboarding/chirpstack/chirpstack_delete_devices_grpc.py [--dry-run]

# Queue a downlink to all devices in the app
python onboarding/chirpstack/chirpstack_downlink_all.py --payload 0102
python onboarding/chirpstack/chirpstack_downlink_all.py --payload "01 02 03" --f-port 11 --dry-run

# Provision new unit: EUIs/keys, eui_keys.h, onboarding_config.h unit id, registry CSV,
# plus app/prj.conf LoRaWAN region and board overlay (US915+LR62E vs EU868+Seeed WIO)
python onboarding/provision.py keys --preset lr-us915
python onboarding/provision.py keys --preset seeed-eu868 --test

# Switch build only (no new keys, no CSV): prj.conf + nrf52840dk_nrf52840.overlay
python onboarding/provision.py build --preset lr-us915
python onboarding/provision.py build --preset seeed-eu868

# Legacy shim (deprecated)
python onboarding/gen_euis.py --sync-build-only --region us915

# AWS IoT Core for LoRaWAN (prefer provision aws / provision all)
python onboarding/aws/batch_register_lorawan_devices.py \
  --region us-east-1 --device-profile-id <UUID> --service-profile-id <UUID> --destination-name <Name> \
  --last-only
python onboarding/aws/eui_collision_check.py
```

## Files

| Path | Purpose |
|------|---------|
| `api_key` | ChirpStack API token (paste and save; should be gitignored locally) |
| `flexbox_euis/eui_registry*.csv` | Production US915 / EU868 registries |
| `flexbox_euis/demo_units/` | Test registries (`provision.py --test`) |
| `targets.json` | AWS `--target` presets |
| `provision.py` | Primary provisioning CLI |
| `provision_lib.py` | Shared key/build/registry logic |
| `aws/batch_register_lorawan_devices.py` | Register devices from CSV with AWS IoT Core for LoRaWAN |
| `chirpstack/chirpstack_delete_devices_grpc.py` | Delete all devices in the app |
| `chirpstack/chirpstack_downlink_all.py` | Enqueue one downlink to every device in the app |
| `aws/upload_euis.sh` | Zip `flexbox_euis/` → `flexbox_euis.zip`, upload to S3, print presigned URL (same logic as `upload_flexbox_euis_s3.sh`) |
| `aws/upload_flexbox_euis_s3.sh` | Same as `upload_euis.sh` if the latter is not writable in your clone |
| `aws/eui_collision_check.py` | Validate registries (master + demo_units) for duplicate EUIs / keys |
| `gen_euis.py` | Deprecated shim → `provision.py` |
