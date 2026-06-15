#!/usr/bin/env bash

set -euo pipefail

REGION="us-east-1"
PAYLOAD="Bw=="
FPORT=1

declare -A DEVICES=(
  ["c642b2ac-eb37-4b05-9a0d-83874e611edf"]="SWRLD-UNIT-0339"
  ["9a629ff3-3a54-495e-ad85-b34522537648"]="SWRLD-UNIT-0326"
  ["d04fc17a-c185-4582-80fc-107c73eff0b7"]="SWRLD-UNIT-0343"
  ["f2de1fa0-726e-40e6-be3d-0bf0c09dcc9e"]="SWRLD-UNIT-0338"
  ["72de09a7-6109-41b2-a042-09a71e060a94"]="SWRLD-UNIT-0345"
  ["2bc7e700-791d-4b4d-b5ce-898ce88c69c2"]="SWRLD-UNIT-0346"
  ["c76e0df7-6fcf-4d74-8b3e-8a6ef22074b1"]="SWRLD-UNIT-0341"
)

echo "Starting AWS IoT Wireless downlink batch..."
echo "Total devices: ${#DEVICES[@]}"
echo

i=0

for DEVICE_ID in "${!DEVICES[@]}"; do
  NAME="${DEVICES[$DEVICE_ID]}"
  ((i++))

  echo "=================================================="
  echo "[$i/${#DEVICES[@]}] Sending downlink"
  echo "Device : $NAME"
  echo "ID     : $DEVICE_ID"
  echo "Time   : $(date -u)"
  echo "=================================================="

  aws iotwireless send-data-to-wireless-device \
    --id "$DEVICE_ID" \
    --transmit-mode 1 \
    --payload-data "$PAYLOAD" \
    --wireless-metadata "LoRaWAN={FPort=$FPORT}" \
    --region "$REGION"

  if [ $? -eq 0 ]; then
    echo "SUCCESS: $NAME"
  else
    echo "FAILED: $NAME"
  fi

  echo
done

echo "Batch complete."
