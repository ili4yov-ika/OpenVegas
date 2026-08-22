#!/usr/bin/env python3
"""
Search for references to suite strings in the executable
"""

import os
import sys
import struct

def find_all_references(filename, search_string):
    """Find all references to a specific string in the executable"""
    print(f"Searching for references to string: {search_string!r}")
    
    with open(filename, 'rb') as f:
        data = f.read()
    
    # Find all occurrences of the string
    occurrences = []
    start = 0
    while True:
        pos = data.find(search_string.encode('ascii'), start)
        if pos == -1:
            break
        occurrences.append(pos)
        start = pos + 1
    
    print(f"Found {len(occurrences)} occurrences of the string")
    
    # Look for 64-bit pointers that point to these addresses
    references = []
    for i in range(len(data) - 8):
        ptr_bytes = data[i:i+8]
        ptr_value = struct.unpack('<Q', ptr_bytes)[0]
        for occ in occurrences:
            if ptr_value == occ:
                references.append((i, ptr_value))
    
    return references

def search_for_function_call_pattern(filename, string_offset):
    """Search for function call patterns that might reference the string"""
    print(f"Looking for code patterns around 0x{string_offset:08x}")
    
    with open(filename, 'rb') as f:
        data = f.read()
    
    # Look for patterns around the string address
    search_start = max(0, string_offset - 0x100)
    search_end = min(len(data), string_offset + 0x100)
    
    print(f"Searching in range 0x{search_start:08x} to 0x{search_end:08x}")
    
    # Look for typical function call patterns
    # This is a simplified search for common patterns
    
    # Look for x86-64 call instructions (0xE8) with relative offsets
    for i in range(search_start, search_end - 5):
        if data[i] == 0xE8:  # CALL instruction
            # Get the 32-bit relative offset
            offset_bytes = data[i+1:i+5]
            offset = struct.unpack('<I', offset_bytes)[0]
            # Calculate the target address
            target = i + 5 + offset
            if abs(target - string_offset) < 0x1000:  # Within reasonable range
                print(f"Found CALL at 0x{i:08x} -> 0x{target:08x} (offset 0x{offset:08x})")
    
    # Look for typical suite registration patterns
    # Look for mov instructions with the string address
    for i in range(search_start, search_end - 10):
        # Look for mov reg, imm64 pattern (0x48, 0xC7, 0xC0, ...)
        if data[i] == 0x48 and data[i+1] == 0xC7 and data[i+2] == 0xC0:
            # This is MOV reg, imm64 - register 0 = RAX
            imm64_bytes = data[i+3:i+7]
            imm64 = struct.unpack('<I', imm64_bytes)[0]
            if abs(imm64 - string_offset) < 0x1000:  # Within reasonable range
                print(f"Found MOV RAX, 0x{imm64:08x} at 0x{i:08x}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python search_references.py <vegas220.exe>")
        sys.exit(1)
    
    exe_file = sys.argv[1]
    if not os.path.exists(exe_file):
        print(f"File {exe_file} not found")
        sys.exit(1)
    
    print("=== Searching for Suite References ===")
    
    # Search for the suite strings
    interact_suite_addr = 0x00dadae8
    overlay_suite_addr = 0x00dadb50
    
    print("\n1. Searching for OfxHWndInteractSuite references:")
    search_for_function_call_pattern(exe_file, interact_suite_addr)
    
    print("\n2. Searching for OfxHWndOverlayInteractSuite references:")
    search_for_function_call_pattern(exe_file, overlay_suite_addr)
    
    print("\n=== Next Steps ===")
    print("Since we can't easily search the full binary in this environment,")
    print("the recommended approach is to:")
    print("1. Open vegas220.exe in Ghidra")
    print("2. Search for the strings using Ghidra's string search (not our script)")
    print("3. Find all cross-references to these strings")
    print("4. Identify the registration code that sets up the suite tables")
    print("5. The suite tables should be located at the addresses that the strings point to")