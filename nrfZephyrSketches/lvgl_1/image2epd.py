from PIL import Image
import sys

# Config: EPD resolution
WIDTH = 400
HEIGHT = 300

def quantize_color(r, g, b):
    """
    Decide pixel color:
    - Black: dark pixels
    - Red: strong red pixels
    - White: everything else
    """
    # Perceived brightness
    brightness = 0.299*r + 0.587*g + 0.114*b

    # Black: very dark pixels
    if brightness < 60:
        return 'black'
    
    # Red: strong red, significantly more than G and B
    # Red must be >160, and at least 1.5x green and blue
    if r > 160 and r > 1.5*g and r > 1.5*b:
        return 'red'

    # Everything else -> white
    return 'white'

def bmp_to_epd_arrays(input_file, output_file):
    img = Image.open(input_file).convert("RGB")
    img = img.resize((WIDTH, HEIGHT))
    pixels = img.load()

    total_bytes = WIDTH * HEIGHT // 8
    bw = bytearray(total_bytes)
    red = bytearray(total_bytes)

    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = pixels[x, y]
            color = quantize_color(r, g, b)
            byte_index = (x + y * WIDTH) // 8
            bit_index = 7 - (x % 8)
            if color == 'black':
                bw[byte_index] |= (1 << bit_index)
            elif color == 'red':
                red[byte_index] |= (1 << bit_index)
            # White stays 0

    # Write header file
    with open(output_file, 'w') as f:
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"const uint8_t bw_bitmap[{total_bytes}] = {{\n")
        for i, val in enumerate(bw):
            if i % 16 == 0:
                f.write("\n ")
            f.write(f"0x{val:02X},")
        f.write("\n};\n\n")

        f.write(f"const uint8_t red_bitmap[{total_bytes}] = {{\n")
        for i, val in enumerate(red):
            if i % 16 == 0:
                f.write("\n ")
            f.write(f"0x{val:02X},")
        f.write("\n};\n")

    print(f"Header file '{output_file}' generated with {total_bytes} bytes per array.")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python image2epd.py input_image.png output_image.h")
    else:
        bmp_to_epd_arrays(sys.argv[1], sys.argv[2])
