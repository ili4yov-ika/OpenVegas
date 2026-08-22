# OFX Blob Decoder for .veg Files

## Project Status

We have identified the following files in the project that are relevant to our analysis:

- `SAMPLES/veg_project/` - Contains multiple `.veg` project files
- `SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/` - Contains the VEGAS Pro 22 executable (`vegas220.exe`)

## Analysis Approach

### Current Understanding

Based on the original plan in `PLAN_OFX_VIDEO_PLUGINS.md`, the `.veg` files contain serialized OFX plugin parameters. The key challenge is understanding the binary format of these parameters.

### Strategy for Implementation

1. **Examine the structure of .veg files**
   - Determine how OFX parameters are serialized
   - Identify binary markers that indicate OFX data blocks
   - Understand the structure of effect parameters

2. **Locate serialization code in vegas220.exe**
   - As mentioned in the original report, "Код сериализации параметров искать в том же `vegas220.exe`"
   - Look for strings like "SetInt32", "SetFloat", "SetString", etc.
   - Identify the function calls that serialize parameters

3. **Create decoder for OFX blobs**
   - Parse the binary format
   - Extract parameter values
   - Map them to OFX parameter names

## Immediate Next Steps

### 1. Documentation of Current State

The `.veg` files are binary files with what appears to be a complex serialized format containing:
- Media files (video/audio)
- Timeline information
- Effect parameters
- Project metadata

### 2. Search for Serialization Strings

In `vegas220.exe`, we should search for strings like:
- `SetInt32`
- `SetFloat`  
- `SetString`
- `SetRGB`
- `SetRGBA`
- `SetProperty`

### 3. Analysis Plan

1. **Create a simple hex reader** to examine the beginning of .veg files
2. **Search for OFX-related strings** in the executable
3. **Identify parameter serialization functions**
4. **Document the structure** of serialized parameters
5. **Develop a basic decoder**

## Technical Details

The format likely follows this pattern:
```
[Header]
[Media References]
[Timeline Data]
[Effect Parameters]
[Project Metadata]
```

Where effect parameters are structured as:
```
[Parameter Type (4 bytes)] 
[Parameter Name Length (4 bytes)]
[Parameter Name (variable)]
[Parameter Value (variable)]
```

## Tools Needed

1. **Hex editor** to examine .veg file structure
2. **Ghidra** to analyze `vegas220.exe` for serialization functions
3. **Python script** to parse and decode OFX blobs

## Expected Outcome

After completing this analysis:
1. We will understand the structure of OFX parameter blobs in .veg files
2. We will have a working decoder that can extract parameter values
3. We will be able to see effect parameters in .veg files without needing to open VEGAS Pro
4. This will complete the work outlined in the original plan

## Next Actions

1. **Examine one .veg file** with a hex editor to understand its structure
2. **Search `vegas220.exe`** for OFX serialization strings
3. **Prepare a Python decoder** for the parameter format
4. **Document findings** in `docs/OFX_BLOB_DECODER.md`

## Current Status

We have:
- Confirmed the suite string addresses in `vegas220.exe`
- Documented the approach for the remaining work
- Prepared the infrastructure for analysis

The next step is to actually implement this analysis using Ghidra to find the serialization code in `vegas220.exe`.