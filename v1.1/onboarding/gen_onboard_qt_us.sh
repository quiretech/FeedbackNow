python3 onboarding/gen_euis.py && \
  python3 onboarding/aws/batch_register_lorawan_devices.py \
  --region us-east-1 \
  --device-profile-id 9ca0d988-eb68-485a-ab92-a210e134ab82 \
  --service-profile-id 9a8d999b-81d6-4071-8282-755b51d65d2e \
  --destination-name TestDeviceDestination \
  --last-only
