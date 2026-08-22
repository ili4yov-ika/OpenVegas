#!/usr/bin/env python3
"""
Analyze OFX suite structures in vegas220.exe
"""

import os
import sys
import struct

def read_bytes(filename, offset, length):
    """Read bytes from file at offset"""
    with open(filename, 'rb') as f:
        f.seek(offset)
        return f.read(length)

def analyze_suite_table(filename, offset, num_entries=10):
    """Analyze a suite table structure"""
    print(f"\nAnalyzing suite table at 0x{offset:08x}")
    
    # Read the structure - assuming each entry is 8 bytes (pointer) for now
    # OFX suites are typically arrays of function pointers
    table_data = read_bytes(filename, offset, num_entries * 8)
    
    entries = []
    for i in range(num_entries):
        entry_offset = offset + i * 8
        if len(table_data) >= (i + 1) * 8:
            # Read 8-byte pointer (64-bit)
            ptr_bytes = table_data[i*8:(i+1)*8]
            ptr_value = struct.unpack('<Q', ptr_bytes)[0]  # little-endian
            entries.append((entry_offset, ptr_value))
            print(f"  +0x{i*8:04x}: 0x{ptr_value:016x}")
    
    return entries

def find_references(filename, string_offset):
    """Find references to an address in the executable"""
    print(f"\nSearching for references to 0x{string_offset:08x}")
    
    # Simple approach: look for 64-bit pointers that match the string address
    data = read_bytes(filename, 0, os.path.getsize(filename))
    string_addr = string_offset
    
    # Look for the address as a 64-bit value in the file
    references = []
    for i in range(len(data) - 8):
        ptr_bytes = data[i:i+8]
        ptr_value = struct.unpack('<Q', ptr_bytes)[0]
        if ptr_value == string_addr:
            references.append(i)
    
    print(f"Found {len(references)} references:")
    for ref in references[:10]:  # Show first 10
        print(f"  0x{ref:08x}")
    
    if len(references) > 10:
        print(f"  ... and {len(references) - 10} more")
    
    return references

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python analyze_suites.py <vegas220.exe>")
        sys.exit(1)
    
    exe_file = sys.argv[1]
    if not os.path.exists(exe_file):
        print(f"File {exe_file} not found")
        sys.exit(1)
    
    # Known addresses from the report
    interact_suite_addr = 0x00dadae8
    overlay_suite_addr = 0x00dadb50
    
    print("=== OFX Suite Analysis ===")
    
    # Analyze HWndInteractSuite
    print("\n1. Analyzing OfxHWndInteractSuite:")
    interact_entries = analyze_suite_table(exe_file, interact_suite_addr, 10)
    
    # Analyze HWndOverlayInteractSuite  
    print("\n2. Analyzing OfxHWndOverlayInteractSuite:")
    overlay_entries = analyze_suite_table(exe_file, overlay_suite_addr, 10)
    
    # Find references to these addresses
    print("\n3. Finding references:")
    interact_refs = find_references(exe_file, interact_suite_addr)
    overlay_refs = find_references(exe_file, overlay_suite_addr)
    
    print("\n=== Analysis Complete ===")
    print("Next steps:")
    print("- Examine the functions pointed to by these addresses in Ghidra")
    print("- Look for registration code that sets up these suites")
    print("- The +0x08 slot should be identified by analyzing these function pointers")