#!/bin/bash

# Send downlink to a list of AWS IoT Wireless devices

DEVICE_IDS=(
  "9a629ff3-3a54-495e-ad85-b34522537648"
  "c419bcfe-d6ef-415e-9b5c-a28adc0dbd49"
  "0be5609e-a7b8-4eab-a896-aa39ab9aa41d"
  "bf074b23-e3ad-40fd-a2ae-1d5d0f426634"
  "4c278054-6d22-4b2d-b5bd-3b5d19e4e4e1"
  "8edfa356-0d74-434e-9c5c-335e64a4a601"
  "a9c424d6-5d3f-46b7-94e4-17fb002f2343"
  "99ba5178-30c8-4db1-b240-cd27214f9476"
  "d15fd0a8-b388-4144-b69e-238160bd01e0"
  "7a1813f9-f698-4863-931c-2fe010242fca"
  "7eff028b-db87-48a4-95c6-962403952a4e"
  "78429c3d-e23c-42b3-8e8c-3c7c842bbf57"
  "f2de1fa0-726e-40e6-be3d-0bf0c09dcc9e"
  "c642b2ac-eb37-4b05-9a0d-83874e611edf"
  "bbba68a8-dcf3-4979-991c-89d22b778c64"
  "c76e0df7-6fcf-4d74-8b3e-8a6ef22074b1"
  "5ca67988-636f-471e-a965-683c80b33c9e"
  "d04fc17a-c185-4582-80fc-107c73eff0b7"
  "cd7e80fc-fbd8-4bd0-9fbe-2dfb4d44b94c"
  "72de09a7-6109-41b2-a042-09a71e060a94"
  "2bc7e700-791d-4b4d-b5ce-898ce88c69c2"
  "66daada4-d4d4-48bf-bb5e-259d291e8a5e"
  "0bff2c63-f7c5-47e4-b804-0131d8d4e608"
  "2401dd18-3fa4-4894-8003-77b567157634"
  "40c15a5c-f76e-4199-a93c-ccdd3652289c"
  "aacb5879-e8a2-4274-897d-01e65d1dca98"
  "4c96e485-817a-489c-abbd-021de070fec9"
  "fa27b428-8dca-4a03-a12a-9131f90dbe60"
  "7f25bd18-56a0-4af9-b802-5d99ad2bc6cb"
  "78c067fa-0965-4568-b227-b79610d8d67d"
  "c2fe5c7a-97dd-4580-a746-d8a2a0464ec4"
  "1cd8c3bf-cf04-41e5-9d8a-6e161f9dbce6"
  "f99bb4d5-b77b-4937-85ef-128808a0f31b"
  "94b88ddf-2a37-4f5b-8da6-ae84cf8ed6e4"
  "6f63433b-a1a2-46c4-9bc7-0fd6388839f9"
  "fa247053-dfba-411e-a61d-8f0918c0a944"
  "10e8effa-be03-46c2-afce-6d358e65bdd6"
  "01ad68ca-0cc8-4335-a6e1-9e65bae63a9d"
  "b599bc12-5768-41df-8e42-b89690d19760"
  "e3904e3f-09a8-4f32-9147-8102b5afccd5"
  "4858da1d-3383-4fb8-9e6a-717f5cf06d16"
  "15d522ac-4f19-43b1-86f9-02e995712441"
)

for DEVICE_ID in "${DEVICE_IDS[@]}"; do
  echo "Sending downlink to ${DEVICE_ID}..."

  aws iotwireless send-data-to-wireless-device \
    --id "${DEVICE_ID}" \
    --transmit-mode 0 \
    --payload-data "Bw==" \
    --wireless-metadata "LoRaWAN={FPort=1}" \
    --region us-east-1

  # Uncomment to throttle requests if desired
  sleep 2
done

echo "Done."
