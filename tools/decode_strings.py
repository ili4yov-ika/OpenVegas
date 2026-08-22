#!/usr/bin/env python3
"""
Decode the actual string bytes to understand what we found
"""

import os
import sys
import struct

def read_bytes(filename, offset, length):
    """Read bytes from file at offset"""
    with open(filename, 'rb') as f:
        f.seek(offset)
        return f.read(length)

def decode_string_bytes(offset, data):
    """Decode bytes as string to see what we found"""
    print(f"Bytes at 0x{offset:08x}: {data.hex()}")
    
    # Try to interpret as ASCII
    try:
        ascii_str = data.decode('ascii')
        print(f"ASCII: {ascii_str!r}")
    except UnicodeDecodeError:
        print("Not valid ASCII")
    
    # Try to interpret as little-endian 64-bit values
    if len(data) >= 8:
        value = struct.unpack('<Q', data[:8])[0]
        print(f"64-bit LE value: 0x{value:016x}")
        # Try to decode as ASCII bytes
        ascii_bytes = value.to_bytes(8, 'little')
        try:
            ascii_str = ascii_bytes.decode('ascii')
            print(f"ASCII from 64-bit: {ascii_str!r}")
        except UnicodeDecodeError:
            pass

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python decode_strings.py <vegas220.exe>")
        sys.exit(1)
    
    exe_file = sys.argv[1]
    if not os.path.exists(exe_file):
        print(f"File {exe_file} not found")
        sys.exit(1)
    
    # Check the string addresses we found
    interact_suite_addr = 0x00dadae8
    overlay_suite_addr = 0x00dadb50
    
    print("=== Decoding String Addresses ===")
    
    print("\n1. OfxHWndInteractSuite (0x00dadae8):")
    data = read_bytes(exe_file, interact_suite_addr, 32)
    decode_string_bytes(interact_suite_addr, data)
    
    print("\n2. OfxHWndOverlayInteractSuite (0x00dadb50):")
    data = read_bytes(exe_file, overlay_suite_addr, 32)
    decode_string_bytes(overlay_suite_addr, data)
    
    print("\n=== Understanding the Structure ===")
    print("The strings we found are:")
    print("  OfxHWndInteractSuite")
    print("  OfxHWndOverlayInteractSuite")
    print("")
    print("These are likely located in the .rdata section of the executable.")
    print("The actual suite tables are probably at different offsets,")
    print("containing function pointers.")
    print("")
    print("Next step: We need to find the code that references these strings")
    print("to identify where the suites are registered and how they're used.")