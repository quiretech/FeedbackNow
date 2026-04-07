import grpc
from chirpstack_api import api

CHIRPSTACK_API = "http://192.168.0.169:8080/"
API_TOKEN = ""

def enqueue_device_queue_item(data, f_port, confirmed=False, dev_eui="464c588a04bf5771"):

    channel = grpc.insecure_channel(CHIRPSTACK_API)

    client = api.DeviceServiceStub(channel)

    metadata = [("authorization", "Bearer %s" % API_TOKEN)]

    queue_item = api.DeviceQueueItem(
        dev_eui=dev_eui,  
        confirmed=confirmed,
        f_port=f_port,
        data=data,
        is_pending=False,
        is_encrypted=False
    )


    request = api.EnqueueDeviceQueueItemRequest(
        queue_item=queue_item
    )


    response = client.Enqueue(request, metadata=metadata)
    
    print("################################################################")
    print("Enqueued item for Milesight device:")
    print(f"  Queue Item ID: {response.id}")
    print(f"  Device EUI: 24e124601e171829")
    print(f"  Device Name: milesight-device")
    print(f"  Profile: milesight_ds3604_dev_profile")
    print("################################################################\n")

if __name__ == "__main__":


    # data = b'\xfb\x01\x42\x08\x53\x61\x6D\x20\x4B\x6F\x63\x68\xff\x3d\x02'
    data =  b'\xfb\x01\x00\x102025-05-22 16:10\xff=\x02'
    f_port = 85
    enqueue_device_queue_item(data, f_port)
