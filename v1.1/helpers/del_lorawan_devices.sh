#!/bin/bash

# AWS IoT Wireless Device Deletion Script
# Paste this into AWS CloudShell and run: bash delete-devices.sh

DEVICE_IDS=(
"7686253c-b408-43d5-823f-9065d7b9fe03"
"fd0c04d3-6fd7-42a3-9a0a-8810040fb9c0"
"c3c7fa27-b563-4244-996f-bf8555145aa8"
"0b88a40e-2b7c-46f8-9e51-83d0ed973e4d"
"dfc5864b-0a54-4b83-b1cc-8202dff9acc6"
"82c996f9-d563-4e8c-a064-d0814ba2d980"
"5911531e-85c4-44a3-a763-fd38e745659f"
"2d3b4285-46d1-41e4-9e64-c27c33ca659a"
"5bb7f08e-ed8f-48b4-890c-10693ded8c67"
"f1b472c9-d21f-4643-b2db-c1e72d132434"
)

echo "🗑️  AWS IoT Wireless Device Deletion Script"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Total devices to delete: ${#DEVICE_IDS[@]}"
echo ""
echo "⚠️  WARNING: This will permanently delete these devices from AWS IoT Wireless"
echo ""
read -p "Continue? (type 'yes' to confirm): " confirm

if [ "$confirm" != "yes" ]; then
  echo "Cancelled."
  exit 0
fi

echo ""
echo "Starting deletion..."
echo ""

DELETED=0
FAILED=0

for DEVICE_ID in "${DEVICE_IDS[@]}"; do
  echo -n "Deleting $DEVICE_ID ... "
  
  if aws iotwireless delete-wireless-device --id "$DEVICE_ID" 2>/dev/null; then
    echo "✓ Deleted"
    ((DELETED++))
  else
    echo "✗ Failed"
    ((FAILED++))
  fi
done

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Summary:"
echo "  ✓ Deleted: $DELETED"
echo "  ✗ Failed: $FAILED"
echo "  Total: ${#DEVICE_IDS[@]}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
