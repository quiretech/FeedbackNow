#!/usr/bin/env bash

# delete-specific-lorawan-devices.sh

set -euo pipefail

REGION="us-east-1"

DEVICE_IDS=(

)

echo "About to delete ${#DEVICE_IDS[@]} AWS IoT Core LoRaWAN devices."
echo

read -p "Type DELETE to continue: " CONFIRM

if [[ "$CONFIRM" != "DELETE" ]]; then
  echo "Aborted."
  exit 1
fi

for ID in "${DEVICE_IDS[@]}"; do
  echo "Deleting device: $ID"

  aws iotwireless delete-wireless-device \
    --id "$ID" \
    --region "$REGION"

  echo "Deleted: $ID"
done

echo
echo "Done."