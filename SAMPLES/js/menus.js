/**
 * Dropdown menus + context menus for Vegas Pro HTML mockups.
 */
(function () {
  const MENU_DATA = {
    File: [
      {
        label: "New...",
        shortcut: "Ctrl+N",
        svg: '<path d="M3 2h7l3 3v9H3V2zm7 1v2h3" fill="none" stroke="currentColor" stroke-width="1.2"/>',
      },
      { label: "Welcome Screen", shortcut: "", ico: "" },
      {
        label: "Open...",
        shortcut: "Ctrl+O",
        svg: '<path d="M1.5 4h5l1.5 2H14.5v8H1.5V4z" fill="none" stroke="currentColor" stroke-width="1.2"/>',
      },
      { label: "Close", shortcut: "Ctrl+F4", ico: "" },
      { sep: true },
      {
        label: "Save",
        shortcut: "Ctrl+S",
        svg: '<path d="M2.5 2.5h8.5L13.5 5v8.5h-11V2.5zm2 0v3.5h5.5V2.5M4.5 9h7v4h-7V9z" fill="none" stroke="currentColor" stroke-width="1.15"/>',
      },
      {
        label: "Save As...",
        shortcut: "Ctrl+Shift+S",
        svg: '<path d="M2.5 2.5h8.5L13.5 5v8.5h-11V2.5zm2 0v3.5h5.5V2.5M4.5 9h7v4h-7V9z" fill="none" stroke="currentColor" stroke-width="1.15"/><path d="M10.5 11.5h3M12 10v3" stroke="currentColor" stroke-width="1.2"/>',
      },
      {
        label: "Incremental Save",
        shortcut: "Ctrl+Alt+S",
        svg: '<path d="M2.5 2.5h8.5L13.5 5v8.5h-11V2.5zm2 0v3.5h5.5V2.5M4.5 9h4.5v4H4.5V9z" fill="none" stroke="currentColor" stroke-width="1.15"/><path d="M11.2 10.2h2.6M12.5 8.9v2.6" stroke="currentColor" stroke-width="1.25"/>',
      },
      { sep: true },
      {
        label: "Render As...",
        shortcut: "Ctrl+Shift+M",
        action: "render-as",
        svg: '<path d="M2 3.5h9v7H2v-7zm10 1.5h2v6h-2V5zM4.5 12.5h4" fill="none" stroke="currentColor" stroke-width="1.2"/>',
      },
      { label: "Real-Time Render...", shortcut: "", ico: "", disabled: true },
      { sep: true },
      {
        label: "Import",
        shortcut: "",
        submenu: [
          {
            label: "Media...",
            shortcut: "",
            svg: '<path d="M2.5 3.5h8v9h-8zM10.5 6.5l3-1.5v6l-3-1.5V6.5z" fill="none" stroke="currentColor" stroke-width="1.15"/><path d="M5.5 7.5v4M3.5 9.5h4" stroke="currentColor" stroke-width="1.15"/>',
          },
          { label: "Media from Project...", shortcut: "", ico: "" },
          { label: "Premiere/After Effects (*.prproj)...", shortcut: "", ico: "" },
          { label: "Final Cut Pro 7/DaVinci Resolve (*.xml)...", shortcut: "", ico: "" },
          { label: "Final Cut Pro X (*.fcpxml)...", shortcut: "", ico: "" },
          { label: "EDL Text File (*.txt)...", shortcut: "", ico: "" },
          { label: "Broadcast Wave Format...", shortcut: "", ico: "" },
          { label: "Closed Captioning...", shortcut: "", ico: "" },
        ],
      },
      {
        label: "Export",
        shortcut: "",
        submenu: [
          { label: "OpenVegas Project Archive (*.veg)...", shortcut: "", ico: "" },
          { label: "Premiere/After Effects (*.prproj)...", shortcut: "", ico: "" },
          { label: "Final Cut Pro 7/DaVinci Resolve (*.xml)...", shortcut: "", ico: "" },
          { label: "Final Cut Pro X (*.fcpxml)...", shortcut: "", ico: "" },
          { label: "EDL Text File (*.txt)...", shortcut: "", ico: "" },
        ],
      },
      {
        label: "OpenVegas Capture...",
        shortcut: "",
        svg: '<rect x="2" y="4.5" width="9.5" height="7" rx="0.8" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M11.5 7l3-1.5v5L11.5 9V7z"/><circle cx="6.5" cy="8" r="1.5" fill="#c42b1c"/>',
      },
      {
        label: "Extract Audio from CD...",
        shortcut: "",
        svg: '<circle cx="8" cy="8" r="5.2" fill="none" stroke="currentColor" stroke-width="1.2"/><circle cx="8" cy="8" r="1.4"/><path d="M10.5 5.2c1.1.6 1.8 1.6 1.8 2.8" fill="none" stroke="currentColor" stroke-width="1.1"/>',
      },
      { sep: true },
      {
        label: "Properties...",
        shortcut: "Alt+Enter",
        action: "project-properties",
        svg: '<path d="M6.4 1.6h3.2l.35 1.45 1.35.55 1.25-.9 2.25 2.25-.9 1.25.55 1.35 1.45.35v3.2l-1.45.35-.55 1.35.9 1.25-2.25 2.25-1.25-.9-1.35.55L9.6 14.4H6.4l-.35-1.45-1.35-.55-1.25.9L1.2 11.05l.9-1.25-.55-1.35L.1 8V4.8l1.45-.35.55-1.35-.9-1.25L3.45.6l1.25.9 1.35-.55zm1.6 3.9A2.5 2.5 0 108 10.9a2.5 2.5 0 000-5.4z" fill="none" stroke="currentColor" stroke-width="1.1" stroke-linejoin="round"/>',
      },
      { sep: true },
      { label: "Exit", shortcut: "Alt+F4", ico: "" },
      { sep: true },
      { label: "1  D:\\Devs\\C++\\OpenVegas\\SAMPLES\\example_project_with_fades_and_crossfades.veg", shortcut: "", ico: "" },
      { label: "2  D:\\Devs\\C++\\OpenVegas\\SAMPLES\\example_project_with_only_video.veg", shortcut: "", ico: "" },
      { label: "3  D:\\Devs\\C++\\OpenVegas\\sample_ui\\example_project_with_fades_and_crossfades.veg", shortcut: "", ico: "" },
      { label: "4  D:\\Devs\\C++\\OpenVegas\\sample_ui\\example_project_with_trimmers_markers.veg", shortcut: "", ico: "" },
      { label: "5  D:\\Devs\\C++\\OpenVegas\\sample_ui\\example_project_with_2_videos-one_stretched.veg", shortcut: "", ico: "" },
      { label: "6  D:\\Devs\\C++\\OpenVegas\\sample_ui\\example_project_with_2_videos-one-compressed.veg", shortcut: "", ico: "" },
    ],
    Edit: [
      { label: "Undo", shortcut: "Ctrl+Z", ico: "↶" },
      { label: "Redo", shortcut: "Ctrl+Shift+Z", ico: "↷" },
      { label: "Repeat", shortcut: "Ctrl+Y", ico: "" },
      { sep: true },
      { label: "Cut", shortcut: "Ctrl+X", ico: "✂" },
      { label: "Copy", shortcut: "Ctrl+C", ico: "📋" },
      { label: "Paste", shortcut: "Ctrl+V", ico: "📎" },
      { sep: true },
      { label: "Delete", shortcut: "Delete", ico: "" },
      { label: "Select All", shortcut: "Ctrl+A", ico: "" },
      { sep: true },
      { label: "Group", shortcut: "G", ico: "" },
      { label: "Ungroup", shortcut: "U", ico: "" },
    ],
    View: [
      { label: "Show Bus Tracks", shortcut: "", ico: "" },
      { label: "Active Take Information", shortcut: "", ico: "" },
      { sep: true },
      { label: "Zoom In Time", shortcut: "Up", ico: "" },
      { label: "Zoom Out Time", shortcut: "Down", ico: "" },
      { label: "Zoom Time to Selection", shortcut: "", ico: "" },
      { sep: true },
      { label: "Window Layouts", shortcut: "", ico: "▸" },
      { label: "Toolbar", shortcut: "", ico: "▸" },
      { label: "Docking Layouts", shortcut: "", ico: "▸" },
    ],
    Insert: [
      { label: "Audio Track", shortcut: "Ctrl+T", ico: "" },
      { label: "Video Track", shortcut: "Ctrl+Shift+T", ico: "" },
      { sep: true },
      { label: "Audio Envelope", shortcut: "", ico: "▸" },
      { label: "Video Envelope", shortcut: "", ico: "▸" },
      { sep: true },
      { label: "Audio Bus Track", shortcut: "", ico: "" },
      { label: "Video Bus Track", shortcut: "", ico: "" },
      { sep: true },
      { label: "Empty Event", shortcut: "", ico: "" },
      { label: "Text Media...", shortcut: "", ico: "" },
    ],
    Tools: [
      { label: "Scripting", shortcut: "", ico: "▸" },
      { label: "External Tools", shortcut: "", ico: "▸" },
      { sep: true },
      { label: "Build Dynamic RAM Preview", shortcut: "Shift+B", ico: "" },
      { label: "Selectively Prune Dynamic RAM Preview", shortcut: "", ico: "" },
      { sep: true },
      { label: "Audio", shortcut: "", ico: "▸" },
      { label: "Video", shortcut: "", ico: "▸" },
    ],
    Options: [
      { label: "Enable Snapping", shortcut: "F8", ico: "✓" },
      { label: "Quantize to Frames", shortcut: "Alt+F8", ico: "✓" },
      { label: "Enable Ripple Editing", shortcut: "Ctrl+L", ico: "▸" },
      { sep: true },
      { label: "Metronome", shortcut: "", ico: "" },
      { label: "Ignore Event Grouping", shortcut: "Ctrl+Shift+U", ico: "", action: "ignore-event-grouping", check: true },
      { sep: true },
      { label: "Preferences...", shortcut: "", ico: "", action: "preferences" },
    ],
    Help: [
      { label: "Contents and Index", shortcut: "F1", ico: "" },
      { label: "OpenVegas Interactive Tutorials", shortcut: "", ico: "" },
      { sep: true },
      { label: "Keyboard Mapping...", shortcut: "", ico: "" },
      { label: "About OpenVegas...", shortcut: "", ico: "" },
    ],
  };

  const SVG = {
    nestedPlus:
      '<rect x="2" y="3" width="8" height="8" rx="0.8" fill="none" stroke="#6eb0ff" stroke-width="1.2"/>' +
      '<rect x="6" y="5" width="8" height="8" rx="0.8" fill="#1a1a1a" stroke="#9ec8ff" stroke-width="1.1"/>' +
      '<path d="M10 7.2v3.6M8.2 9h3.6" stroke="#9ec8ff" stroke-width="1.2"/>',
    nested:
      '<rect x="2" y="3" width="8" height="8" rx="0.8" fill="none" stroke="#6eb0ff" stroke-width="1.2"/>' +
      '<rect x="6" y="5" width="8" height="8" rx="0.8" fill="#1a1a1a" stroke="#9ec8ff" stroke-width="1.1"/>',
    fx:
      '<text x="1" y="12" fill="#5eb0ff" font-size="11" font-family="Segoe UI,Arial,sans-serif" font-weight="700" font-style="italic">fx</text>',
    fxCircle:
      '<circle cx="8" cy="8" r="6.2" fill="none" stroke="#3a7ab8" stroke-width="1.2"/>' +
      '<text x="3.2" y="11.2" fill="#5eb0ff" font-size="8" font-family="Segoe UI,Arial,sans-serif" font-weight="700" font-style="italic">fx</text>',
    panCrop:
      '<path d="M2 5V2h3M11 2h3v3M14 11v3h-3M5 14H2v-3" fill="none" stroke="#e8e8e8" stroke-width="1.3"/>' +
      '<rect x="4.5" y="4.5" width="7" height="7" fill="none" stroke="#aaa" stroke-width="1"/>',
    motion:
      '<circle cx="5" cy="3.2" r="1.4" fill="#e85a5a"/>' +
      '<path d="M5 5.2l1.2 2.4 2.8-1.2-1 3.6 2.2 2.2M5 5.2L3.6 8.2 1.8 7.4M6.2 7.6l-1.4 3.2 2 2.4" fill="none" stroke="#e85a5a" stroke-width="1.15" stroke-linecap="round" stroke-linejoin="round"/>',
    cut:
      '<path d="M4.2 3.2l7.6 9.6M11.8 3.2L4.2 12.8" fill="none" stroke="#ddd" stroke-width="1.25"/>' +
      '<circle cx="3.2" cy="3.2" r="1.5" fill="none" stroke="#ddd" stroke-width="1.1"/>' +
      '<circle cx="3.2" cy="12.8" r="1.5" fill="none" stroke="#ddd" stroke-width="1.1"/>',
    copy:
      '<rect x="5.5" y="4" width="8" height="10" rx="0.8" fill="none" stroke="#ddd" stroke-width="1.15"/>' +
      '<rect x="2.5" y="2" width="8" height="10" rx="0.8" fill="#1a1a1a" stroke="#ddd" stroke-width="1.15"/>',
    del:
      '<path d="M3.5 3.5l9 9M12.5 3.5l-9 9" stroke="#e05555" stroke-width="2" stroke-linecap="round"/>',
    trim:
      '<path d="M3 2.5v11M13 2.5v11" stroke="#6eb0ff" stroke-width="1.4"/>' +
      '<path d="M5.5 5.5h5v5h-5z" fill="none" stroke="#888" stroke-width="1"/>',
    trimStart:
      '<path d="M3 2.5v11" stroke="#e8e8e8" stroke-width="1.4"/>' +
      '<path d="M5.2 4.5v7l5-3.5z" fill="#c44" stroke="#e8e8e8" stroke-width="0.8"/>',
    trimEnd:
      '<path d="M13 2.5v11" stroke="#e8e8e8" stroke-width="1.4"/>' +
      '<path d="M10.8 4.5v7l-5-3.5z" fill="#c44" stroke="#e8e8e8" stroke-width="0.8"/>',
    detect:
      '<path d="M3 3v10M7.5 3v10M12.5 3v10" stroke="#6eb0ff" stroke-width="1.35"/>' +
      '<circle cx="3" cy="5" r="1.1" fill="#6eb0ff"/><circle cx="7.5" cy="9" r="1.1" fill="#6eb0ff"/><circle cx="12.5" cy="6.5" r="1.1" fill="#6eb0ff"/>',
    split:
      '<path d="M4 2.5v11M12 2.5v11" stroke="#ddd" stroke-width="1.45"/>' +
      '<path d="M6.5 8h3" stroke="#888" stroke-width="1.2"/>',
    smartSplit:
      '<path d="M4 2.5v11M12 2.5v11" stroke="#888" stroke-width="1.45" stroke-dasharray="2 1.5"/>' +
      '<path d="M6.5 8h3" stroke="#666" stroke-width="1.2"/>',
    audioNote:
      '<path d="M6.5 12.2a2.2 2.2 0 1 1-1.6-2.1V3.2l7-1.2v7.4a2.2 2.2 0 1 1-1.6-2.1V4.2l-3.8.7v7.3z" fill="#ddd"/>',
    nestedOpen:
      '<rect x="2" y="3" width="8" height="8" rx="0.8" fill="none" stroke="#6eb0ff" stroke-width="1.2"/>' +
      '<path d="M11 5.5l3 2.5-3 2.5V5.5z" fill="#9ec8ff"/>',
    beatDetect:
      '<path d="M2 11V5M4.5 12V4M7 13V3M9.5 11.5V4.5M12 12V5M14.5 10.5V6.5" stroke="#e8c84a" stroke-width="1.35" stroke-linecap="round"/>',
    tempoDetect:
      '<circle cx="7" cy="8" r="4.2" fill="none" stroke="#ddd" stroke-width="1.2"/>' +
      '<path d="M10.2 11.2L14 14.5" stroke="#ddd" stroke-width="1.4" stroke-linecap="round"/>' +
      '<path d="M4.8 8h4.4M7 5.8v4.4" stroke="#e8c84a" stroke-width="1.1"/>',
  };

  const CONTEXT_MENUS = {
    "video-event": [
      { label: "Open in <u>T</u>rimmer", action: "event-open-trimmer" },
      { label: "Open Parent Media in Trimmer", disabled: true },
      { label: "Select <u>i</u>n Project Media list", action: "event-select-media" },
      { label: "Edit Source Project", disabled: true },
      { sep: true },
      {
        label: "Create Nested Timeline",
        shortcut: "Alt+C",
        svg: SVG.nestedPlus,
        action: "event-create-nested",
      },
      {
        label: "Open Nested Timeline",
        shortcut: "Alt+N",
        svg: SVG.nested,
        action: "event-open-nested",
        disabled: true,
      },
      { sep: true },
      {
        label: "<u>M</u>edia FX...",
        action: "video-event-fx",
        svg: SVG.fx,
      },
      { sep: true },
      {
        label: "<u>V</u>ideo Event Pan/Crop...",
        action: "video-pan-crop",
        svg: SVG.panCrop,
      },
      {
        label: "Video Event FX...",
        action: "video-event-fx",
        svg: SVG.fxCircle,
      },
      {
        label: "Motion Tracking",
        shortcut: "Alt+M",
        svg: SVG.motion,
        action: "event-motion-tracking",
      },
      { sep: true },
      { label: "Cu<u>t</u>", shortcut: "Ctrl+X", svg: SVG.cut, action: "event-cut" },
      { label: "<u>C</u>opy", shortcut: "Ctrl+C", svg: SVG.copy, action: "event-copy" },
      { label: "Paste Event Attributes", action: "event-paste-attrs", disabled: true },
      { label: "Selectively Paste Event Attributes", action: "event-paste-attrs-selective", disabled: true },
      { sep: true },
      { label: "<u>D</u>elete", shortcut: "Delete", svg: SVG.del, action: "event-delete" },
      { label: "T<u>r</u>im", shortcut: "Ctrl+T", svg: SVG.trim, disabled: true },
      { label: "Trim Start", shortcut: "Alt+[", svg: SVG.trimStart, action: "event-trim-start" },
      { label: "Trim End", shortcut: "Alt+]", svg: SVG.trimEnd, action: "event-trim-end" },
      { label: "Detect Scenes and Split", svg: SVG.detect, action: "event-detect-scenes" },
      { label: "<u>S</u>plit", shortcut: "S", svg: SVG.split, action: "event-split" },
      { label: "Smart Split", shortcut: "Alt+S", svg: SVG.smartSplit, disabled: true },
      { label: "Event Heal", disabled: true },
      { label: "Close Gaps", disabled: true },
      { sep: true },
      { label: "Resize Adjustment Event to Project", disabled: true },
      { sep: true },
      { label: "Add Missing Stream for Selected Event", disabled: true },
      { label: "Create Subclip", action: "event-create-subclip" },
      { label: "Reverse", action: "event-reverse" },
      { label: "Pair as Stereoscopic 3D Subclip", disabled: true },
      { sep: true },
      { label: "Select Events to End", action: "event-select-to-end" },
      { label: "Select All After Cursor", action: "event-select-after-cursor" },
      {
        label: "Insert/Remove Envelope",
        submenu: [
          { label: "Velocity", check: true, action: "event-env-velocity" },
          { label: "Opacity", check: true, action: "event-env-opacity" },
          { label: "Fade to Color", check: true, action: "event-env-fade-color" },
          { label: "Transition Progress", check: true, disabled: true },
        ],
      },
      { sep: true },
      {
        label: "Switches",
        submenu: [
          { label: "Mute", check: true, action: "event-switch-mute" },
          { label: "Lock", check: true, action: "event-switch-lock" },
          { label: "Loop", check: true, action: "event-switch-loop" },
          { label: "Maintain Aspect Ratio", check: true, checked: true, action: "event-switch-aspect" },
          { label: "Reduce Interlace Flicker", check: true, action: "event-switch-interlace" },
          { label: "Resample", check: true, action: "event-switch-resample" },
          { label: "Disable Resample", check: true, action: "event-switch-disable-resample" },
        ],
      },
      {
        label: "Take",
        submenu: [
          { label: "Active Take Information", check: true, checked: true, action: "event-take-info" },
          { sep: true },
          { label: "Next Take", action: "event-take-next" },
          { label: "Previous Take", action: "event-take-prev" },
          { sep: true },
          { label: "Delete Take", disabled: true },
        ],
      },
      {
        label: "Group",
        submenu: [
          { label: "Group", shortcut: "G", action: "event-group" },
          { label: "Ungroup", shortcut: "U", action: "event-ungroup" },
          { sep: true },
          { label: "Ignore Event Grouping", check: true, action: "event-ignore-grouping" },
          { label: "Clear Group", action: "event-clear-group", disabled: true },
          { label: "Select Events in Group", action: "event-select-group", disabled: true },
        ],
      },
      {
        label: "Stream",
        submenu: [
          { label: "Stream 0", radio: true, checked: true, action: "event-stream-0" },
          { label: "Stream 1", radio: true, action: "event-stream-1" },
        ],
      },
      {
        label: "Synchronize",
        submenu: [
          { label: "By Timecode", action: "event-sync-timecode" },
          { label: "By Sound", action: "event-sync-sound" },
          { label: "Align Start Times", action: "event-sync-align-start" },
        ],
      },
      { label: "Create Sync Link with Selected Events", action: "event-create-sync-link" },
      {
        label: "Sync Link",
        submenu: [
          { label: "Select Linked Events", action: "event-select-linked", disabled: true },
          { label: "Unlink", action: "event-unlink", disabled: true },
        ],
      },
      { sep: true },
      { label: "Properties...", action: "event-properties" },
    ],
    "audio-event": [
      { label: "Open in <u>T</u>rimmer", action: "event-open-trimmer" },
      { label: "Open Parent Media in Trimmer", disabled: true },
      { sep: true },
      {
        label: "Open in Audio <u>E</u>ditor",
        svg: SVG.audioNote,
        action: "event-open-audio-editor",
      },
      { label: "Open Copy in Audio Editor", action: "event-open-audio-editor-copy" },
      { label: "Edit Source Project", disabled: true },
      { sep: true },
      {
        label: "Create Nested Timeline",
        shortcut: "Alt+C",
        svg: SVG.nestedPlus,
        action: "event-create-nested",
      },
      {
        label: "Open Nested Timeline",
        shortcut: "Alt+N",
        svg: SVG.nestedOpen,
        action: "event-open-nested",
        disabled: true,
      },
      { label: "Select <u>i</u>n Project Media list", action: "event-select-media" },
      { sep: true },
      {
        label: "Beat <u>D</u>etection",
        svg: SVG.beatDetect,
        action: "event-beat-detect",
      },
      {
        label: "Tempo Detection",
        svg: SVG.tempoDetect,
        action: "event-tempo-detect",
      },
      { sep: true },
      {
        label: "Audio Event FX...",
        action: "audio-event-fx",
        svg: SVG.fxCircle,
      },
      { label: "Apply Non-Real-Time Event FX...", action: "event-nrt-fx" },
      { sep: true },
      { label: "Cu<u>t</u>", shortcut: "Ctrl+X", svg: SVG.cut, action: "event-cut" },
      { label: "<u>C</u>opy", shortcut: "Ctrl+C", svg: SVG.copy, action: "event-copy" },
      { label: "Paste Event Attributes", action: "event-paste-attrs", disabled: true },
      { label: "Selectively Paste Event Attributes", action: "event-paste-attrs-selective", disabled: true },
      { sep: true },
      { label: "<u>D</u>elete", shortcut: "Delete", svg: SVG.del, action: "event-delete" },
      { label: "T<u>r</u>im", shortcut: "Ctrl+T", svg: SVG.trim, disabled: true },
      { label: "Trim Start", shortcut: "Alt+[", svg: SVG.trimStart, action: "event-trim-start" },
      { label: "Trim End", shortcut: "Alt+]", svg: SVG.trimEnd, action: "event-trim-end" },
      { label: "<u>S</u>plit", shortcut: "S", svg: SVG.split, action: "event-split" },
      { label: "Event Heal", disabled: true },
      { label: "Close Gaps", disabled: true },
      { sep: true },
      { label: "Add Missing Stream for Selected Event", disabled: true },
      { label: "Create Su<u>b</u>clip", action: "event-create-subclip" },
      { label: "Reverse", action: "event-reverse" },
      { sep: true },
      { label: "Select Events to End", action: "event-select-to-end" },
      { label: "Select All After Cursor", action: "event-select-after-cursor" },
      { sep: true },
      {
        label: "Switc<u>h</u>es",
        submenu: [
          { label: "Mute", check: true, action: "event-switch-mute" },
          { label: "Lock", check: true, action: "event-switch-lock" },
          { label: "Loop", check: true, action: "event-switch-loop" },
          { label: "Normalize", check: true, action: "event-switch-normalize" },
          { label: "Invert Phase", check: true, action: "event-switch-invert" },
        ],
      },
      {
        label: "Take",
        submenu: [
          { label: "Active Take Information", check: true, checked: true, action: "event-take-info" },
          { sep: true },
          { label: "Next Take", action: "event-take-next" },
          { label: "Previous Take", action: "event-take-prev" },
          { sep: true },
          { label: "Delete Take", disabled: true },
        ],
      },
      {
        label: "Group",
        submenu: [
          { label: "Group", shortcut: "G", action: "event-group" },
          { label: "Ungroup", shortcut: "U", action: "event-ungroup" },
          { sep: true },
          { label: "Ignore Event Grouping", check: true, action: "event-ignore-grouping" },
          { label: "Clear Group", action: "event-clear-group", disabled: true },
          { label: "Select Events in Group", action: "event-select-group", disabled: true },
        ],
      },
      {
        label: "Stream",
        submenu: [
          { label: "Stream 0", radio: true, checked: true, action: "event-stream-0" },
          { label: "Stream 1", radio: true, action: "event-stream-1" },
        ],
      },
      {
        label: "Channels",
        submenu: [
          { label: "Channels...", action: "audio-channels" },
          { sep: true },
          { label: "Left Only", radio: true, action: "event-ch-left" },
          { label: "Right Only", radio: true, action: "event-ch-right" },
          { label: "Both (Stereo)", radio: true, checked: true, action: "event-ch-stereo" },
          { label: "Combine to Mono", radio: true, action: "event-ch-mono" },
        ],
      },
      {
        label: "Synchronize",
        submenu: [
          { label: "By Timecode", action: "event-sync-timecode" },
          { label: "By Sound", action: "event-sync-sound" },
          { label: "Align Start Times", action: "event-sync-align-start" },
        ],
      },
      { label: "Create Sync Link with Selected Events", action: "event-create-sync-link" },
      {
        label: "Sync Link",
        submenu: [
          { label: "Select Linked Events", action: "event-select-linked", disabled: true },
          { label: "Unlink", action: "event-unlink", disabled: true },
        ],
      },
      { sep: true },
      { label: "<u>P</u>roperties...", action: "event-properties" },
    ],
    "video-track-empty": [
      { label: "Insert Empty Event", shortcut: "", ico: "" },
      { label: "Insert Text Media...", shortcut: "", ico: "" },
      { sep: true },
      { label: "Paste", shortcut: "Ctrl+V", ico: "📎" },
      { sep: true },
      { label: "Select All on Track", shortcut: "", ico: "" },
      { label: "Track FX...", shortcut: "", ico: "", action: "track-fx" },
      { label: "Insert/Remove Envelope", shortcut: "", ico: "▸" },
    ],
    "audio-track-empty": [
      { label: "Insert Empty Event", shortcut: "", ico: "" },
      { sep: true },
      { label: "Paste", shortcut: "Ctrl+V", ico: "📎" },
      { sep: true },
      { label: "Select All on Track", shortcut: "", ico: "" },
      { label: "Insert/Remove Envelope", shortcut: "", ico: "▸" },
      { label: "Track FX...", shortcut: "", ico: "", action: "track-fx" },
      { label: "Record Input...", shortcut: "", ico: "" },
    ],
    "video-track-header": [
      { label: "Insert Video Track", action: "track-insert-video" },
      { label: "Duplicate Track", action: "track-duplicate" },
      { label: "Delete Track", action: "track-delete" },
      { sep: true },
      { label: "Track Name...", action: "track-rename" },
      { label: "Track Color...", action: "track-color" },
      { sep: true },
      { label: "Track FX...", action: "track-fx" },
      { label: "Track Motion...", action: "track-motion" },
      { label: "Mute", action: "track-mute", check: true, checked: false },
      { label: "Solo", action: "track-solo", check: true, checked: false },
    ],
    "audio-track-header": [
      { label: "Insert Audio Track", action: "track-insert-audio" },
      { label: "Duplicate Track", action: "track-duplicate" },
      { label: "Delete Track", action: "track-delete" },
      { sep: true },
      { label: "Track Name...", action: "track-rename" },
      { label: "Track Color...", action: "track-color" },
      { sep: true },
      { label: "Track FX...", action: "track-fx" },
      { label: "Mute", action: "track-mute", check: true, checked: false },
      { label: "Solo", action: "track-solo", check: true, checked: false },
      { label: "Record Arm", action: "track-record" },
    ],
    "timeline-marker": [
      { label: "<u>G</u>o To", action: "marker-goto" },
      { label: "<u>R</u>ename", action: "marker-rename" },
      { sep: true },
      { label: "<u>D</u>elete", action: "marker-delete" },
    ],
    "video-event-more": [
      { label: "Active Take Information", check: true, checked: true },
      { label: "Playback Rate" },
      { label: "Freeze Frame at Cursor" },
      { label: "Selectively Paste Event Attributes", disabled: true },
      { label: "Event Headers" },
      { label: "Event Length" },
      { label: "Color Grading" },
      { label: "Media FX" },
      { label: "Event Handles" },
      { label: "Motion Tracking" },
      { label: "Detect Scenes and Split" },
      { sep: true },
      { label: "Edit Visible Button Set..." },
    ],
    "audio-event-more": [
      { label: "Active Take Information", check: true, checked: true },
      { label: "Event Headers" },
      { label: "Event Length" },
      { label: "Event Handles" },
      { label: "Normalize" },
      { label: "Auto Normalize" },
      { sep: true },
      { label: "Edit Visible Button Set..." },
    ],
    "timeline-empty": [
      {
        label: "Open...",
        shortcut: "Ctrl+O",
        svg: '<path d="M1.5 3.5h5l1.2 1.5H14.5v8H1.5v-9.5z" fill="none" stroke="currentColor" stroke-width="1.2"/>',
      },
      {
        label: "Insert Audio Track",
        shortcut: "Ctrl+Q",
        action: "track-insert-audio",
        svg: '<path d="M2 8h1.2v2H2V8zm2.2-3h1.2v8H4.2V5zm2.2 1.5h1.2v5H6.4V6.5zm2.2-2h1.2v9H8.6V4.5zm2.2 3h1.2v3h-1.2V7.5zm2.2-1.5H14v6h-1.2V6z" fill="currentColor"/>',
      },
      {
        label: "Insert Video Track",
        shortcut: "Ctrl+Shift+Q",
        action: "track-insert-video",
        svg: '<rect x="1.5" y="2.5" width="13" height="11" rx="1" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M1.5 5.5h13M1.5 10.5h13M5.5 2.5v11M10.5 2.5v11" stroke="currentColor" stroke-width="1"/>',
      },
      {
        label: "Insert Adjustment Track",
        shortcut: "Ctrl+Alt+Shift+Q",
        action: "track-insert-video",
        svg: '<rect x="2" y="2" width="12" height="12" rx="1" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M4 12L12 4" stroke="currentColor" stroke-width="1.2"/><path d="M9.5 3.5h3v3M6.5 12.5h-3v-3" stroke="currentColor" stroke-width="1.1" fill="none"/>',
      },
      { sep: true },
      {
        label: "Preferences...",
        shortcut: "",
        action: "preferences",
        svg: '<circle cx="8" cy="8" r="2.2" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6M3.3 3.3l1.1 1.1M11.6 11.6l1.1 1.1M12.7 3.3l-1.1 1.1M4.4 11.6l-1.1 1.1" fill="none" stroke="currentColor" stroke-width="1.2"/>',
      },
    ],
    "time-display": [
      { label: "Time at Cursor", radio: true, checked: true },
      { label: "MIDI Timecode In", radio: true },
      { label: "MIDI Timecode Out", radio: true },
      { label: "MIDI Clock Out", radio: true },
      { sep: true },
      {
        label: "Time Format",
        submenu: [
          { label: "Time", check: true },
          { label: "Seconds", check: true },
          { label: "Time & Frames", check: true, checked: true },
          { label: "Absolute Frames", check: true },
          { label: "Measures & Beats", check: true },
          { label: "SMPTE Drop (29.97 fps)", check: true },
          { label: "SMPTE Non-Drop (29.97 fps)", check: true },
          { label: "SMPTE 30", check: true },
          { label: "SMPTE EBU (25 fps)", check: true },
          { label: "SMPTE Film Sync (24 fps)", check: true },
          { label: "Audio Samples", check: true },
        ],
      },
      { sep: true },
      {
        label: "Text Color",
        submenu: [
          { label: "White", check: true, checked: true },
          { label: "Yellow", check: true },
          { label: "Cyan", check: true },
          { label: "Green", check: true },
          { label: "Custom...", check: true },
        ],
      },
      {
        label: "Background Color",
        submenu: [
          { label: "Black", check: true },
          { label: "Dark Gray", check: true, checked: true },
          { label: "Navy", check: true },
          { label: "Custom...", check: true },
        ],
      },
    ],
    "timeline-ruler": [
      { label: "Samples", radio: true },
      { label: "Time", radio: true },
      { label: "Seconds", radio: true },
      { label: "Time & Frames", radio: true },
      { label: "Absolute Frames", radio: true },
      { label: "Measures & Beats", radio: true, checked: true },
      { label: "Feet and Frames 16mm (40 fpf)", radio: true },
      { label: "Feet and Frames 35mm (16 fpf)", radio: true },
      { label: "SMPTE Film Sync IVTC (23.976 fps, Video)", radio: true },
      { label: "SMPTE Film Sync (24 fps, Film)", radio: true },
      { label: "SMPTE EBU (25 fps, Video)", radio: true },
      { label: "SMPTE Non-Drop (29.97 fps, Video)", radio: true },
      { label: "SMPTE Drop (29.97 fps, Video)", radio: true },
      { label: "SMPTE 30 (30 fps, Audio)", radio: true },
      { label: "Audio CD Time", radio: true, disabled: true },
      { sep: true },
      { label: "Set Time at Cursor...", shortcut: "", ico: "" },
      { label: "Set Project Tempo...", shortcut: "", ico: "" },
    ],
    preview: [
      { label: "Video Output Color Grading...", shortcut: "", ico: "" },
      { sep: true },
      { label: "Default Background", radio: true, checked: true },
      { label: "Black Background", radio: true },
      { label: "White Background", radio: true },
      { sep: true },
      { label: "Simulate Device Aspect Ratio", check: true, checked: true },
      { label: "Enable Preview Scaling", check: true, checked: true },
      { label: "Adjust Size and Quality for Optimal Playback", check: true, checked: true },
      { sep: true },
      { label: "Show Toolbar", check: true, checked: true },
      { label: "Show Status Bar", check: true, checked: true },
      { label: "Show Transport Bar", check: true, checked: true },
      { sep: true },
      { label: "Video Preview Preferences...", shortcut: "", ico: "" },
      { label: "Preview Device Preferences...", shortcut: "", ico: "" },
      { sep: true },
      {
        label: "Preview Quality",
        submenu: [
          { label: "Draft", shortcut: "", ico: "" },
          { label: "Preview", shortcut: "", ico: "" },
          { label: "Good", shortcut: "", ico: "" },
          {
            label: "Best",
            submenu: [
              { label: "Auto", check: true, checked: true },
              { label: "Full", check: true },
              { label: "Half", check: true },
              { label: "Quarter", check: true },
            ],
          },
        ],
      },
      {
        label: "Display Frame Rate",
        submenu: [
          { label: "Full", check: true, checked: true },
          { label: "Half", check: true },
          { label: "Quarter", check: true },
        ],
      },
      { sep: true },
      { label: "Copy Frame to Clipboard", shortcut: "", ico: "" },
      { label: "Save Frame to File...", shortcut: "", ico: "" },
    ],
  };

  function markIco(it) {
    if (it.svg) {
      return '<svg class="menu-ico-svg" viewBox="0 0 16 16" aria-hidden="true">' + it.svg + "</svg>";
    }
    if (it.radio) return it.checked ? "●" : "";
    if (it.check) return it.checked ? "✓" : "";
    return it.ico || "";
  }

  function buildMenuItems(items) {
    return items
      .map((it) => {
        if (it.sep) return '<div class="dropdown-menu__sep"></div>';
        const hasSub = Array.isArray(it.submenu) && it.submenu.length;
        const classes = ["dropdown-menu__item"];
        if (hasSub) classes.push("has-submenu");
        if (it.checked) classes.push("is-checked");
        if (it.disabled) classes.push("is-disabled");
        let html =
          '<div class="' +
          classes.join(" ") +
          '"' +
          (it.disabled ? ' aria-disabled="true"' : "") +
          (it.action ? ' data-action="' + it.action + '"' : "") +
          ">" +
          '<span class="ico">' +
          markIco(it) +
          "</span>" +
          '<span class="label">' +
          it.label +
          "</span>";
        if (hasSub) {
          html += '<span class="shortcut submenu-arrow">▸</span>';
          html += '<div class="context-submenu">' + buildMenuItems(it.submenu) + "</div>";
        } else {
          html += '<span class="shortcut">' + (it.shortcut || "") + "</span>";
        }
        html += "</div>";
        return html;
      })
      .join("");
  }

  function hideFlyout(submenu) {
    if (!submenu) return;
    submenu.classList.remove("is-visible");
    submenu.style.display = "";
    submenu.style.position = "";
    submenu.style.left = "";
    submenu.style.top = "";
    submenu.style.right = "";
    submenu.style.bottom = "";
    submenu.style.visibility = "";
  }

  function closeAllSubmenus(scope) {
    const root = scope || document;
    root.querySelectorAll(".has-submenu.is-open").forEach((el) => {
      el.classList.remove("is-open");
      hideFlyout(el.querySelector(":scope > .context-submenu"));
    });
    root.querySelectorAll(".context-submenu.is-visible").forEach((sub) => hideFlyout(sub));
  }

  function closeAllDropdowns() {
    closeAllSubmenus();
    document.querySelectorAll(".menu-item.is-open").forEach((el) => el.classList.remove("is-open"));
    document.querySelectorAll(".dropdown-menu.is-open").forEach((el) => el.classList.remove("is-open"));
  }

  function closeContextMenu() {
    const cm = document.getElementById("context-menu");
    if (cm) {
      closeAllSubmenus(cm);
      cm.classList.remove("is-open");
    }
  }

  function positionFlyout(parentItem, submenu) {
    // Fixed escapes .app { overflow:hidden }; overlap avoids a hover gap that closes the flyout.
    const pr = parentItem.getBoundingClientRect();
    submenu.classList.add("is-visible");
    submenu.style.position = "fixed";
    submenu.style.visibility = "hidden";
    submenu.style.display = "block";
    const sw = submenu.offsetWidth || 300;
    const sh = submenu.offsetHeight || 160;
    let left = pr.right - 6;
    let top = pr.top - 4;
    if (left + sw > window.innerWidth - 8) {
      left = Math.max(8, pr.left - sw + 6);
    }
    if (top + sh > window.innerHeight - 8) {
      top = Math.max(8, window.innerHeight - 8 - sh);
    }
    submenu.style.left = left + "px";
    submenu.style.top = top + "px";
    submenu.style.right = "auto";
    submenu.style.bottom = "auto";
    submenu.style.visibility = "";
  }

  function wireSubmenus(root) {
    root.querySelectorAll(".has-submenu").forEach((item) => {
      const sub = item.querySelector(":scope > .context-submenu");
      if (!sub || item.dataset.subWired === "1") return;
      item.dataset.subWired = "1";
      hideFlyout(sub);
      let closeTimer = null;

      function openSub() {
        if (closeTimer) {
          clearTimeout(closeTimer);
          closeTimer = null;
        }
        const parent = item.parentElement;
        if (parent) {
          parent.querySelectorAll(":scope > .has-submenu.is-open").forEach((sib) => {
            if (sib === item) return;
            sib.classList.remove("is-open");
            hideFlyout(sib.querySelector(":scope > .context-submenu"));
          });
        }
        item.classList.add("is-open");
        positionFlyout(item, sub);
      }

      function closeSubNow() {
        if (closeTimer) {
          clearTimeout(closeTimer);
          closeTimer = null;
        }
        item.classList.remove("is-open");
        hideFlyout(sub);
      }

      function scheduleClose() {
        if (closeTimer) clearTimeout(closeTimer);
        closeTimer = setTimeout(closeSubNow, 160);
      }

      item.addEventListener("mouseenter", openSub);
      item.addEventListener("mouseleave", (e) => {
        // relatedTarget can be null when moving to a fixed flyout (moved out of DOM tree).
        if (e.relatedTarget && (item.contains(e.relatedTarget) || sub.contains(e.relatedTarget))) {
          return;
        }
        scheduleClose();
      });
      sub.addEventListener("mouseenter", openSub);
      sub.addEventListener("mouseleave", (e) => {
        if (e.relatedTarget && (item.contains(e.relatedTarget) || sub.contains(e.relatedTarget))) {
          return;
        }
        scheduleClose();
      });
    });
  }

  function openContextMenu(type, x, y, targetEl) {
    let items = CONTEXT_MENUS[type];
    if (!items) return;

    if ((type === "video-track-header" || type === "audio-track-header") && targetEl) {
      const header = targetEl.closest?.(".track-header") || targetEl;
      items = items.map((it) => {
        if (it.action === "track-mute") {
          return Object.assign({}, it, {
            check: true,
            checked: !!header.querySelector?.('.ms-btn[title="Mute"].is-active'),
          });
        }
        if (it.action === "track-solo") {
          return Object.assign({}, it, {
            check: true,
            checked: !!header.querySelector?.('.ms-btn[title="Solo"].is-active'),
          });
        }
        if (it.action === "track-record") {
          return Object.assign({}, it, {
            check: true,
            checked: !!header.querySelector?.(".track-mini-btn--rec.is-armed, .track-mini-btn.is-armed"),
          });
        }
        return it;
      });
    }

    if ((type === "video-event" || type === "audio-event") && targetEl) {
      const ev = targetEl.closest?.(".event") || targetEl;
      if (ev?.classList?.contains("event") && window.VegasEventMenuPatch?.patch) {
        items = window.VegasEventMenuPatch.patch(items, ev);
      }
    }

    let cm = document.getElementById("context-menu");
    if (!cm) {
      cm = document.createElement("div");
      cm.id = "context-menu";
      cm.className = "context-menu";
      document.body.appendChild(cm);
      cm.addEventListener("click", (e) => {
        e.stopPropagation();
        const item = e.target.closest(".dropdown-menu__item");
        if (!item || item.classList.contains("is-disabled") || item.classList.contains("has-submenu")) {
          return;
        }
        const action = item.getAttribute("data-action") || "";
        const label = (item.querySelector(".label")?.textContent || "").trim();
        const target = cm._contextTarget || null;
        const ctxType = cm.dataset.contextType || "";
        closeContextMenu();
        document.dispatchEvent(
          new CustomEvent("vegas:context-action", {
            detail: { type: ctxType, action, label, target },
          })
        );
      });
      cm.addEventListener("contextmenu", (e) => e.preventDefault());
    }
    cm.dataset.contextType = type;
    cm._contextTarget = targetEl || null;
    cm.classList.toggle(
      "context-menu--compact",
      type === "timeline-marker" || type === "video-event-more" || type === "audio-event-more"
    );
    cm.classList.toggle("context-menu--event", type === "video-event" || type === "audio-event");
    cm.innerHTML = buildMenuItems(items);
    cm.classList.add("is-open");
    wireSubmenus(cm);

    const pad = 4;
    cm.style.left = "0px";
    cm.style.top = "0px";
    const w = cm.offsetWidth || 300;
    const h = cm.offsetHeight || 360;
    let left = x;
    let top = y;
    if (left + w > window.innerWidth - pad) left = Math.max(pad, window.innerWidth - w - pad);
    if (top + h > window.innerHeight - pad) top = Math.max(pad, window.innerHeight - h - pad);
    cm.style.left = left + "px";
    cm.style.top = top + "px";
  }

  function initMenuBar() {
    const bar = document.querySelector(".menu-bar");
    if (!bar || bar.dataset.menusInit) return;
    bar.dataset.menusInit = "1";

    Object.keys(MENU_DATA).forEach((name) => {
      let btn = bar.querySelector('[data-menu="' + name + '"]');
      if (!btn) {
        btn = document.createElement("button");
        btn.type = "button";
        btn.className = "menu-item";
        btn.dataset.menu = name;
        btn.textContent = name;
        bar.appendChild(btn);
      }

      let dd = btn.querySelector(".dropdown-menu");
      if (!dd) {
        dd = document.createElement("div");
        dd.className = "dropdown-menu" + (name === "File" ? " dropdown-menu--file" : "");
        dd.innerHTML = buildMenuItems(MENU_DATA[name]);
        btn.appendChild(dd);
        wireSubmenus(dd);
        dd.dataset.subWired = "1";
      } else {
        if (name === "File") dd.classList.add("dropdown-menu--file");
        if (!dd.dataset.subWired) {
          wireSubmenus(dd);
          dd.dataset.subWired = "1";
        }
      }

      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        const item = e.target.closest(".dropdown-menu__item");
        if (item) {
          if (item.classList.contains("is-disabled") || item.classList.contains("has-submenu")) {
            return;
          }
          const action = item.getAttribute("data-action") || "";
          const label = (item.querySelector(".label")?.textContent || "").trim();
          closeAllDropdowns();
          closeContextMenu();
          document.dispatchEvent(
            new CustomEvent("vegas:menu-action", {
              detail: { action, label, menu: name },
            })
          );
          return;
        }
        const wasOpen = btn.classList.contains("is-open");
        closeAllDropdowns();
        closeContextMenu();
        if (!wasOpen) {
          btn.classList.add("is-open");
          dd.classList.add("is-open");
          closeAllSubmenus(dd);
        }
      });

      btn.addEventListener("mouseenter", () => {
        // Keep File open while Import/Export flyout (fixed) is active.
        if (document.querySelector(".menu-item.is-open .has-submenu.is-open")) {
          return;
        }
        if (bar.querySelector(".menu-item.is-open") && !btn.classList.contains("is-open")) {
          closeAllDropdowns();
          btn.classList.add("is-open");
          dd.classList.add("is-open");
        }
      });
    });
  }

  function initContextTargets() {
    // Empty space under track controllers uses the same menu as empty timeline.
    document.querySelectorAll(".track-headers").forEach((el) => {
      if (!el.getAttribute("data-context")) el.setAttribute("data-context", "timeline-empty");
    });

    document.addEventListener("contextmenu", (e) => {
      const target = e.target.closest("[data-context]");
      if (!target) return;
      e.preventDefault();
      closeAllDropdowns();
      // Right-click on an event selects it + group (Vegas behavior).
      if (target.classList.contains("event") || target.closest?.(".event")) {
        const ev = target.classList.contains("event") ? target : target.closest(".event");
        const area = ev?.closest(".tracks-area");
        if (ev && area && !ev.classList.contains("is-selected")) {
          if (window.VegasEventGroup?.selectWithGroup) {
            window.VegasEventGroup.selectWithGroup(ev, area);
          } else {
            area.querySelectorAll(".event.is-selected").forEach((el) => el.classList.remove("is-selected"));
            ev.classList.add("is-selected");
            window.VegasTimelineSelect?.syncSelectionChrome?.();
          }
        }
      }
      openContextMenu(target.dataset.context, e.clientX, e.clientY, target);
    });
  }

  function initPanelTabs() {
    document.querySelectorAll("[data-tabs]").forEach((root) => {
      const bar = root.querySelector(".panel-tabs");
      if (!bar) return;
      const panels = root.querySelectorAll(".tab-panel");

      function chromeHtml() {
        return (
          '<span class="panel-tab__chrome">' +
          '<button type="button" class="panel-tab-ico" title="Maximize" aria-label="Maximize">' +
          '<svg viewBox="0 0 12 12" width="10" height="10" aria-hidden="true"><rect x="1.5" y="1.5" width="9" height="9" fill="none" stroke="currentColor" stroke-width="1.2"/></svg>' +
          "</button>" +
          '<button type="button" class="panel-tab-ico" title="Close" aria-label="Close">×</button>' +
          "</span>"
        );
      }

      function placeChrome(wrap) {
        bar.querySelectorAll(".panel-tab__chrome").forEach((el) => el.remove());
        bar.querySelectorAll(".panel-tab-wrap").forEach((w) => w.classList.remove("is-active"));
        if (!wrap) return;
        wrap.classList.add("is-active");
        wrap.insertAdjacentHTML("beforeend", chromeHtml());
      }

      bar.addEventListener("click", (e) => {
        if (e.target.closest(".panel-tab-ico")) {
          e.stopPropagation();
          return;
        }
        const tab = e.target.closest(".panel-tab");
        if (!tab || !bar.contains(tab)) return;
        const id = tab.dataset.tab;
        const wrap = tab.closest(".panel-tab-wrap");
        bar.querySelectorAll(".panel-tab").forEach((t) => t.classList.toggle("is-active", t === tab));
        panels.forEach((p) => p.classList.toggle("is-active", p.dataset.panel === id));
        placeChrome(wrap || tab.parentElement);
      });
    });
  }

  function initMuteSolo() {
    document.addEventListener("click", (e) => {
      const btn = e.target.closest(".ms-btn");
      if (!btn) return;
      // Track Mute/Solo handled in timeline-tracks.js
      if (btn.closest(".track-header") && (btn.title === "Mute" || btn.title === "Solo")) {
        return;
      }
      e.stopPropagation();
      btn.classList.toggle("is-active");
    });
  }

  document.addEventListener("click", () => {
    closeAllDropdowns();
    closeContextMenu();
  });

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      closeAllDropdowns();
      closeContextMenu();
    }
  });

  document.addEventListener("DOMContentLoaded", () => {
    initMenuBar();
    initContextTargets();
    initPanelTabs();
    initMuteSolo();
  });

  window.VegasMenus = {
    MENU_DATA,
    CONTEXT_MENUS,
    buildMenuItems,
    openContextMenu,
    closeAllDropdowns,
  };
})();
