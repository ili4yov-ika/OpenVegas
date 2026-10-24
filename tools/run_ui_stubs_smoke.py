#!/usr/bin/env python3
"""Run the UI integration check using a configured Windows MinGW/Ninja build.
Usage: python tools/run_ui_stubs_smoke.py --build build-msvc [--suite audio-fx]
The audio-fx suite requires registered Windows Shared Plug-Ins and native HWNDs.
Reuses application objects; QSettings are isolated by the test's temporary directory.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build-msvc")
    parser.add_argument("--suite", choices=("ui-stubs", "audio-fx"), default="ui-stubs")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = (root / args.build).resolve()
    if os.name != "nt":
        parser.error("This runner currently supports Windows MinGW/Ninja builds.")
    cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
    def cache_value(name):
        match = re.search(r"^" + re.escape(name) + r":[^=]*=(.+)$", cache, re.M)
        if not match:
            raise RuntimeError("Missing CMake cache value: " + name)
        return match[1].strip()
    compiler = Path(cache_value("CMAKE_CXX_COMPILER"))
    qt_bin = Path(cache_value("Qt6_DIR")).parents[2] / "bin"
    env = dict(os.environ)
    env["PATH"] = str(compiler.parent) + os.pathsep + str(qt_bin) + os.pathsep + env.get("PATH", "")
    env["QT_QPA_PLATFORM"] = "windows" if args.suite == "audio-fx" else "offscreen"
    ninja = cache_value("CMAKE_MAKE_PROGRAM")
    # Keep the response file when the changed application is linked.
    subprocess.run([ninja, "-d", "keeprsp", "OpenVegas"], cwd=build, env=env, check=True)
    commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    command = next(c for c in commands if c["file"].replace("\\", "/").endswith("/src/app/main.cpp"))
    stem = args.suite + "-smoke"
    source = "test_audio_fx_smoke.cpp" if args.suite == "audio-fx" else "test_ui_stubs_smoke.cpp"
    obj = build / (stem + ".cpp.obj")
    prefix = command["command"].split(" -o ", 1)[0]
    compile_command = prefix + ' -o "' + str(obj) + '" -c "' + str(root / "tests" / source) + '"'
    subprocess.run(compile_command, cwd=build, env=env, check=True)
    scanner_obj = build / "ui-stubs-smoke-scanner.cpp.obj"
    scanner_command = prefix + ' -o "' + str(scanner_obj) + '" -c "' + str(root / "tests/ui_smoke_plugin_scanner.cpp") + '"'
    subprocess.run(scanner_command, cwd=build, env=env, check=True)
    rsp = build / "CMakeFiles/OpenVegas.rsp"
    if not rsp.exists():
        # Ninja removes its response file after a normal link. Obtain the same inputs
        # without rebuilding every source, using its generated link rule.
        graph = (build / "build.ninja").read_text(encoding="utf-8")
        rule = re.search(r"^build OpenVegas\.exe: [^\n]+\n(?:  [^\n]*\n)*", graph, re.M)[0]
        inputs = rule.split("\n", 1)[0].split(": ", 1)[1].split(" ", 1)[1].split(" |", 1)[0]
        libraries = re.search(r"^  LINK_LIBRARIES = (.*)$", rule, re.M)[1]
        response = inputs + " " + libraries
    else:
        response = rsp.read_text(encoding="utf-8")
    response = response.replace("CMakeFiles/OpenVegas.dir/src/app/main.cpp.obj", '"' + obj.as_posix() + '"')
    response = response.replace("CMakeFiles\\OpenVegas.dir\\src\\app\\main.cpp.obj", '"' + obj.as_posix() + '"')
    for old in ("CMakeFiles/OpenVegas.dir/src/plugins/PluginScanner.cpp.obj",
                "CMakeFiles\\OpenVegas.dir\\src\\plugins\\PluginScanner.cpp.obj"):
        response = response.replace(old, '"' + scanner_obj.as_posix() + '"')
    if scanner_obj.as_posix() not in response:
        raise RuntimeError("Could not substitute the test OFX scanner")
    smoke_rsp = build / (stem + ".rsp")
    smoke_rsp.write_text(response, encoding="utf-8")
    # The empty test scanner prevents native OFX loading; the real scanner
    # remains covered by the separate media/video suites.
    exe = build / stem / "run/bin/tests" / (stem + ".exe")
    exe.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(compiler), "-g", "@" + str(smoke_rsp), "-o", str(exe)], cwd=build, env=env, check=True)
    subprocess.run([str(exe)], cwd=exe.parent, env=env, check=True, timeout=180)

if __name__ == "__main__":
    main()
