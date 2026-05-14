#!/usr/bin/env bash
# Zip onboarding/flexbox_euis/ (canonical EUIs from gen_euis.py) and upload to S3.
# Use this if upload_euis.sh cannot be updated (e.g. root-owned) in your clone.
set -euo pipefail
ONBOARDING="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ONBOARDING"
ZIP_NAME="flexbox_euis.zip"
rm -f "$ZIP_NAME"
zip -r "$ZIP_NAME" flexbox_euis/
aws s3 cp "$ZIP_NAME" s3://flexbox-euis-701424605739-us-east-1-an/flexbox_euis.zip
aws s3 presign s3://flexbox-euis-701424605739-us-east-1-an/flexbox_euis.zip --expires-in 604800
