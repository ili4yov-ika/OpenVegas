#!/usr/bin/env python3
"""
Extract strings from vegas220.exe to find OFX suite references
"""

import os
import sys
import struct

def extract_strings(filename, min_length=4):
    """Extract strings from binary file"""
    with open(filename, 'rb') as f:
        data = f.read()
    
    strings = []
    i = 0
    while i < len(data):
        # Look for null-terminated ASCII strings
        if data[i] >= 32 and data[i] < 127:  # Printable ASCII
            start = i
            while i < len(data) and data[i] != 0:
                if data[i] < 32 or data[i] >= 127:  # Non-printable
                    break
                i += 1
            else:
                # Found a null terminator
                if i - start >= min_length:
                    try:
                        s = data[start:i].decode('ascii')
                        if s.strip():  # Non-empty string
                            strings.append((start, s))
                    except UnicodeDecodeError:
                        pass
        i += 1
    
    return strings

def find_suite_strings(filename):
    """Find OFX suite strings in the executable"""
    print(f"Searching for OFX suite strings in {filename}")
    strings = extract_strings(filename)
    
    suite_strings = []
    for offset, s in strings:
        if "OfxHWndInteractSuite" in s or "OfxHWndOverlayInteractSuite" in s:
            suite_strings.append((offset, s))
            print(f"Found: 0x{offset:08x} -> {s}")
    
    return suite_strings

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python extract_strings.py <vegas220.exe>")
        sys.exit(1)
    
    exe_file = sys.argv[1]
    if not os.path.exists(exe_file):
        print(f"File {exe_file} not found")
        sys.exit(1)
    
    suite_strings = find_suite_strings(exe_file)
    
    if suite_strings:
        print(f"\nFound {len(suite_strings)} suite string(s)")
        for offset, s in suite_strings:
            print(f"  Offset 0x{offset:08x}: {s}")
    else:
        print("No suite strings found")