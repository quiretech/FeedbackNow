# AWS batch device registration (from eui_registry.csv)

Registers all devices in **onboarding/eui_registry.csv** with AWS IoT Core for LoRaWAN. The CSV columns are: `asset_id`, `dev_eui`, `join_eui`, `app_key` (same as ChirpStack onboarding).

**Lab/experimental use.** Review and adjust before production.

## Prerequisites

- AWS CLI configured (credentials with `iotwireless:CreateWirelessDevice`).
- A **Destination** already created in AWS IoT Wireless.
- Device Profile ID and Service Profile ID from your AWS account.

## 1. Get profile and destination IDs

```shell
aws iotwireless list-device-profiles
aws iotwireless list-service-profiles
aws iotwireless list-destinations
```

## 2. Install dependencies

```shell
pip install -r requirements.txt
# or: py -3 -m pip install -r requirements.txt
```

## 3. Run (from repo root v1.1)

**Default input:** `onboarding/eui_registry.csv`

```shell
# Dry run (no API calls)
python onboarding/aws_onboarding/batchregistration/batch_register_lorawan_devices.py \
  --region us-east-1 \
  --device-profile-id <DeviceProfileUUID> \
  --service-profile-id <ServiceProfileUUID> \
  --destination-name <DestinationName> \
  --dryrun

# Actually register all devices
python onboarding/aws_onboarding/batchregistration/batch_register_lorawan_devices.py \
  --region us-east-1 \
  --device-profile-id <DeviceProfileUUID> \
  --service-profile-id <ServiceProfileUUID> \
  --destination-name <DestinationName>
```

Optional: pass a different CSV as the first argument instead of using the default.

## Arguments

| Argument | Required | Description |
|----------|----------|--------------|
| `inputfilename` | No | CSV path (default: `onboarding/eui_registry.csv`) |
| `--region`, `-r` | Yes | AWS region (e.g. `us-east-1`) |
| `--device-profile-id` | Yes | Device Profile UUID |
| `--service-profile-id` | Yes | Service Profile UUID |
| `--destination-name` | Yes | Destination name (must already exist) |
| `--dryrun`, `-d` | No | Log only; do not call AWS API |
| `--verbose`, `-v` | No | More logging |
