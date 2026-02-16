import csv
from pathlib import Path

# Input CSV file
INPUT_FILE = "example_device_list.csv"
# Output folder for generated text files
OUTPUT_DIR = Path("lorawan_configs")
OUTPUT_DIR.mkdir(exist_ok=True)

def format_hex_string(hex_str, reverse=False):
    """Convert a hex string into a formatted C-style hex array."""
    # Remove any whitespace
    hex_str = hex_str.strip()
    # Split into bytes
    bytes_list = [hex_str[i:i+2] for i in range(0, len(hex_str), 2)]
    # Reverse if needed (LoRaWAN often uses LSB first)
    if reverse:
        bytes_list.reverse()
    # Format as hex
    formatted = ", ".join(f"0x{b.lower()}" for b in bytes_list)
    return "{ " + formatted + " }"

with open(INPUT_FILE, newline='', encoding='utf-8') as csvfile:
    reader = csv.DictReader(csvfile, delimiter=';')
    for row in reader:
        dev_eui = row["DevEui"]
        app_eui = row["AppEui"]
        app_key = row["AppKey"]
        name = row.get("Name", "device").strip()

        # Format in C-style defines
        content = f"""#define LORAWAN_DEV_EUI \\
    {format_hex_string(dev_eui)}

#define LORAWAN_JOIN_EUI \\
    {format_hex_string(app_eui)}

#define LORAWAN_APP_KEY \\
    {format_hex_string(app_key, reverse=False)}
"""

        # Save to text file
        output_file = OUTPUT_DIR / f"{name}.txt"
        with open(output_file, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"Generated: {output_file}")
