# Default registry (flexbox_euis/eui_registry.csv)
python onboarding/aws/batch_register_lorawan_devices.py \
  --region us-east-1 --device-profile-id <UUID> --service-profile-id <UUID> --destination-name <Name>

# EU868 registry (flexbox_euis/eui_registry_EU868.csv)
python onboarding/aws/batch_register_lorawan_devices.py --EU \
  --region us-east-1 --device-profile-id <UUID> --service-profile-id <UUID> --destination-name <Name>



1. US915
PS C:\Users\jatan\Desktop\githubrepo\fb_now\v1.1> 
python3 onboarding/aws/batch_register_lorawan_devices.py \
  --region us-east-1 \
  --device-profile-id 9ca0d988-eb68-485a-ab92-a210e134ab82 \
  --service-profile-id 9a8d999b-81d6-4071-8282-755b51d65d2e \
  --destination-name TestDeviceDestination \
  --last-only

2. EU868
PS C:\Users\jatan\Desktop\githubrepo\fb_now\v1.1> 
python3 onboarding/aws/batch_register_lorawan_devices.py \
  --EU \
  --region us-east-1 \
  --device-profile-id cf8adeae-ce9d-4a26-bcf2-c9805c2ef571 \
  --service-profile-id 9a8d999b-81d6-4071-8282-755b51d65d2e \
  --destination-name TestDeviceDestination \
  --last-only


# gen_euis.py: default = US915 + flexbox_euis/eui_registry.csv + LR62E overlay + prj US915
PS C:\Users\jatan\Desktop\githubrepo\fb_now\v1.1\onboarding> clear; python3 .\gen_euis.py; clear

# EU868 + flexbox_euis/eui_registry_EU868.csv + Seeed WIO overlay + prj EU868
PS C:\Users\jatan\Desktop\githubrepo\fb_now\v1.1\onboarding> clear; python3 .\gen_euis.py --EU; clear

# Build profile only (swap region/hardware in tree without generating EUIs)
python3 onboarding/gen_euis.py --sync-build-only --region us915
python3 onboarding/gen_euis.py --sync-build-only --region eu868



UNIT-ENG-TEST,464C58D039797CF4,9ADFAF670B2F429E,16B5007727F044FC4E1C0F70C8A48A0C
QUIRETECH_ENG_TEST_UNIT,6B1B0DE124A39277,1E8D358985805A36,090275A0301B62A0247BF97DD6EBC157

/* UNIT-ENG-TEST */
#define LORAWAN_DEV_EUI                                                        \
  { 0x46, 0x4c, 0x58, 0xd0, 0x39, 0x79, 0x7c, 0xf4 }

#define LORAWAN_JOIN_EUI                                                       \
  { 0x9a, 0xdf, 0xaf, 0x67, 0x0b, 0x2f, 0x42, 0x9e }

#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0x16, 0xb5, 0x00, 0x77, 0x27, 0xf0, 0x44, 0xfc,    \
        0x4e, 0x1c, 0x0f, 0x70, 0xc8, 0xa4, 0x8a, 0x0c                                                 \
  }

/* QUIRETECH_ENG_TEST_UNIT */
#define LORAWAN_DEV_EUI                                                        \
  { 0x6b, 0x1b, 0x0d, 0xe1, 0x24, 0xa3, 0x92, 0x77 }

#define LORAWAN_JOIN_EUI                                                       \
  { 0x1e, 0x8d, 0x35, 0x89, 0x85, 0x80, 0x5a, 0x36 }

#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0x09, 0x02, 0x75, 0xa0, 0x30, 0x1b, 0x62, 0xa0,    \
        0x24, 0x7b, 0xf9, 0x7d, 0xd6, 0xeb, 0xc1, 0x57                                                 \
  }

  /* ZZ-UNIT-HM-TEST-EU868 */
#define LORAWAN_DEV_EUI                                                        \
  { 0xad, 0xc6, 0x21, 0x90, 0x93, 0x9a, 0x71, 0x4e }

#define LORAWAN_JOIN_EUI                                                       \
  { 0x97, 0x76, 0x0f, 0x8b, 0x2d, 0xe7, 0x7c, 0x0e }

#define LORAWAN_APP_KEY                                                        \
  {                                                                            \
    0xa1, 0x49, 0x0a, 0x62, 0x07, 0xca, 0x06, 0xb0,    \
        0x89, 0xc4, 0x42, 0x37, 0xe9, 0xd8, 0xbf, 0x79                                                 \
  }



/////////////////////////////////////////////////////////////////////////////////////////
FBN PROD - EU UNITS AWS COMMAND TO CALL UPLOAD DEVICE



// FBN-PROD-EU

python3 onboarding/gen_euis.py --EU && \
(cd app && west flash --runner jlink) && \
python3 onboarding/aws/batch_register_lorawan_devices.py \
  --EU \
  --region us-east-1 \
  --device-profile-id 30a6011a-2d0a-471f-8d81-19d7a294afb8 \
  --service-profile-id 75c24d9c-3898-4b36-ad3b-eef0b63c7c3b \
  --destination-name capture \
  --last-only


// FBN-PROD-US

python3 onboarding/gen_euis.py && \
(cd app && west flash --runner jlink) && \
python3 onboarding/aws/batch_register_lorawan_devices.py \
  --region us-east-1 \
  --device-profile-id a7381c2f-3751-41fa-afb1-5aa0328f09f9 \
  --service-profile-id 75c24d9c-3898-4b36-ad3b-eef0b63c7c3b \
  --destination-name capture \
  --last-only




// FBN-ADMIN-US

python3 onboarding/gen_euis.py && \
(cd app && west flash --runner jlink) && \
python3 onboarding/aws/batch_register_lorawan_devices.py \
  --region us-east-1 \
  --device-profile-id a50e599b-d37d-41f1-a19b-aac66d93fdfa \
  --service-profile-id 71da2f46-8470-492a-a757-fcec760095cd \
  --destination-name destination \
  --last-only










// Quire-tech US
python3 onboarding/gen_euis.py && \
  python3 onboarding/aws/batch_register_lorawan_devices.py \
  --region us-east-1 \
  --device-profile-id 9ca0d988-eb68-485a-ab92-a210e134ab82 \
  --service-profile-id 9a8d999b-81d6-4071-8282-755b51d65d2e \
  --destination-name TestDeviceDestination \
  --last-only


// Quire-tech Eu

python3 onboarding/gen_euis.py --EU && \
python3 onboarding/aws/batch_register_lorawan_devices.py \
  --EU \
  --region us-east-1 \
  --device-profile-id cf8adeae-ce9d-4a26-bcf2-c9805c2ef571 \
  --service-profile-id 9a8d999b-81d6-4071-8282-755b51d65d2e \
  --destination-name TestDeviceDestination \
  --last-only


aws configure list-profiles

default
339683755525_iot-config-access
fbn-admin
quiretech
fbnow-admin
fbn-prod-eu



aws sts get-caller-identity
aws sso login --profile my-profile
export AWS_PROFILE=your-profile-name


lora_semtech_sx1262mb2das: sx1262@0 {
	compatible = "semtech,sx1262";
	reg = <0>;
	spi-max-frequency = <7000000>;
	label = "SX1262";
	reset-gpios = <&arduino_header 0 GPIO_ACTIVE_LOW>;
	busy-gpios  = <&gpio0 31 GPIO_ACTIVE_HIGH>;
	dio1-gpios  = <&gpio0 19 (GPIO_PULL_DOWN | GPIO_ACTIVE_HIGH)>;
	dio3-tcxo-voltage = <0x02>;
	tcxo-power-startup-delay-ms = <10>;
	dio2-tx-enable;
	status = "okay";
};