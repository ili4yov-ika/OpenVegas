#!/usr/bin/env python3
"""Generate the Media Generator preset table from what VEGAS itself ships.

Four generators are OFX plug-ins in the Vfx1 bundle, and their stock presets are plain XML
with typed values in

    OFX Video Plug-Ins/Vfx1.ofx.bundle/Contents/Presets/PresetPackage.xml

Values keep VEGAS's own parameter names and shapes — a colour is [r, g, b, a] in 0..1, a
choice, integer or boolean is a number — which is exactly what the importer reads out of a
project (vegOfxParamMap()), so a preset placed from the browser and the same preset opened
from a .veg carry identical data.

Two things the package does not hold, written out below with where they come from:

* "(Default)". VEGAS lists it first for Checkerboard and Test Pattern and saves it under
  that name, but it is the plug-in's own default rather than a packaged preset. Its values
  are the ones VEGAS wrote into SAMPLES/veg_projects/project_media-generators-without-
  titles-and-text.veg.
* Color Gradient and Credit Roll, which in VEGAS 22 are still legacy DirectX generators in
  sftrans1.dll. Their preset names are that DLL's string table, in its order. Their values
  are read out of the sample project, which holds every one of them, with the same decoding
  the importer does (src/io/VegLegacyGenerators.cpp), so the test that opens the project
  can hold the two to each other.

Run from the repo root:

    python tools/gen_generator_presets.py > src/video/GeneratorPresetData.cpp

With --schema <dir>, it writes the parameter schema of the four OFX generators instead,
from the JSON the [generator-ref] test dumps (each plug-in's own declarations):

    python tools/gen_generator_presets.py --schema <dir> > src/video/GeneratorParamSchema.cpp
"""

import json
import os
import struct
import sys
import xml.etree.ElementTree as ET
from collections import OrderedDict

XML = ("SAMPLES/VEGAS-PRO-22-PROGRAM-FILES/OFX Video Plug-Ins/Vfx1.ofx.bundle/"
       "Contents/Presets/PresetPackage.xml")

# Browser order, which is VEGAS's own: alphabetical by name.
GROUPS = [
    ("{Svfx:com.vegascreativesoftware:checkerboard}", "Checkerboard"),
    ("{8B5DEABC-53BD-49F6-9803-367478307A59}", "Color Gradient"),
    ("{02FD3B60-986B-11D4-9F6F-00485481BD31}", "Credit Roll"),
    ("{Svfx:com.vegascreativesoftware:noisetexture}", "Noise Texture"),
    ("{Svfx:com.vegascreativesoftware:solidcolor}", "Solid Color"),
    ("{Svfx:com.vegascreativesoftware:testpattern}", "Test Pattern"),
]

DEFAULTS = {
    "{Svfx:com.vegascreativesoftware:checkerboard}": OrderedDict([
        ("TileDimensionsWidth", 0.1), ("TileDimensionsHeight", 0.1), ("TileDimensionsSquare", 1.0),
        ("EdgeBlendingHorizontal", 0.0), ("EdgeBlendingVertical", 0.0),
        ("EdgeBlendingProportional", 1.0), ("Color1", [0.0, 0.0, 0.0, 1.0]),
        ("Color2", [1.0, 1.0, 1.0, 1.0]), ("GridOffsetX", 0.0), ("GridOffsetY", 0.0),
    ]),
    "{Svfx:com.vegascreativesoftware:testpattern}": OrderedDict([("TestPatternType", 0.0)]),
}

LEGACY = {
    "{8B5DEABC-53BD-49F6-9803-367478307A59}": [
        "Linear White to Black", "Linear Black to Transparent", "Linear Red, Green and Blue",
        "Elliptical White to Black", "Elliptical Transparent to Black", "Elliptical Ring",
        "Rectangular Transparent to Black", "Linear Red to Yellow",
        "Linear, Blue-Green Title Bar", "Skyscape", "Desert", "Soft-Green Backdrop",
        "Soft-Blue Backdrop", "Soft-Red Backdrop", "Sunburst", "Blue Movie Border", "Eclipse",
        "Fancy Wooden Board",
    ],
    "{02FD3B60-986B-11D4-9F6F-00485481BD31}": [
        "Plain Scrolling on Black", "Scrolling on Transparent", "Timed, Fade on Black",
        "Timed, Zoom In/Out on Blue", "Scrolling, Left Side, on Semi-Black",
        "Timed, Scroll Right, on Frosty White", "Timed, Wipe In/Out, on Black",
        "Backward Scrolling, Primary Colors",
    ],
}


SAMPLE = "SAMPLES/veg_projects/project_media-generators-without-titles-and-text.veg"


def legacy_blocks(path, clsid):
    """preset name -> parameter block bytes, for every pool entry of the legacy generator."""
    data = open(path, "rb").read()
    needle = ("META:\\Video Generator\\" + clsid).encode("utf-16-le")
    found = OrderedDict()
    at = data.find(needle)
    while at >= 0:
        strings = []
        i = at + len(needle)
        while len(strings) < 3 and i + 2 <= len(data):
            j = i
            text = []
            while j + 2 <= len(data):
                c = struct.unpack_from("<H", data, j)[0]
                if c == 0 or c < 0x20 or c > 0x2000:
                    break
                text.append(chr(c))
                j += 2
            if len(text) >= 3 and j + 2 <= len(data) and struct.unpack_from("<H", data, j)[0] == 0:
                strings.append(("".join(text), i))
            i = j + 2 if j > i else i + 2
        # the path's own tail " (n)[k]", then the display name, then the preset name
        if len(strings) >= 3:
            name, pos = strings[2]
            start = pos + 2 * (len(name) + 1)
            found.setdefault(name, data[start:start + 4096])
        at = data.find(needle, at + len(needle))
    return found


def bgra(b, off):
    bb, g, r, a = struct.unpack_from("<4f", b, off)
    return [round(r, 6), round(g, 6), round(bb, 6), round(a, 6)]


def color_gradient(b):
    n = struct.unpack_from("<i", b, 0)[0] - 1
    points = []
    for i in range(1, n + 1):
        points.append(OrderedDict([("X", struct.unpack_from("<d", b, 0xB0 + 8 * i)[0]),
                                   ("Y", struct.unpack_from("<d", b, 0x100 + 8 * i)[0]),
                                   ("Color", bgra(b, 0x10 + 16 * i))]))
    return OrderedDict([("GradientType", struct.unpack_from("<i", b, 4)[0]),
                        ("AspectAngle", struct.unpack_from("<d", b, 8)[0]),
                        ("Points", points)])


def credit_roll(b):
    def i32(o):
        return struct.unpack_from("<i", b, o)[0]

    def f64(o):
        return struct.unpack_from("<d", b, o)[0]

    fixed = i32(4)
    styles = []
    for k in range(5):
        s = 0x70 + 120 * k
        face = b[s:s + 64].decode("utf-16-le").split("\0", 1)[0]
        styles.append(OrderedDict([("Font", face), ("Size", i32(s + 64)), ("Bold", i32(s + 68)),
                                   ("Color", bgra(b, s + 72)), ("Align", i32(s + 88)),
                                   ("Tracking", f64(s + 96)), ("SpaceBelow", f64(s + 104)),
                                   ("Scale", f64(s + 112))]))
    nbytes = i32(fixed)
    text = b[fixed + 8:fixed + 8 + nbytes].decode("utf-16-le").rstrip("\0")
    return OrderedDict([("Effect", i32(8)), ("Left", f64(0x18)), ("Right", f64(0x20)),
                        ("Top", f64(0x28)), ("InEffect", i32(0x30)), ("OutEffect", i32(0x34)),
                        ("Display", i32(0x38)),
                        ("Connector", b[0x3C:0x40].decode("utf-16-le").split("\0", 1)[0]),
                        ("Background", bgra(b, 0x54)),
                        ("ScrollDirection", i32(0x64)), ("Styles", styles), ("Text", text)])


DECODERS = {
    "{8B5DEABC-53BD-49F6-9803-367478307A59}": color_gradient,
    "{02FD3B60-986B-11D4-9F6F-00485481BD31}": credit_roll,
}


def value_of(kind, text):
    text = (text or "").strip()
    if kind == "Boolean":
        return 1.0 if text.lower() in ("true", "1") else 0.0
    parts = text.split()
    if kind in ("RGB", "RGBA", "Double2D", "Double3D", "Integer2D", "Integer3D") or len(parts) > 1:
        return [float(p) for p in parts]
    return float(text)


def literal(v):
    if isinstance(v, dict):
        return "QVariantMap{%s}" % ", ".join("{%s, %s}" % (quoted(k), literal(x)) for k, x in v.items())
    if isinstance(v, list):
        return "QVariantList{%s}" % ", ".join(literal(x) for x in v)
    if isinstance(v, str):
        return quoted(v)
    if isinstance(v, int) and not isinstance(v, bool):
        return str(v)
    return number(v)


def number(x):
    s = repr(float(x))
    return s


def quoted(s):
    s = (s.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "\\r")
         .replace("\n", "\\n").replace("\t", "\\t"))
    return 'QStringLiteral("%s")' % s


def main():
    root = ET.parse(XML).getroot()
    packaged = OrderedDict()
    for preset in root.findall("OfxPreset"):
        if preset.get("context") != "Generator":
            continue
        plugin = "{Svfx:%s}" % preset.get("plugin")
        params = OrderedDict()
        for el in preset:
            if not el.tag.startswith("OfxParamType"):
                continue
            values = [v.text for v in el.iter("OfxParamValue")]
            if values:
                params[el.get("name")] = value_of(el.tag[len("OfxParamType"):], values[0])
        packaged.setdefault(plugin, []).append((preset.get("name"), params))

    out = []
    out.append("// Generated by tools/gen_generator_presets.py - do not edit by hand.")
    out.append("//")
    out.append("// Source: VEGAS's own PresetPackage.xml inside the Vfx1 OFX bundle for the OFX")
    out.append("// generators; for the legacy Color Gradient and Credit Roll, sftrans1.dll's string")
    out.append("// table for the names and the sample project's stored blocks for the values;")
    out.append("// \"(Default)\" as VEGAS saves it in a project.")
    out.append("")
    out.append('#include "video/GeneratorPresetData.h"')
    out.append("")
    out.append("namespace openvegas {")
    out.append("")
    out.append("const QVector<StockGeneratorGroup> &stockGeneratorPresets()")
    out.append("{")
    out.append("    static const QVector<StockGeneratorGroup> groups = {")
    total = 0
    for plugin, name in GROUPS:
        presets = []
        if plugin in DEFAULTS:
            presets.append(("(Default)", DEFAULTS[plugin]))
        presets += packaged.get(plugin, [])
        if plugin in LEGACY:
            blocks = legacy_blocks(SAMPLE, plugin)
            for n in LEGACY[plugin]:
                if n not in blocks:
                    sys.exit("%s: preset %r not in %s" % (name, n, SAMPLE))
                presets.append((n, DECODERS[plugin](blocks[n])))
        total += len(presets)
        out.append("        {%s, %s," % (quoted(plugin), quoted(name)))
        out.append("         {")
        for pname, params in presets:
            fields = ", ".join("{%s, %s}" % (quoted(k), literal(v)) for k, v in params.items())
            out.append("             {%s, QVariantMap{%s}}," % (quoted(pname), fields))
        out.append("         }},")
    out.append("    };")
    out.append("    return groups;")
    out.append("}")
    out.append("")
    out.append("const StockGeneratorGroup *stockGeneratorGroup(const QString &pluginId)")
    out.append("{")
    out.append("    for (const StockGeneratorGroup &g : stockGeneratorPresets()) {")
    out.append("        if (g.pluginId.compare(pluginId, Qt::CaseInsensitive) == 0) {")
    out.append("            return &g;")
    out.append("        }")
    out.append("    }")
    out.append("    return nullptr;")
    out.append("}")
    out.append("")
    out.append("const StockGeneratorPreset *stockGeneratorPreset(const QString &pluginId, const QString &presetName)")
    out.append("{")
    out.append("    const StockGeneratorGroup *group = stockGeneratorGroup(pluginId);")
    out.append("    if (!group) {")
    out.append("        return nullptr;")
    out.append("    }")
    out.append("    for (const StockGeneratorPreset &p : group->presets) {")
    out.append("        if (p.name.compare(presetName, Qt::CaseInsensitive) == 0) {")
    out.append("            return &p;")
    out.append("        }")
    out.append("    }")
    out.append("    return nullptr;")
    out.append("}")
    out.append("")
    out.append("} // namespace openvegas")
    print("\n".join(out))
    print("%d groups, %d presets" % (len(GROUPS), total), file=sys.stderr)


def schema(jsondir):
    out = []
    out.append("// Generated by tools/gen_generator_presets.py --schema - do not edit by hand.")
    out.append("//")
    out.append("// Source: what VEGAS's own Vfx1.ofx declares for each OFX generator in the Generator")
    out.append("// context, dumped through our host by the [generator-ref] test: names, types, labels,")
    out.append("// groups, defaults, ranges and choices, in the plug-in's own order.")
    out.append("")
    out.append('#include "video/GeneratorParamSchema.h"')
    out.append("")
    out.append("namespace openvegas {")
    out.append("")
    out.append("const QVector<GeneratorParamSchema> &generatorParamSchemas()")
    out.append("{")
    out.append("    static const QVector<GeneratorParamSchema> schemas = {")
    for plugin, name in GROUPS:
        path = os.path.join(jsondir, name + ".json")
        if not os.path.exists(path):
            continue
        doc = json.load(open(path, encoding="utf-8"))
        out.append("        {%s," % quoted(plugin))
        out.append("         {")
        for p in doc["params"]:
            kind = p["type"].replace("OfxParamType", "")
            if kind == "Page":
                continue
            out.append("             {%s, %s, %s, %s, QVariantList{%s}, %s, %s, QStringList{%s}}," % (
                quoted(p["name"]), quoted(kind), quoted(p["label"]), quoted(p["parent"]),
                ", ".join(literal(v) for v in p["default"]),
                number(p.get("displayMin", p.get("min", 0.0))),
                number(p.get("displayMax", p.get("max", 1.0))),
                ", ".join(quoted(c) for c in p["choices"])))
        out.append("         }},")
    out.append("    };")
    out.append("    return schemas;")
    out.append("}")
    out.append("")
    out.append("const QVector<GeneratorParamDef> *generatorParamSchema(const QString &pluginId)")
    out.append("{")
    out.append("    for (const GeneratorParamSchema &s : generatorParamSchemas()) {")
    out.append("        if (s.pluginId.compare(pluginId, Qt::CaseInsensitive) == 0) {")
    out.append("            return &s.params;")
    out.append("        }")
    out.append("    }")
    out.append("    return nullptr;")
    out.append("}")
    out.append("")
    out.append("} // namespace openvegas")
    print("\n".join(out))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--schema":
        schema(sys.argv[2])
    else:
        main()
