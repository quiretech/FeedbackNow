# import json
# import datetime
# import zoneinfo
# import boto3
# import base64


#### --- continious downlink (if you get uplink)

# DOWNLINK_LAMBDA_NAME = 'senddownlinkpayload-SendDataToWirelessDeviceFuncti-WrSScv7K54Qi'

# current_module = 1

# lambda_client = boto3.client('lambda')

# def prepare_payload(payload_content, module="1"):
#     CONTENT_UPDATE_CHANNEL = "fb"
#     TYPE = "01"
#     TEMPLATE = "0"
#     MODULE = module

#     CONTROL_CHANNEL = "ff"
#     CONTROL_TYPE = "3d"
#     CONTROL_VALUE = "02"

#     CONTENT = payload_content.encode().hex()
#     content_size_dec = len(CONTENT) // 2
#     CONTENT_SIZE = format(content_size_dec, '02x')

#     prep_payload = (
#         CONTENT_UPDATE_CHANNEL +
#         TYPE +
#         TEMPLATE +
#         MODULE +
#         CONTENT_SIZE +
#         CONTENT +
#         CONTROL_CHANNEL +
#         CONTROL_TYPE +
#         CONTROL_VALUE
#     )

#     return bytes.fromhex(prep_payload)


# def lambda_handler(event, context):
#     global current_module

#     # Extract values from IoT Rule
#     timestamp_utc = event.get('messageTimestamp')
#     device_id = event.get('WirelessDeviceId')
#     payload_data = event.get('PayloadData')
#     f_port = event.get('fPort')

#     if not all([timestamp_utc, device_id, payload_data, f_port]):
#         return {
#             'statusCode': 400,
#             'body': json.dumps("Missing required fields in event.")
#         }

#     # Convert timestamp to EST
#     try:
#         est_time = datetime.datetime.fromisoformat(timestamp_utc.replace("Z", "+00:00")) \
#             .astimezone(zoneinfo.ZoneInfo("US/Eastern")) \
#             .strftime("%Y-%m-%d %I:%M %p")
#     except Exception as e:
#         return {
#             'statusCode': 500,
#             'body': json.dumps(f"Timestamp conversion failed: {str(e)}")
#         }

#     # Prepare payload
#     downlink_payload = prepare_payload(est_time, module=str(current_module))

#     # Encode to Base64 twice
#     payload_once = base64.b64encode(downlink_payload)
#     payload_twice = base64.b64encode(payload_once).decode('utf-8')

#     # Payload for sender Lambda
#     downlink_event = {
#         "WirelessDeviceId": device_id,
#         "FPort": f_port,
#         "PayloadData": payload_twice,
#         "TransmitMode": 1  # Use 1 for unconfirmed, 0 for confirmed (as per LoRaWAN)
#     }

#     # Invoke sender Lambda
#     try:
#         response = lambda_client.invoke(
#             FunctionName=DOWNLINK_LAMBDA_NAME,
#             InvocationType='RequestResponse',
#             Payload=json.dumps(downlink_event)
#         )
#         result_payload = json.load(response['Payload'])
#     except Exception as e:
#         return {
#             'statusCode': 500,
#             'body': json.dumps(f"Failed to invoke sender Lambda: {str(e)}")
#         }

#     # Cycle module
#     current_module = (current_module % 3) + 1

#     return {
#         'statusCode': 200,
#         'body': json.dumps({
#             "message": "Downlink request sent.",
#             "senderResult": result_payload
#         })
#     }






import json
import datetime
import zoneinfo
import boto3
import base64

DOWNLINK_LAMBDA_NAME = 'senddownlinkpayload-SendDataToWirelessDeviceFuncti-WrSScv7K54Qi'
current_module = 1
lambda_client = boto3.client('lambda')

# Button press payloads (as-is, plain text)
BUTTON_PRESS_PAYLOADS = ["/y4C/3MA", "/y4B/3MA", "/y4A/3MA"]

def prepare_payload(payload_content, module="1"):
    CONTENT_UPDATE_CHANNEL = "fb"
    TYPE = "01"
    TEMPLATE = "0"
    MODULE = module
    CONTROL_CHANNEL = "ff"
    CONTROL_TYPE = "3d"
    CONTROL_VALUE = "02"

    CONTENT = payload_content.encode().hex()
    content_size_dec = len(CONTENT) // 2
    CONTENT_SIZE = format(content_size_dec, '02x')

    prep_payload = (
        CONTENT_UPDATE_CHANNEL +
        TYPE +
        TEMPLATE +
        MODULE +
        CONTENT_SIZE +
        CONTENT +
        CONTROL_CHANNEL +
        CONTROL_TYPE +
        CONTROL_VALUE
    )

    return bytes.fromhex(prep_payload)

def lambda_handler(event, context):
    global current_module

    timestamp_utc = event.get('messageTimestamp')
    device_id = event.get('WirelessDeviceId')
    payload_data = event.get('PayloadData')  # plain text
    f_port = event.get('fPort')

    if not all([timestamp_utc, device_id, payload_data, f_port]):
        return {
            'statusCode': 400,
            'body': json.dumps("Missing required fields in event.")
        }

    # Only proceed if this is a button press
    if payload_data not in BUTTON_PRESS_PAYLOADS:
        return {
            'statusCode': 200,
            'body': json.dumps("Payload is not a button press. No downlink queued.")
        }

    # Convert timestamp to EST
    try:
        est_time = datetime.datetime.fromisoformat(timestamp_utc.replace("Z", "+00:00")) \
            .astimezone(zoneinfo.ZoneInfo("US/Eastern")) \
            .strftime("%Y-%m-%d %I:%M %p")
    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Timestamp conversion failed: {str(e)}")
        }

    # Prepare and encode payload
    downlink_payload = prepare_payload(est_time, module=str(current_module))
    payload_once = base64.b64encode(downlink_payload)
    payload_twice = base64.b64encode(payload_once).decode('utf-8')

    downlink_event = {
        "WirelessDeviceId": device_id,
        "FPort": f_port,
        "PayloadData": payload_twice,
        "TransmitMode": 1
    }

    try:
        response = lambda_client.invoke(
            FunctionName=DOWNLINK_LAMBDA_NAME,
            InvocationType='RequestResponse',
            Payload=json.dumps(downlink_event)
        )
        result_payload = json.load(response['Payload'])
    except Exception as e:
        return {
            'statusCode': 500,
            'body': json.dumps(f"Failed to invoke sender Lambda: {str(e)}")
        }

    current_module = (current_module % 3) + 1

    return {
        'statusCode': 200,
        'body': json.dumps({
            "message": "Button press detected. Downlink request sent.",
            "senderResult": result_payload
        })
    }
