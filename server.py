from flask import Flask, request
import json
from grpc_queue_downlink import enqueue_device_queue_item
import datetime
import zoneinfo


app = Flask(__name__)


current_module = 1

@app.route('/chirpstack-webhook', methods=['POST'])
def chirpstack_webhook():
    global current_module
    event_type = request.args.get('event')
    data = request.json
    # print(f"################################################################")
    # print(f"\nReceived event: {event_type}")
    # print("Payload:")
    # print(json.dumps(data, indent=2))
    # print(f"################################################################\n")


    f_cnt_down = 0

    

    if event_type == "up":

        uplink_data = data.get('data')
        if uplink_data in ["/y4C/3MA", "/y4B/3MA", "/y4A/3MA"]:  # Button pressed
            utc_timestamp = data.get('time')
            est_time = datetime.datetime.fromisoformat(utc_timestamp).astimezone(zoneinfo.ZoneInfo("US/Eastern")).strftime("%Y-%m-%d %I:%M %p")
            f_port = data.get('fPort')
            dev_eui = data.get('deviceInfo', {}).get('devEui')  
            
            # Prepare payload with current module
            downlink_data = prepare_payload(est_time, module=str(current_module))
            
            enqueue_device_queue_item(downlink_data, f_port, dev_eui=dev_eui)
            
            # (cycle between 1, 2, 3)
            current_module = (current_module % 3) + 1

        # elif uplink_data == "AXVk/3MA":  # Periodic uplink data
        #     print("Periodic data")

    return '', 200


def prepare_payload(payload_content, f_cnt_down=0, module="1"):
    """
    Prepare payload for Milesight DS3604 display
    
    Format:
    fb (Content Update Channel)
    01 (Text Content Update Type) 
    0X (Template and Module - X is template/module number)
    XX (Content Size in hex)
    XXXX... (Content in hex)
    ff (Control Channel)
    3d (Screen Refresh Type)
    02 (Control Value)
    """


    # Content update parameters
    CONTENT_UPDATE_CHANNEL = "fb"
    TYPE = "01"  # Text content update
    TEMPLATE = "0"  
    MODULE = module  # Use the provided module number
    
    # Control parameters
    CONTROL_CHANNEL = "ff"
    CONTROL_TYPE = "3d"  # Screen refresh
    CONTROL_VALUE = "02"

    ###

    # custom_message = "Last cleaned at" 
    # CONTENT = custom_message.encode().hex()

    ###

    CONTENT = payload_content.encode().hex()
    content_size_dec = len(CONTENT) // 2  # Get size in bytes
    CONTENT_SIZE = format(content_size_dec, '02x')  # Convert to 2-digit hex

    # Build complete payload
    prep_payload = (
        CONTENT_UPDATE_CHANNEL +  # fb
        TYPE +                    # 01
        TEMPLATE +               # 0
        MODULE +                 # Module number (1, 2, or 3)
        CONTENT_SIZE +          # XX
        CONTENT +               # Content in hex
        CONTROL_CHANNEL +       # ff
        CONTROL_TYPE +          # 3d
        CONTROL_VALUE           # 02
    )

    # Convert hex string to bytes
    bytes_payload = bytes.fromhex(prep_payload)
    
    # print(f"Hex payload: {prep_payload}")
    # print(f"Bytes payload: {bytes_payload}")
    
    return bytes_payload




if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)