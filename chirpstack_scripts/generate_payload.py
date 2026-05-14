import sys
import base64

def build_payload(plaintext: str) -> bytes:
    header = b'\xfb'           # Start of message
    channel_type = b'\x01'     # Text content update
    channel_value = b'\x00'    # Template 1, module 1
    content_bytes = plaintext.encode('ascii')  # Convert string to ASCII bytes
    content_length = len(content_bytes).to_bytes(1, 'big')  # Length byte
    refresh = b'\xff\x3d\x02'  # Screen refresh sequence
    payload = header + channel_type + channel_value + content_length + content_bytes + refresh
    return payload

def main():
    if len(sys.argv) != 2:
        print("Usage: python generate_payload.py <plaintext>")
        sys.exit(1)

    cli_input = sys.argv[1]
    payload = build_payload(cli_input)
    b64_encoded = base64.b64encode(payload).decode('utf-8')
    print("Base64 Payload:", b64_encoded)

if __name__ == "__main__":
    main()
