# Setting up Ghidra + GhidraMCP for OFX Reverse Engineering

## Prerequisites

- Ghidra 12.1.2 installed at `C:\Program Files\Ghidra`
- Python 3.x installed
- opencode configured with MCP support

## Setup Steps

### 1. Install GhidraMCP Extension

1. Download `GhidraMCP-release-1-4.zip` from [LaurieWired/GhidraMCP](https://github.com/LaurieWired/GhidraMCP)
2. Inside the zip, extract `GhidraMCP-1-4.zip`
3. Edit `extension.properties` in the extracted folder:
   - Change `version=12.1.2`
   - Change `ghidraVersion=12.1.2`
4. Extract `GhidraMCP/` to:
   ```
   %APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\Extensions\GhidraMCP
   ```
5. Edit `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\tools\_code_browser.tcd`:
   - Add `<INCLUDE CLASS="com.lauriewired.GhidraMCPPlugin" />` inside the `<TOOL>` element

### 2. Prepare Ghidra Project

1. Create project directory:
   ```
   mkdir "%TEMP%\opencode\ghidramcp\proj"
   ```

2. Configure auto-recovery:
   - Add to `%APPDATA%\ghidra\ghidra_12.1.2_PUBLIC\preferences`:
     ```
     LastOpenedProject=C:/Users/Admin/AppData/Local/Temp/opencode/ghidramcp/proj/vegas_ofx
     ```

### 3. Open and Analyze vegas220.exe

1. Launch Ghidra:
   ```
   C:\Program Files\Ghidra\ghidraRun.bat
   ```

2. In Ghidra GUI:
   - Open `File → New Project → Non-Shared → vegas_ofx`
   - `File → Import File → D:\Devs\C++\OpenVegas\SAMPLES\VEGAS-PRO-22-PROGRAM-FILES\vegas220.exe`
   - Select `Auto Analyze` with processor `x86:LE:64:default`

### 4. Start GhidraMCP Server

1. In Ghidra GUI:
   - `Tools → GhidraMCP → Start HTTP Server`
   - Verify server starts on `127.0.0.1:8080`

### 5. Connect opencode to Ghidra

1. Run the bridge script:
   ```
   python tools/ghidra-mcp/bridge_mcp_ghidra.py --ghidra-server http://127.0.0.1:8080/
   ```

## Verification

1. Test server connectivity:
   ```
   curl "http://127.0.0.1:8080/methods?offset=0&limit=5"
   ```

2. Test decompilation:
   ```
   curl -X POST "http://127.0.0.1:8080/decompile" -d "FUN_180487cc0"
   ```

## Troubleshooting

- **Ghidra won't start**: Check that `ghidraRun.bat` exists at `C:\Program Files\Ghidra\ghidraRun.bat`
- **Plugin not found**: Ensure extension is in the correct directory and `tools\_code_browser.tcd` is edited
- **Project locked**: Close all Ghidra processes before changing preferences