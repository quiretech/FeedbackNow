# Onboarding & ChirpStack scripts

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
python onboarding\chirpstack_onboard.py --device-profile-id <UUID>
```

If you don’t use a venv, install and run with the same launcher so you don’t pick up NCS’s `python`/`pip`:

```powershell
py -3 -m pip install -r onboarding\requirements.txt
py -3 onboarding\chirpstack_onboard.py --device-profile-id <UUID>
```

If `py -3` isn’t found, install Python from python.org and use the full path, e.g. `"C:\Users\<you>\AppData\Local\Programs\Python\Python312\python.exe" -m pip install ...`.

## Run from repo root (v1.1)

```bash
# Onboard devices from CSV (need --device-profile-id from ChirpStack UI)
python onboarding/chirpstack_onboard.py --device-profile-id <UUID>

# List then delete all devices in the app (optional --dry-run)
python onboarding/chirpstack_delete_devices_grpc.py [--dry-run]

# Queue a downlink to all devices in the app
python onboarding/chirpstack_downlink_all.py --payload 0102
python onboarding/chirpstack_downlink_all.py --payload "01 02 03" --f-port 11 --dry-run

# Provision new unit: EUIs/keys, eui_keys.h, sys_config.h unit id, registry CSV,
# plus app/prj.conf LoRaWAN region and board overlay (US915+LR62E vs EU868+Seeed WIO)
python onboarding/gen_euis.py
python onboarding/gen_euis.py --region eu868   # same registry family as --EU
python onboarding/gen_euis.py --EU

# Switch build only (no new keys, no CSV): prj.conf + nrf52840dk_nrf52840.overlay
python onboarding/gen_euis.py --sync-build-only --region us915
python onboarding/gen_euis.py --sync-build-only --region eu868
```

## Files

| File | Purpose |
|------|--------|
| `api_key` | ChirpStack API token (paste and save; not in VCS if you add to .gitignore) |
| `eui_registry.csv` | US915 registry (asset_id, dev_eui, join_eui, app_key); default for `gen_euis.py` |
| `eui_registry_EU868.csv` | EU868 registry; used with `gen_euis.py --EU` or `--region eu868` |
| `chirpstack_onboard.py` | Create/update devices and keys from CSV |
| `chirpstack_delete_devices_grpc.py` | Delete all devices in the app |
| `chirpstack_downlink_all.py` | Enqueue one downlink to every device in the app |
| `gen_euis.py` | Provision credentials (`eui_keys.h`, `sys_config.h`), append CSV, sync region in `prj.conf` + board overlay |
