def format_hex_string(hex_str):
    # Split the string into 2-character chunks, format with 0x, and lowercase them
    bytes_list = [f"0x{hex_str[i:i+2].lower()}" for i in range(0, len(hex_str), 2)]
    # Join together inside curly brackets
    return f"{{ {', '.join(bytes_list)} }}"

# Example Usage
print(format_hex_string("464C5869828E9E71"))
