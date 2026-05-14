#!/bin/sh
# Zip onboarding/flexbox_euis/ (canonical EUIs from gen_euis.py) and upload to S3.
# POSIX sh (works with `sh ./upload_flexbox_euis_s3.sh`). Prefer: bash ./upload_flexbox_euis_s3.sh
set -eu
ONBOARDING="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ONBOARDING"
ZIP_NAME="flexbox_euis.zip"
rm -f "$ZIP_NAME"
zip -r "$ZIP_NAME" flexbox_euis/
aws s3 cp "$ZIP_NAME" s3://flexbox-euis-701424605739-us-east-1-an/flexbox_euis.zip
aws s3 presign s3://flexbox-euis-701424605739-us-east-1-an/flexbox_euis.zip --expires-in 604800
