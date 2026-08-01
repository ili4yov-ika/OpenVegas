/**
 * Event Properties dialog (Vegas Pro dark UI).
 * Video Event / Audio Event + Media + General.
 * Open: event context → Properties…, or VegasEventProperties.open(eventEl).
 */
(function () {
  const WIN_ID = "event-properties";
  let currentEvent = null;
  let snapshot = null;

  function ak(text, letter) {
    const i = text.toLowerCase().indexOf(letter.toLowerCase());
    if (i < 0) return text;
    return text.slice(0, i) + "<u>" + text.charAt(i) + "</u>" + text.slice(i + 1);
  }

  function esc(s) {
    return String(s || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function isAudio(ev) {
    return !!ev?.classList?.contains("event--audio");
  }

  function eventLabel(ev) {
    const fallback = isAudio(ev) ? "sample_for_project_audio" : "sample_for_project_video";
    return (
      ev?.querySelector?.(".event__titlebar")?.childNodes?.[0]?.textContent?.trim() ||
      ev?.querySelector?.(".event__titlebar")?.textContent?.trim()?.replace(/\s*Nested\s*$/, "").trim() ||
      ev?.querySelector?.(".event__label")?.textContent?.trim() ||
      fallback
    );
  }

  function mediaPath(name, kind) {
    if (kind === "audio") {
      const file = /\.(wav|mp3|flac|ogg|aif)$/i.test(name) ? name : name + ".wav";
      return "D:\\Devs\\C++\\OpenVegas\\SAMPLES\\assets\\" + file;
    }
    const file = /\.mp4$/i.test(name) ? name : name + ".mp4";
    return "D:\\Devs\\C++\\OpenVegas\\SAMPLES\\assets\\" + file;
  }

  function opt(list, selected) {
    return list
      .map((v) => "<option" + (v === selected ? " selected" : "") + ">" + esc(v) + "</option>")
      .join("");
  }

  function spin(value, name) {
    return (
      '<span class="ep-spin">' +
      '<input type="text" data-ep="' +
      name +
      '" value="' +
      esc(value) +
      '" />' +
      '<span class="ep-spin__btns">' +
      '<button type="button" data-ep-spin="up" tabindex="-1" aria-label="Increase">▴</button>' +
      '<button type="button" data-ep-spin="down" tabindex="-1" aria-label="Decrease">▾</button>' +
      "</span></span>"
    );
  }

  function check(name, label, letter, checked) {
    return (
      '<label class="ep-check">' +
      '<input type="checkbox" data-ep="' +
      name +
      '"' +
      (checked ? " checked" : "") +
      " />" +
      "<span>" +
      ak(label, letter) +
      "</span></label>"
    );
  }

  function radio(group, value, label, letter, checked) {
    return (
      '<label class="ep-radio">' +
      '<input type="radio" name="' +
      group +
      '" data-ep-radio="' +
      group +
      '" value="' +
      value +
      '"' +
      (checked ? " checked" : "") +
      " />" +
      "<span>" +
      (letter ? ak(label, letter) : label) +
      "</span></label>"
    );
  }

  function section(title) {
    return (
      '<div class="ep-section"><span class="ep-section__title">' +
      title +
      '</span><span class="ep-section__line"></span></div>'
    );
  }

  function tabAudioEvent(state) {
    const gainOn = !!(state.normalize || state.autoNormalize);
    return (
      '<div class="ep-panel is-active" data-ep-panel="event">' +
      '<div class="ep-row ep-row--take">' +
      "<label>" +
      ak("Active take name:", "A") +
      "</label>" +
      '<input type="text" class="ep-input" data-ep="takeName" value="' +
      esc(state.takeName) +
      '" />' +
      "</div>" +
      section("Switches") +
      '<div class="ep-switches">' +
      check("mute", "Mute", "M", state.mute) +
      check("lock", "Lock", "L", state.lock) +
      check("loop", "Loop", "o", state.loop) +
      check("invertPhase", "Invert phase", "I", state.invertPhase) +
      check("normalize", "Normalize", "N", state.normalize) +
      check("autoNormalize", "Auto Normalize", "u", state.autoNormalize) +
      '<div class="ep-row ep-row--gain">' +
      "<label>" +
      ak("Gain:", "G") +
      "</label>" +
      '<span class="ep-gain-val' +
      (gainOn ? "" : " is-disabled") +
      '" data-ep-gain-val>' +
      esc(state.gain) +
      "</span>" +
      '<button type="button" class="ep-btn" data-ep-act="recalc"' +
      (gainOn ? "" : " disabled") +
      ">" +
      ak("Recalculate", "R") +
      "</button>" +
      "</div>" +
      "</div>" +
      section("Time stretch / pitch shift") +
      '<div class="ep-row ep-row--method">' +
      "<label>" +
      ak("Method:", "e") +
      "</label>" +
      '<select class="ep-select" data-ep="stretchMethod">' +
      opt(["None", "élastique", "Classic"], state.stretchMethod || "Classic") +
      "</select>" +
      "</div>" +
      "</div>"
    );
  }

  function tabVideoEvent(state) {
    return (
      '<div class="ep-panel is-active" data-ep-panel="event">' +
      '<div class="ep-row ep-row--take">' +
      "<label>" +
      ak("Active take name:", "A") +
      "</label>" +
      '<input type="text" class="ep-input" data-ep="takeName" value="' +
      esc(state.takeName) +
      '" />' +
      "</div>" +
      section("Switches") +
      '<div class="ep-switches">' +
      check("mute", "Mute", "M", state.mute) +
      check("lock", "Lock", "L", state.lock) +
      check("maintainAspect", "Maintain aspect ratio", "a", state.maintainAspect) +
      check("reduceInterlace", "Reduce interlace flicker", "R", state.reduceInterlace) +
      '<div class="ep-radios">' +
      radio("loopMode", "hold", "Hold last frame", "H", state.loopMode === "hold") +
      radio("loopMode", "loop", "Loop event", "e", state.loopMode === "loop") +
      radio("loopMode", "trim", "Trim event to include all frames", "T", state.loopMode === "trim") +
      "</div>" +
      '<div class="ep-radios">' +
      radio("resample", "project", "Use Project Resample Mode", "j", state.resample === "project") +
      radio("resample", "blend", "Frame Blend", "F", state.resample === "blend") +
      radio("resample", "optical", "Optical Flow", "O", state.resample === "optical") +
      radio("resample", "disable", "Disable resample", "D", state.resample === "disable") +
      "</div>" +
      "</div>" +
      '<div class="ep-sep"></div>' +
      '<div class="ep-row ep-row--rate">' +
      "<label>" +
      ak("Playback rate:", "P") +
      "</label>" +
      spin(state.playbackRate, "playbackRate") +
      '<button type="button" class="ep-btn" data-ep-act="conform">' +
      ak("Conform to Project Frame Rate", "C") +
      "</button>" +
      "</div>" +
      '<div class="ep-row ep-row--rate">' +
      "<label>" +
      ak("Undersample rate:", "U") +
      "</label>" +
      spin(state.undersampleRate, "undersampleRate") +
      '<span class="ep-hint">59,940 fps</span>' +
      "</div>" +
      "</div>"
    );
  }

  function tabMedia(state) {
    if (state.kind === "audio") {
      return (
        '<div class="ep-panel" data-ep-panel="media">' +
        '<div class="ep-row ep-row--file">' +
        "<label>File name:</label>" +
        '<div class="ep-readonly" title="' +
        esc(state.filePath) +
        '">' +
        esc(state.filePath) +
        "</div>" +
        "</div>" +
        '<div class="ep-row">' +
        "<label>" +
        ak("Tape name:", "T") +
        "</label>" +
        '<input type="text" class="ep-input" data-ep="tapeName" value="' +
        esc(state.tapeName) +
        '" />' +
        "</div>" +
        section("Timecode") +
        '<div class="ep-radios ep-radios--indent is-disabled">' +
        radio("timecode", "file", "Use timecode in file", "U", true) +
        '<div class="ep-row ep-row--tc">' +
        radio("timecode", "custom", "Use custom timecode:", "c", false) +
        '<input type="text" class="ep-input ep-input--tc" data-ep="customTC" value="' +
        esc(state.customTC) +
        '" disabled />' +
        "</div>" +
        "</div>" +
        '<div class="ep-row ep-row--indent">' +
        '<select class="ep-select" data-ep="tcFormat" disabled>' +
        opt(["Time", "Seconds", "Time & Frames", "SMPTE Drop (29.97 fps, Video)"], state.tcFormat) +
        "</select>" +
        "</div>" +
        section("Stream properties") +
        '<div class="ep-grid">' +
        "<label>" +
        ak("Stream:", "S") +
        "</label>" +
        '<div class="ep-stream">' +
        '<select class="ep-select" data-ep="stream">' +
        opt(["Audio 1", "Audio 2"], state.stream) +
        "</select>" +
        "</div>" +
        "<label>Format:</label><div class=\"ep-static\">Uncompressed</div>" +
        "<label>Attributes:</label><div class=\"ep-static\">192 000 Hz; 24 Bit; Stereo; 5.0.036</div>" +
        "</div>" +
        "</div>"
      );
    }

    return (
      '<div class="ep-panel" data-ep-panel="media">' +
      '<div class="ep-row ep-row--file">' +
      "<label>File name:</label>" +
      '<div class="ep-readonly" title="' +
      esc(state.filePath) +
      '">' +
      esc(state.filePath) +
      "</div>" +
      "</div>" +
      '<div class="ep-row">' +
      "<label>" +
      ak("Tape name:", "T") +
      "</label>" +
      '<input type="text" class="ep-input" data-ep="tapeName" value="' +
      esc(state.tapeName) +
      '" />' +
      "</div>" +
      section("Timecode") +
      '<div class="ep-radios ep-radios--indent">' +
      radio("timecode", "file", "Use timecode in file", "U", state.timecode === "file") +
      '<div class="ep-row ep-row--tc">' +
      radio("timecode", "custom", "Use custom timecode:", "c", state.timecode === "custom") +
      '<input type="text" class="ep-input ep-input--tc" data-ep="customTC" value="' +
      esc(state.customTC) +
      '"' +
      (state.timecode !== "custom" ? " disabled" : "") +
      " />" +
      "</div>" +
      "</div>" +
      '<div class="ep-row ep-row--indent">' +
      "<label></label>" +
      '<select class="ep-select" data-ep="tcFormat"' +
      (state.timecode !== "custom" ? " disabled" : "") +
      ">" +
      opt(
        [
          "SMPTE Drop (29.97 fps, Video)",
          "SMPTE Non-Drop (29.97 fps, Video)",
          "SMPTE EBU (25 fps, Video)",
          "SMPTE Film Sync (24 fps, Film)",
          "Time & Frames",
        ],
        state.tcFormat
      ) +
      "</select>" +
      "</div>" +
      section("Stream properties") +
      '<div class="ep-grid">' +
      "<label>" +
      ak("Stream:", "S") +
      "</label>" +
      '<div class="ep-stream">' +
      '<select class="ep-select" data-ep="stream">' +
      opt(["Video 1", "Video 2"], state.stream) +
      "</select>" +
      '<span class="ep-stream__ico" title="Media" aria-hidden="true">📄</span>' +
      "</div>" +
      "<label>Format:</label><div class=\"ep-static\">MP4 Base Media v1 · AVC</div>" +
      "<label>Attributes:</label><div class=\"ep-static\">1920x1080x32; 33.2.000</div>" +
      "<label>" +
      ak("Frame rate:", "F") +
      "</label>" +
      '<select class="ep-select" data-ep="frameRate">' +
      opt(
        ["59,940 (Double NTSC)", "29,970 (NTSC)", "25,000 (PAL)", "24,000 (Film)", "23,976 (NTSC Film)"],
        state.frameRate
      ) +
      "</select>" +
      "<label>" +
      ak("Field order:", "i") +
      "</label>" +
      '<select class="ep-select" data-ep="fieldOrder">' +
      opt(["None (progressive scan)", "Upper field first", "Lower field first"], state.fieldOrder) +
      "</select>" +
      "<label>" +
      ak("Pixel aspect ratio:", "P") +
      "</label>" +
      '<select class="ep-select" data-ep="pixelAspect">' +
      opt(["1,0000 (Square)", "0,9091 (NTSC DV)", "1,0926 (PAL DV)"], state.pixelAspect) +
      "</select>" +
      "<label>" +
      ak("Alpha channel:", "l") +
      "</label>" +
      '<select class="ep-select" data-ep="alpha">' +
      opt(["None", "Straight", "Premultiplied"], state.alpha) +
      "</select>" +
      '<label class="ep-dim">Background color:</label>' +
      '<button type="button" class="ep-swatch" disabled title="Background color"></button>' +
      "<label>" +
      ak("Color space:", "o") +
      "</label>" +
      '<select class="ep-select" data-ep="colorSpace">' +
      opt(["Default", "Rec. 709", "Rec. 601", "sRGB"], state.colorSpace) +
      "</select>" +
      "<label>" +
      ak("Color range:", "r") +
      "</label>" +
      '<select class="ep-select" data-ep="colorRange">' +
      opt(["Undefined", "Full (0-255)", "Studio (16-235)"], state.colorRange) +
      "</select>" +
      "<label>" +
      ak("Rotation:", "t") +
      "</label>" +
      '<select class="ep-select" data-ep="rotation">' +
      opt(["0° (original)", "90° clockwise", "180°", "90° counter-clockwise"], state.rotation) +
      "</select>" +
      "<label>" +
      ak("Stereoscopic 3D mode:", "3") +
      "</label>" +
      '<select class="ep-select" data-ep="stereo3d">' +
      opt(["Off", "Side by side", "Top/bottom", "Line alternate"], state.stereo3d) +
      "</select>" +
      "<label></label>" +
      '<label class="ep-check ep-check--inline is-disabled">' +
      '<input type="checkbox" disabled data-ep="swapLR" />' +
      "<span>" +
      ak("Swap Left/Right", "w") +
      "</span></label>" +
      "</div>" +
      "</div>"
    );
  }

  function generalText(state) {
    if (state.kind === "audio") {
      return (
        "General\n" +
        "  Name: " +
        state.fileName +
        "\n" +
        "  Folder: D:\\Devs\\C++\\OpenVegas\\SAMPLES\\assets\n" +
        "  Type: Wave (Microsoft)\n" +
        "  Size: 11,57 MB (11 848 798 bytes)\n" +
        "  Created: 19 июля 2026 г., 23:46:35\n" +
        "  Modified: 19 июля 2026 г., 23:46:35\n" +
        "  Accessed: 28 июля 2026 г., 23:28:21\n" +
        "  Attributes: Archive\n" +
        "\n" +
        "Media information\n" +
        "  Stream format: Wave\n" +
        "\n" +
        "  Audio stream #1\n" +
        "    Audio format: PCM\n" +
        "    Sampling rate: 192000 Hz\n" +
        "    Channels: 2 channels\n" +
        "    Bit rate mode: Constant\n" +
        "    Bit rate: 9216000 bps\n" +
        "\n" +
        "Streams\n" +
        "  Audio: 00:00:10,285, 192 000 Hz; 24 Bit; Stereo, Uncompressed\n" +
        "\n" +
        "Summary\n" +
        "  Software: LMMS (libsndfile-1.0.26pre5)\n" +
        "\n" +
        "ACID information\n" +
        "  ACID chunk: no\n" +
        "  Stretch chunk: no\n" +
        "  Stretch list: no\n" +
        "  Stretch info2: no\n" +
        "  Beat markers: no\n" +
        "  Detected beats: no\n" +
        "\n" +
        "Other metadata\n" +
        "  Regions/markers: no\n" +
        "  Command markers: no\n" +
        "\n" +
        "Media manager\n" +
        "  Media tags: no\n" +
        "\n" +
        "Plug-In\n" +
        "  Name: wavplug.dll\n" +
        "  Folder: C:\\Program Files (x86)\\Steam\\steamapps\\common\\VEGAS Pro 22.0\n" +
        "  Format: Wave (Microsoft)\n" +
        "  Version: Version 22.0 (Build 250)\n" +
        "  Company: MAGIX Computer Products Intl. Co.\n"
      );
    }

    return (
      "General\n" +
      "  Name: " +
      state.fileName +
      "\n" +
      "  Folder: D:\\Devs\\C++\\OpenVegas\\SAMPLES\\assets\n" +
      "  Type: ISO Base\n" +
      "  Size: 54,56 MB (55 865 416 bytes)\n" +
      "  Created: 27 июля 2026 г., 5:30:21\n" +
      "  Modified: 16 августа 2020 г., 15:04:14\n" +
      "  Accessed: 28 июля 2026 г., 23:28:21\n" +
      "  Attributes: Archive\n" +
      "\n" +
      "Media information\n" +
      "  Stream format: MPEG-4\n" +
      "\n" +
      "  Video stream #1\n" +
      "    Video format: AVC\n" +
      "    Resolution: 1920 x 1080 px\n" +
      "    Aspect ratio: 16:9\n" +
      "    Color depth: 8 bit\n" +
      "    Frame rate: 59.940 fps\n" +
      "    Scan type: Progressive\n" +
      "    Bit rate: 6531843 bps\n" +
      "\n" +
      "  Audio stream #1\n" +
      "    Audio format: AAC\n" +
      "    Sampling rate: 44100 Hz\n" +
      "    Channels: 2 channels\n" +
      "    Bit rate mode: Constant\n" +
      "    Bit rate: 127999 bps\n" +
      "\n" +
      "Streams\n" +
      "  Video: 00:01:07,000, 59,940 fps progressive, 1920x1080x32, A\n" +
      "  Audio: 00:01:07,059, 44100 Hz, 16 Bits, Stereo, AAC\n" +
      "\n" +
      "ACID information\n" +
      "  ACID chunk: no\n" +
      "  Stretch chunk: no\n" +
      "  Stretch list: no\n" +
      "  Stretch info2: no\n" +
      "  Beat markers: no\n" +
      "  Detected beats: no\n" +
      "\n" +
      "Other metadata\n" +
      "  Regions/markers: no\n" +
      "  Command markers: no\n" +
      "\n" +
      "Media manager\n" +
      "  Media tags: no\n" +
      "\n" +
      "Plug-In\n" +
      "  Name: mxcompoundplug.dll\n" +
      "  Folder: C:\\Program Files (x86)\\Steam\\steamapps\\common\\VEGAS Pro 22.0\n" +
      "  Format: MAGIX AVC\n" +
      "  Version: Version 22.0 (Build 250)\n" +
      "  Company: MAGIX Computer Products Intl. Co.\n"
    );
  }

  function tabGeneral(state) {
    return (
      '<div class="ep-panel" data-ep-panel="general">' +
      "<label class=\"ep-general-label\">File properties:</label>" +
      '<textarea class="ep-general" readonly spellcheck="false">' +
      esc(generalText(state)) +
      "</textarea>" +
      "</div>"
    );
  }

  function readStateFromEvent(ev) {
    const kind = isAudio(ev) ? "audio" : "video";
    const name = eventLabel(ev);
    const fileName =
      kind === "audio"
        ? /\.(wav|mp3|flac|ogg|aif)$/i.test(name)
          ? name
          : name + ".wav"
        : /\.mp4$/i.test(name)
          ? name
          : name + ".mp4";

    const base = {
      kind,
      takeName: name.replace(/\.[^.]+$/, ""),
      mute: ev.classList.contains("is-event-muted"),
      lock: ev.classList.contains("is-event-locked"),
      tapeName: ev.dataset.tapeName || "",
      timecode: ev.dataset.timecode || "file",
      customTC: ev.dataset.customTC || "00:00:00;00",
      filePath: mediaPath(fileName, kind),
      fileName,
    };

    if (kind === "audio") {
      return Object.assign(base, {
        loop: ev.classList.contains("is-event-loop"),
        invertPhase: ev.classList.contains("is-invert-phase"),
        normalize: ev.classList.contains("is-normalized"),
        autoNormalize: ev.dataset.autoNormalize === "1",
        gain: ev.dataset.gain || "0,0 dB",
        stretchMethod: ev.dataset.stretchMethod || "Classic",
        stream: ev.dataset.mediaStream || "Audio 1",
        tcFormat: ev.dataset.tcFormat || "Time",
      });
    }

    return Object.assign(base, {
      maintainAspect: ev.dataset.maintainAspect !== "0",
      reduceInterlace: ev.classList.contains("is-reduce-interlace"),
      loopMode: ev.classList.contains("is-event-loop") ? "loop" : ev.dataset.loopMode || "loop",
      resample: ev.dataset.resample || "project",
      playbackRate: ev.dataset.playbackRate || "1,000",
      undersampleRate: ev.dataset.undersampleRate || "1,000",
      tcFormat: ev.dataset.tcFormat || "SMPTE Drop (29.97 fps, Video)",
      stream: ev.dataset.mediaStream || "Video 1",
      frameRate: ev.dataset.frameRate || "59,940 (Double NTSC)",
      fieldOrder: ev.dataset.fieldOrder || "None (progressive scan)",
      pixelAspect: ev.dataset.pixelAspect || "1,0000 (Square)",
      alpha: ev.dataset.alpha || "None",
      colorSpace: ev.dataset.colorSpace || "Default",
      colorRange: ev.dataset.colorRange || "Undefined",
      rotation: ev.dataset.rotation || "0° (original)",
      stereo3d: ev.dataset.stereo3d || "Off",
    });
  }

  function buildMarkup(state) {
    const eventTabLabel = state.kind === "audio" ? ak("Audio Event", "A") : ak("Video Event", "V");
    const eventPanel = state.kind === "audio" ? tabAudioEvent(state) : tabVideoEvent(state);
    return (
      '<div class="ep-dialog" role="dialog" aria-modal="true" aria-labelledby="ep-title">' +
      '<div class="ep-dialog__titlebar" data-ep-drag>' +
      '<span class="ep-dialog__title" id="ep-title">Properties</span>' +
      '<div class="ep-dialog__winbtns">' +
      '<button type="button" class="ep-winbtn" title="Help" aria-label="Help">?</button>' +
      '<button type="button" class="ep-winbtn ep-winbtn--close" data-ep-close title="Close" aria-label="Close">✕</button>' +
      "</div></div>" +
      '<div class="ep-dialog__body">' +
      '<div class="ep-tabs" role="tablist">' +
      '<button type="button" class="ep-tab is-active" data-ep-tab="event" role="tab">' +
      eventTabLabel +
      "</button>" +
      '<button type="button" class="ep-tab" data-ep-tab="media" role="tab">' +
      ak("Media", "M") +
      "</button>" +
      '<button type="button" class="ep-tab" data-ep-tab="general" role="tab">' +
      ak("General", "G") +
      "</button>" +
      "</div>" +
      '<div class="ep-panels">' +
      eventPanel +
      tabMedia(state) +
      tabGeneral(state) +
      "</div>" +
      "</div>" +
      '<div class="ep-dialog__footer">' +
      '<button type="button" class="ep-btn ep-btn--default" data-ep-ok>OK</button>' +
      '<button type="button" class="ep-btn" data-ep-cancel>Cancel</button>' +
      "</div>" +
      "</div>"
    );
  }

  function ensure() {
    let root = document.getElementById(WIN_ID);
    if (root) return root;
    root = document.createElement("div");
    root.id = WIN_ID;
    root.className = "ep-backdrop";
    root.hidden = true;
    document.body.appendChild(root);
    root.addEventListener("click", (e) => {
      if (e.target === root) close();
    });
    return root;
  }

  function collectForm(root) {
    const s = Object.assign({}, snapshot);
    root.querySelectorAll("[data-ep]").forEach((el) => {
      const key = el.getAttribute("data-ep");
      if (el.type === "checkbox") s[key] = el.checked;
      else s[key] = el.value;
    });
    const loop = root.querySelector('input[data-ep-radio="loopMode"]:checked');
    if (loop) s.loopMode = loop.value;
    const resample = root.querySelector('input[data-ep-radio="resample"]:checked');
    if (resample) s.resample = resample.value;
    const tc = root.querySelector('input[data-ep-radio="timecode"]:checked');
    if (tc) s.timecode = tc.value;
    const gainVal = root.querySelector("[data-ep-gain-val]");
    if (gainVal) s.gain = gainVal.textContent.trim();
    return s;
  }

  function open(eventEl) {
    if (!eventEl?.classList?.contains("event")) return;
    currentEvent = eventEl;
    snapshot = readStateFromEvent(eventEl);
    const root = ensure();
    root.innerHTML = buildMarkup(snapshot);
    wire(root);
    root.hidden = false;
    root.classList.add("is-open");
    const dlg = root.querySelector(".ep-dialog");
    if (dlg) {
      dlg.style.position = "";
      dlg.style.left = "";
      dlg.style.top = "";
      dlg.style.margin = "";
    }
  }

  function syncSwitchBadges(ev) {
    let row = ev.querySelector(".event__switch-badges");
    if (!row) {
      row = document.createElement("div");
      row.className = "event__switch-badges";
      ev.appendChild(row);
    }
    const badges = [];
    if (ev.classList.contains("is-event-muted")) badges.push("M");
    if (ev.classList.contains("is-event-locked")) badges.push("L");
    if (ev.classList.contains("is-event-loop")) badges.push("⟳");
    if (ev.classList.contains("is-reversed")) badges.push("◀");
    if (ev.classList.contains("is-normalized")) badges.push("N");
    if (ev.classList.contains("is-invert-phase")) badges.push("Ø");
    row.textContent = badges.join(" ");
    row.hidden = !badges.length;
  }

  function applyToEvent(ev, s) {
    if (!ev || !s) return;

    const title = ev.querySelector(".event__titlebar");
    if (title && s.takeName) {
      const badges = Array.from(title.querySelectorAll(".event__nested-badge, .event__take-badge"));
      title.textContent = s.takeName;
      badges.forEach((b) => title.appendChild(b));
    }
    const label = ev.querySelector(".event__label");
    if (label) label.textContent = s.takeName;

    ev.classList.toggle("is-event-muted", !!s.mute);
    ev.classList.toggle("is-event-locked", !!s.lock);
    ev.dataset.tapeName = s.tapeName || "";
    ev.dataset.timecode = s.timecode || "file";
    ev.dataset.customTC = s.customTC || "";
    ev.dataset.tcFormat = s.tcFormat || "";
    ev.dataset.mediaStream = s.stream || "";

    if (s.kind === "audio" || isAudio(ev)) {
      ev.classList.toggle("is-event-loop", !!s.loop);
      ev.classList.toggle("is-invert-phase", !!s.invertPhase);
      ev.classList.toggle("is-normalized", !!s.normalize);
      ev.dataset.autoNormalize = s.autoNormalize ? "1" : "0";
      ev.dataset.gain = s.gain || "0,0 dB";
      ev.dataset.stretchMethod = s.stretchMethod || "Classic";
      syncSwitchBadges(ev);
      return;
    }

    ev.classList.toggle("is-reduce-interlace", !!s.reduceInterlace);
    ev.classList.toggle("is-event-loop", s.loopMode === "loop");
    ev.dataset.maintainAspect = s.maintainAspect ? "1" : "0";
    ev.dataset.loopMode = s.loopMode;
    ev.dataset.resample = s.resample;
    ev.dataset.playbackRate = s.playbackRate;
    ev.dataset.undersampleRate = s.undersampleRate;
    ev.dataset.frameRate = s.frameRate;
    ev.dataset.fieldOrder = s.fieldOrder;
    ev.dataset.pixelAspect = s.pixelAspect;
    ev.dataset.alpha = s.alpha;
    ev.dataset.colorSpace = s.colorSpace;
    ev.dataset.colorRange = s.colorRange;
    ev.dataset.rotation = s.rotation;
    ev.dataset.stereo3d = s.stereo3d;

    if (s.playbackRate && s.playbackRate !== "1,000") {
      ev.dataset.rate = s.playbackRate;
      let rateEl = ev.querySelector(".event__rate");
      if (!rateEl) {
        rateEl = document.createElement("span");
        rateEl.className = "event__rate";
        ev.appendChild(rateEl);
      }
      rateEl.textContent = s.playbackRate;
    } else {
      delete ev.dataset.rate;
      ev.querySelector(".event__rate")?.remove();
    }

    syncSwitchBadges(ev);
  }

  function wire(root) {
    if (root.dataset.wired) return;
    root.dataset.wired = "1";

    root.addEventListener("click", (e) => {
      const tab = e.target.closest("[data-ep-tab]");
      if (tab) {
        const id = tab.getAttribute("data-ep-tab");
        root.querySelectorAll(".ep-tab").forEach((t) => t.classList.toggle("is-active", t === tab));
        root.querySelectorAll(".ep-panel").forEach((p) => {
          p.classList.toggle("is-active", p.getAttribute("data-ep-panel") === id);
        });
        return;
      }

      if (e.target.closest("[data-ep-close], [data-ep-cancel]")) {
        close();
        return;
      }
      if (e.target.closest("[data-ep-ok]")) {
        if (currentEvent) applyToEvent(currentEvent, collectForm(root));
        close();
        return;
      }
      if (e.target.closest('[data-ep-act="conform"]')) {
        const input = root.querySelector('[data-ep="playbackRate"]');
        if (input) input.value = "1,000";
        return;
      }

      if (e.target.closest('[data-ep-act="recalc"]')) {
        const val = root.querySelector("[data-ep-gain-val]");
        if (val && !val.classList.contains("is-disabled")) {
          val.textContent = "0,0 dB";
          const hidden = root.querySelector('[data-ep="gain"]');
          // store via dataset on a hidden field — keep span as display
          snapshot.gain = "0,0 dB";
        }
        return;
      }

      const spinBtn = e.target.closest("[data-ep-spin]");
      if (spinBtn) {
        const wrap = spinBtn.closest(".ep-spin");
        const input = wrap?.querySelector("input");
        if (!input) return;
        const dir = spinBtn.getAttribute("data-ep-spin") === "up" ? 1 : -1;
        const raw = String(input.value).replace(",", ".");
        let n = parseFloat(raw);
        if (!Number.isFinite(n)) n = 1;
        n = Math.max(0.001, Math.round((n + dir * 0.001) * 1000) / 1000);
        input.value = n.toFixed(3).replace(".", ",");
      }
    });

    root.addEventListener("change", (e) => {
      if (e.target.matches('[data-ep="normalize"], [data-ep="autoNormalize"]')) {
        const norm = root.querySelector('[data-ep="normalize"]')?.checked;
        const auto = root.querySelector('[data-ep="autoNormalize"]')?.checked;
        const on = !!(norm || auto);
        root.querySelector("[data-ep-gain-val]")?.classList.toggle("is-disabled", !on);
        const btn = root.querySelector('[data-ep-act="recalc"]');
        if (btn) btn.disabled = !on;
      }
      if (e.target.matches('[data-ep-radio="timecode"]')) {
        const custom = e.target.value === "custom";
        root.querySelector('[data-ep="customTC"]')?.toggleAttribute("disabled", !custom);
        root.querySelector('[data-ep="tcFormat"]')?.toggleAttribute("disabled", !custom);
      }
      if (e.target.matches('[data-ep="alpha"]')) {
        const on = e.target.value !== "None";
        root.querySelector(".ep-swatch")?.toggleAttribute("disabled", !on);
      }
      if (e.target.matches('[data-ep="stereo3d"]')) {
        const on = e.target.value !== "Off";
        const cb = root.querySelector('[data-ep="swapLR"]');
        if (cb) {
          cb.disabled = !on;
          cb.closest(".ep-check")?.classList.toggle("is-disabled", !on);
        }
      }
    });

    // Drag
    const bar = () => root.querySelector("[data-ep-drag]");
    root.addEventListener("pointerdown", (e) => {
      const handle = e.target.closest("[data-ep-drag]");
      if (!handle || e.target.closest("button")) return;
      const dlg = root.querySelector(".ep-dialog");
      if (!dlg) return;
      e.preventDefault();
      const rect = dlg.getBoundingClientRect();
      const ox = e.clientX - rect.left;
      const oy = e.clientY - rect.top;
      function onMove(ev) {
        dlg.style.position = "fixed";
        dlg.style.left = Math.max(0, Math.min(window.innerWidth - 80, ev.clientX - ox)) + "px";
        dlg.style.top = Math.max(0, Math.min(window.innerHeight - 40, ev.clientY - oy)) + "px";
        dlg.style.margin = "0";
      }
      function onUp() {
        window.removeEventListener("pointermove", onMove);
        window.removeEventListener("pointerup", onUp);
      }
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });
  }

  function close() {
    const root = document.getElementById(WIN_ID);
    if (!root) return;
    root.classList.remove("is-open");
    root.hidden = true;
    currentEvent = null;
    snapshot = null;
  }

  function isOpen() {
    return !!document.getElementById(WIN_ID)?.classList.contains("is-open");
  }

  document.addEventListener("keydown", (e) => {
    if (!isOpen()) return;
    if (e.key === "Escape") {
      e.preventDefault();
      close();
    }
  });

  window.VegasEventProperties = { open, close, isOpen };
})();
