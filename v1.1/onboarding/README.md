# Onboarding & ChirpStack scripts

Layout:

- **`flexbox_euis/`** — canonical EUI registries (`eui_registry.csv`, `eui_registry_EU868.csv`) written by `gen_euis.py`; `flexbox_euis/archive/` holds older CSV snapshots.
- **`chirpstack/`** — ChirpStack gRPC utilities.
- **`aws/`** — AWS IoT Core for LoRaWAN batch registration and collision checks.
- **`docs/`** — command cheat sheets and samples.

- **API key:** Paste your ChirpStack API token in `api_key` (one line, no quotes). Scripts read it from here so you can run without env vars or compile flags.
- **Application:** Default app is **InternalTesting** (application id `1101c2be-d036-44bd-bfa5-2da441213bd0`, tenant FBNOW_LNS_US915). Override with `--application-id` if needed.

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
python onboarding/gen_euis.py
python onboarding/gen_euis.py --region eu868   # same registry family as --EU
python onboarding/gen_euis.py --EU

# Switch build only (no new keys, no CSV): prj.conf + nrf52840dk_nrf52840.overlay
python onboarding/gen_euis.py --sync-build-only --region us915
python onboarding/gen_euis.py --sync-build-only --region eu868

# AWS IoT Core for LoRaWAN (see docs/script_call.md for full examples)
python onboarding/aws/batch_register_lorawan_devices.py \
  --region us-east-1 --device-profile-id <UUID> --service-profile-id <UUID> --destination-name <Name>
python onboarding/aws/eui_collision_check.py
```

## Files

| Path | Purpose |
|------|---------|
| `api_key` | ChirpStack API token (paste and save; should be gitignored locally) |
| `flexbox_euis/eui_registry*.csv` | US915 / EU868 registries (`gen_euis.py`). Columns include `hw_profile` (`FLEXBOX_PLUS` / `FLEXBOX` from `DEVICE_HW_VARIANT` in onboarding_config.h), optional `name_prefix`, `tag_client`, `decal_type`. |
| `aws/batch_register_lorawan_devices.py` | Register devices from CSV with AWS IoT Core for LoRaWAN |
| `chirpstack/chirpstack_delete_devices_grpc.py` | Delete all devices in the app |
| `chirpstack/chirpstack_downlink_all.py` | Enqueue one downlink to every device in the app |
| `aws/upload_euis.sh` | Zip `flexbox_euis/` → `flexbox_euis.zip`, upload to S3, print presigned URL (same logic as `upload_flexbox_euis_s3.sh`) |
| `aws/upload_flexbox_euis_s3.sh` | Same as `upload_euis.sh` if the latter is not writable in your clone |
| `aws/eui_collision_check.py` | Validate registries for duplicate EUIs / keys |
| `gen_euis.py` | Provision credentials (`eui_keys.h`, `onboarding_config.h`), append CSV, sync region in `prj.conf` + board overlay |
