/**
 * Shared chrome fragments for Vegas Pro HTML mockups.
 * Fills [data-chrome="main-toolbar"] and [data-chrome="timeline-tools"].
 */
(function () {
  const I = (title, path, activeOrOpts) => {
    const o =
      typeof activeOrOpts === "object" && activeOrOpts
        ? activeOrOpts
        : { active: !!activeOrOpts };
    const cls =
      "icon-btn" +
      (o.active ? " is-active" : "") +
      (o.disabled ? " is-disabled" : "") +
      (o.caret ? " icon-btn--dd" : "");
    return (
      `<button class="${cls}" type="button" title="${title}"` +
      (o.disabled ? " disabled" : "") +
      `><svg viewBox="0 0 16 16">${path}</svg>` +
      (o.caret ? '<span class="icon-btn__caret" aria-hidden="true">▾</span>' : "") +
      "</button>"
    );
  };

  const SEP = '<span class="toolbar-sep"></span>';

  const GEAR =
    '<path d="M6.4 1.6h3.2l.35 1.45 1.35.55 1.25-.9 2.25 2.25-.9 1.25.55 1.35 1.45.35v3.2l-1.45.35-.55 1.35.9 1.25-2.25 2.25-1.25-.9-1.35.55L9.6 14.4H6.4l-.35-1.45-1.35-.55-1.25.9L1.2 11.05l.9-1.25-.55-1.35L.1 8V4.8l1.45-.35.55-1.35-.9-1.25L3.45.6l1.25.9 1.35-.55zm1.6 3.9A2.5 2.5 0 108 10.9a2.5 2.5 0 000-5.4z" fill="none" stroke="currentColor" stroke-width="1.15" stroke-linejoin="round"/>';

  const MAIN_TOOLBAR = [
    I("New Project", '<path d="M3 2h7l3 3v9H3V2zm7 1v2h3"/>'),
    I("Open", '<path d="M1 4h5l1.5 2H15v8H1V4z"/>'),
    I("Save", '<path d="M2 2h9l3 3v9H2V2zm2 0v4h7V2M4 10h8v4H4z"/>'),
    I("Render As", '<path d="M2 3h12v8H8.5L7 13v-2H2V3zm2 2v4h8V5H4z"/>'),
    I("Project Properties", GEAR),
    SEP,
    I("Cut", '<circle cx="4" cy="3.5" r="1.8"/><circle cx="4" cy="12.5" r="1.8"/><path d="M5.5 4.5l6 7M5.5 11.5l6-7" fill="none" stroke="currentColor" stroke-width="1.2"/>'),
    I("Copy", '<path d="M5 2.5h8v10H5V2.5zm-2.5 2.5H5v9.5H2.5V5z"/>'),
    I("Paste", '<path d="M5 1.5h6v2.5H5V1.5zM3.5 4h9v10.5h-9V4zm2.5 3h4v1.2H6V7z"/>'),
    SEP,
    I("Undo", '<path d="M2.5 7.5C4 4.5 9.5 4 12 7v2h-1.8V8c-1.4-1.8-4.2-2-5.8-.2L6 9.5H2V5l.5 2.5z"/>'),
    I("Redo", '<path d="M13.5 7.5C12 4.5 6.5 4 4 7v2h1.8V8c1.4-1.8 4.2-2 5.8-.2L10 9.5h4V5l-.5 2.5z"/>'),
  ].join("");

  /* Vegas Pro 22 — bottom timeline toolbar (transport → group ops) */
  const TIMELINE_TOOLS = [
    '<div class="timeline-tools__ratecol">' +
      '<div class="status-rate" data-chrome="rate">' +
        '<label>Rate:</label>' +
        '<input type="range" min="0" max="400" value="100" aria-label="Playback rate" />' +
        '<span class="status-rate__val">1,00</span>' +
      "</div>" +
    "</div>" +
    '<div class="timeline-tools__rest">' +
    '<div class="timeline-tools__transport">' +
    I(
      "Record into Track",
      '<circle cx="8" cy="8" r="5.2" fill="#c42b1c"/><circle cx="8" cy="8" r="5.2" fill="none" stroke="#e05050" stroke-width="0.8"/>'
    ) +
    I(
      "Loop Playback",
      '<path d="M3.2 8a4.8 4.8 0 018.2-3.2M12.2 3.2v3h-3M12.8 8a4.8 4.8 0 01-8.2 3.2M3.8 12.8v-3h3" fill="none" stroke="currentColor" stroke-width="1.35" stroke-linecap="round"/>',
      true
    ) +
    I("Play from Start", '<path d="M2.2 3.2v9.6h1.4V3.2H2.2zm3.2 0l8.8 4.8-8.8 4.8V3.2z"/>') +
    I("Play", '<path d="M4 2.8l9.5 5.2L4 13.2V2.8z"/>') +
    I("Pause", '<path d="M3.2 2.8h3.2v10.4H3.2V2.8zm6.4 0h3.2v10.4H9.6V2.8z"/>') +
    I("Stop", '<path d="M3.4 3.4h9.2v9.2H3.4z"/>') +
    I("Go to Start", '<path d="M2.2 3.2v9.6h1.4V3.2H2.2zm11.6 0L6.2 8l7.6 4.8V3.2z"/>') +
    I("Go to End", '<path d="M12.4 3.2v9.6H14V3.2h-1.6zM2.2 3.2L9.8 8l-7.6 4.8V3.2z"/>') +
    I(
      "Previous Frame",
      '<path d="M10.8 3.2L5.2 8l5.6 4.8V3.2z"/><path d="M3.6 3.2v9.6H2.2V3.2h1.4zm1.8 0v9.6H4V3.2h1.4z"/>'
    ) +
    I(
      "Next Frame",
      '<path d="M5.2 3.2L10.8 8 5.2 12.8V3.2z"/><path d="M12.4 3.2v9.6H14V3.2h-1.6zm-1.8 0v9.6h1.4V3.2h-1.4z"/>'
    ) +
    "</div>" +
    SEP +
    '<div class="timeline-tools__edit">' +
    I(
      "Normal Edit Tool",
      '<path d="M3.2 1.6l9.2 5.6-3.4 1.15 2.35 5.45-2.15.9-2.35-5.45-3.35 2.1z"/>',
      { active: true, caret: true }
    ) +
    I(
      "Envelope Edit Tool",
      '<path d="M2 12.5 L5 4.5 L9 10 L14 3.5" fill="none" stroke="currentColor" stroke-width="1.35"/><rect x="1.2" y="11.5" width="2.2" height="2.2" rx="0.3"/><rect x="4.2" y="3.5" width="2.2" height="2.2" rx="0.3"/><rect x="8.2" y="9" width="2.2" height="2.2" rx="0.3"/><rect x="13" y="2.5" width="2.2" height="2.2" rx="0.3"/>'
    ) +
    I(
      "Selection Edit Tool",
      '<rect x="2.5" y="2.5" width="11" height="11" fill="none" stroke="currentColor" stroke-width="1.25" stroke-dasharray="2 1.5"/>'
    ) +
    I(
      "Zoom Edit Tool",
      '<circle cx="7" cy="7" r="4.2" fill="none" stroke="currentColor" stroke-width="1.35"/><path d="M10.2 10.2l3.6 3.6" stroke="currentColor" stroke-width="1.45" fill="none" stroke-linecap="round"/>'
    ) +
    "</div>" +
    SEP +
    '<div class="timeline-tools__ops">' +
    I(
      "Delete",
      '<path d="M4 4l8 8M12 4L4 12" stroke="currentColor" stroke-width="1.6" fill="none" stroke-linecap="round"/>',
      { disabled: true }
    ) +
    I(
      "Trim",
      '<path d="M8 2.5v11M3.5 5.5h4M8.5 10.5h4" stroke="currentColor" stroke-width="1.35" fill="none" stroke-linecap="round"/>',
      { disabled: true }
    ) +
    I(
      "Trim Start",
      '<path d="M6 3v10M6 3h5M6 13h5M3.5 5.5v5" stroke="currentColor" stroke-width="1.3" fill="none" stroke-linecap="round"/>',
      { disabled: true }
    ) +
    I(
      "Trim End",
      '<path d="M10 3v10M10 3H5M10 13H5M12.5 5.5v5" stroke="currentColor" stroke-width="1.3" fill="none" stroke-linecap="round"/>',
      { disabled: true }
    ) +
    I(
      "Split",
      '<path d="M8 2.5v11M3 6.5h3.5M9.5 9.5H13" stroke="currentColor" stroke-width="1.35" fill="none" stroke-linecap="round"/>',
      { disabled: true }
    ) +
    I(
      "Heal",
      '<path d="M3 8h10M5.5 5.5L3 8l2.5 2.5M10.5 5.5L13 8l-2.5 2.5" stroke="currentColor" stroke-width="1.3" fill="none" stroke-linecap="round" stroke-linejoin="round"/>',
      { disabled: true }
    ) +
    I(
      "Lock",
      '<path d="M5 7V5.6a3 3 0 016 0V7h1.4v7.2H3.6V7H5zm1.4 0h3.2V5.6a1.6 1.6 0 00-3.2 0V7z"/>',
      true
    ) +
    "</div>" +
    SEP +
    '<div class="timeline-tools__markers">' +
    I(
      "Insert Marker",
      '<path d="M4.2 2.2h6.2v7.2L7.3 12.4 4.2 9.4V2.2z" fill="#e0a020"/><path d="M5.6 2.2v12.5" stroke="#e0a020" stroke-width="1.25"/>'
    ) +
    I(
      "Insert Region",
      '<path d="M2.2 2.2h4.2v6.4L4.3 11 2.2 8.6V2.2z" fill="#e0a020"/><path d="M13.8 2.2H9.6v6.4L11.7 11l2.1-2.4V2.2z" fill="#e0a020"/>'
    ) +
    "</div>" +
    SEP +
    '<div class="timeline-tools__modes">' +
    I(
      "Enable Snapping",
      '<path d="M8.2 2.2c-1.7 0-3 1.2-3.3 2.8H3.4v2.2h1.1c.15.55.4 1.05.75 1.45L3.4 10.7l1.5 1.5 1.9-2.05c.55.4 1.2.65 1.9.7V13.8h2.2v-2.9c.7-.1 1.35-.35 1.9-.75l1.95 2.1 1.5-1.5-1.85-2c.35-.4.6-.9.75-1.45h1.15V5h-1.55C12.9 3.4 11.55 2.2 9.8 2.2H8.2zm.8 2.2c1.1 0 1.9.8 1.9 1.85S10.1 8.1 9 8.1 7.1 7.3 7.1 6.25 7.9 4.4 9 4.4z" fill="currentColor"/>',
      true
    ) +
    I(
      "Automatic Crossfades",
      '<path d="M2.5 2.5h11v11h-11z" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M2.5 13.5L13.5 2.5" stroke="currentColor" stroke-width="1.25"/><path d="M2.5 2.5h11L2.5 13.5z" fill="currentColor" opacity=".4"/>',
      true
    ) +
    I(
      "Auto Ripple",
      '<rect x="2.2" y="3.2" width="4.2" height="9.6" rx="0.4" fill="#4a9be8"/><path d="M8 8h5.5M11.2 5.6L14 8l-2.8 2.4" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/>',
      { caret: true }
    ) +
    I(
      "Lock Envelopes",
      '<path d="M3.2 9.2h4.2v4.2H3.2zM8.6 9.2h4.2v4.2H8.6z" fill="none" stroke="currentColor" stroke-width="1.15"/><path d="M5.5 5.2V4.3a2.5 2.5 0 015 0v.9h1.2v2.2H4.3V5.2H5.5zm1.3 0h2.4V4.3a1.2 1.2 0 00-2.4 0v.9z"/><path d="M7.4 11.3h1.2" stroke="currentColor" stroke-width="1.2"/>',
      true
    ) +
    I(
      "Ignore Event Grouping",
      '<rect x="2.5" y="3.5" width="6.5" height="5.5" rx="0.4" fill="none" stroke="currentColor" stroke-width="1.15"/><rect x="7" y="7" width="6.5" height="5.5" rx="0.4" fill="none" stroke="currentColor" stroke-width="1.15"/><path d="M10.5 2.5l3.2 2-1.15.4.7 1.85-.95.35-.7-1.85-1.2.75z"/>'
    ) +
    I(
      "Video Output Color Grading",
      '<circle cx="6.2" cy="7.2" r="3.1" fill="#c44"/><circle cx="9.8" cy="7.2" r="3.1" fill="#4a4"/><circle cx="8" cy="10.2" r="3.1" fill="#44a"/><circle cx="6.2" cy="7.2" r="3.1" fill="none" stroke="#222" stroke-width="0.4"/><circle cx="9.8" cy="7.2" r="3.1" fill="none" stroke="#222" stroke-width="0.4"/><circle cx="8" cy="10.2" r="3.1" fill="none" stroke="#222" stroke-width="0.4"/>'
    ) +
    "</div>" +
    SEP +
    '<div class="timeline-tools__group">' +
    I(
      "Paste Attributes",
      '<path d="M4 3.5h7.5v9H4z" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M11 7.5h3.2M12.5 6l2 1.5-2 1.5" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"/>',
      { disabled: true }
    ) +
    I(
      "Copy Attributes",
      '<path d="M4.5 3.5H12v9H4.5z" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M2.2 7.5H5M3.5 6L1.5 7.5 3.5 9" fill="none" stroke="currentColor" stroke-width="1.2" stroke-linecap="round"/>',
      { disabled: true }
    ) +
    I(
      "Group",
      '<path d="M3.5 3.5h4v4h-4zM8.5 8.5h4v4h-4z" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M7 7.5h2M8 6.5v2" stroke="currentColor" stroke-width="1.2"/>',
      { disabled: true }
    ) +
    "</div>" +
    '<span class="timeline-tools__spacer"></span>' +
    '<span class="timecode" data-chrome="timecode" data-context="time-display">1.1.000</span>' +
    '<div class="timeline-tools__mini-vu" title="Master">' +
      '<div class="timeline-tools__mini-vu-ch"><span style="height:35%"></span></div>' +
      '<div class="timeline-tools__mini-vu-ch"><span style="height:28%"></span></div>' +
    "</div>" +
    '<span class="timeline-tools__record" data-chrome="record-time">Record Time (2 channels): 41:06:27:05</span>' +
    "</div>",
  ].join("");

  function fill(selector, html) {
    document.querySelectorAll(selector).forEach((el) => {
      const keepContext = el.getAttribute("data-context");
      el.innerHTML = html;
      if (keepContext) el.setAttribute("data-context", keepContext);
      const tc = el.getAttribute("data-timecode");
      if (tc) {
        const node = el.querySelector("[data-chrome='timecode']");
        if (node) node.textContent = tc;
      }
      const rt = el.getAttribute("data-record-time");
      if (rt) {
        const node = el.querySelector("[data-chrome='record-time']");
        if (node) node.textContent = rt;
      }
      const rate = el.querySelector(".status-rate");
      if (rate) {
        const input = rate.querySelector("input");
        const val = rate.querySelector(".status-rate__val");
        if (input && val) {
          input.addEventListener("input", () => {
            val.textContent = (input.value / 100).toFixed(2).replace(".", ",");
          });
        }
      }
    });
  }

  function fmtMeta(s) {
    return String(s || "").replace(/,/g, ";").replace(/(\d)\.(\d)/g, "$1,$2");
  }

  function splitPreviewMeta(preview) {
    const raw = String(preview || "960x540x32; 59,940p");
    const parts = raw.split(/;\s*/);
    return {
      res: parts[0] || raw,
      rest: parts.slice(1).join("; "),
    };
  }

  function enhancePreviewToolbar() {
    const qualityLevels = ["Draft", "Preview", "Good", "Best"];
    const qualityRes = [
      { label: "Auto", key: "A", checked: true },
      { label: "Full", key: "F" },
      { label: "Half", key: "H" },
      { label: "Quarter", key: "Q" },
    ];

    function mnemo(label, key) {
      if (!key) return label;
      const i = label.toLowerCase().indexOf(key.toLowerCase());
      if (i < 0) return label;
      return (
        label.slice(0, i) +
        "<u>" +
        label.slice(i, i + 1) +
        "</u>" +
        label.slice(i + 1)
      );
    }

    function ddItem(opts) {
      const {
        label,
        key,
        shortcut,
        radio,
        checked,
        check,
        submenu,
        disabled,
        sep,
      } = opts;
      if (sep) return '<div class="preview-dd__sep"></div>';
      const cls = ["preview-dd__item"];
      if (submenu) cls.push("has-submenu");
      if (checked) cls.push("is-checked");
      if (disabled) cls.push("is-disabled");
      let mark = "";
      let mode = "";
      if (radio) {
        mode = "radio";
        mark = checked ? "●" : "";
      } else if (check) {
        mode = "check";
        mark = checked ? "✓" : "";
      }
      let html =
        '<div class="' +
        cls.join(" ") +
        '"' +
        (disabled ? ' aria-disabled="true"' : "") +
        (mode ? ' data-mode="' + mode + '"' : "") +
        ' data-label="' +
        label.replace(/"/g, "&quot;") +
        '">' +
        '<span class="preview-dd__mark">' +
        mark +
        "</span>" +
        '<span class="preview-dd__label">' +
        mnemo(label, key) +
        "</span>";
      if (submenu) {
        html += '<span class="preview-dd__arrow">▸</span>';
        html += '<div class="preview-dd__flyout">';
        submenu.forEach((s) => {
          html += ddItem(s);
        });
        html += "</div>";
      } else {
        html += '<span class="preview-dd__shortcut">' + (shortcut || "") + "</span>";
      }
      html += "</div>";
      return html;
    }

    function ddMenu(items) {
      return items.map(ddItem).join("");
    }

    const qualityMenu = qualityLevels
      .map((name) => {
        const key = name[0];
        return ddItem({
          label: name,
          key,
          radio: true,
          checked: name === "Preview",
          submenu: qualityRes.map((r) => ({
            label: r.label,
            key: r.key,
            radio: true,
            checked: name === "Preview" && !!r.checked,
          })),
        });
      })
      .join("");

    const splitMenu = ddMenu([
      { label: "FX Bypassed", radio: true, checked: true },
      { label: "Clipboard", key: "C", radio: true },
      { sep: true },
      { label: "Select Left Half", key: "L" },
      { label: "Select Right Half", key: "R" },
      { label: "Select All", key: "A" },
      { sep: true },
      { label: "Retain Split Screen Setting", check: true },
    ]);

    const zoomMenu = ddMenu([
      { label: "+", shortcut: "Ctrl+Num+" },
      { label: "−", shortcut: "Ctrl+Num−" },
      { sep: true },
      { label: "Original resolution" },
      { label: "50 %" },
      { label: "100 %", shortcut: "Ctrl+Num1", radio: true, checked: true },
      { label: "125 %" },
      { label: "150 %" },
      { label: "200 %" },
      { label: "400 %" },
      { label: "800 %" },
      { sep: true },
      { label: "Bypass Zoom", shortcut: "Ctrl+Shift+Alt+B", check: true },
    ]);

    const overlayMenu = ddMenu([
      { label: "Grid", key: "G", check: true },
      { label: "Safe Areas", key: "S", check: true },
      { sep: true },
      { label: "Closed Captioning CC1 (Primary)", key: "1", radio: true },
      { label: "Closed Captioning CC2", key: "2", radio: true },
      { label: "Closed Captioning CC3 (Secondary)", key: "3", radio: true },
      { label: "Closed Captioning CC4", key: "4", radio: true },
      { sep: true },
      { label: "Red", key: "R", radio: true },
      { label: "Green", key: "e", radio: true },
      { label: "Blue", key: "u", radio: true },
      { label: "Red as Grayscale", key: "R", radio: true },
      { label: "Green as Grayscale", key: "G", radio: true },
      { label: "Blue as Grayscale", key: "B", radio: true },
      { label: "Alpha as Grayscale", key: "A", radio: true },
    ]);

    document.querySelectorAll("[data-chrome='preview-toolbar']").forEach((el) => {
      if (el.dataset.filled) return;
      el.dataset.filled = "1";
      el.innerHTML =
        I(
          "Project Properties",
          '<circle cx="8" cy="8" r="2.2"/><path d="M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6M3.3 3.3l1.1 1.1M11.6 11.6l1.1 1.1M12.7 3.3l-1.1 1.1M4.4 11.6l-1.1 1.1" fill="none" stroke="currentColor" stroke-width="1.2"/>'
        ) +
        I(
          "Preview on External Monitor",
          '<path d="M2 3.5h9v7H2v-7zm10 1.5h2v6h-2V5zM4.5 12.5h4" fill="none" stroke="currentColor" stroke-width="1.2"/>'
        ) +
        '<button class="icon-btn icon-btn--sm" type="button" title="Video Output FX"><span style="font-size:9px;font-weight:700;line-height:1">fx</span></button>' +
        '<div class="preview-dd" data-dd="split">' +
        '<button type="button" class="icon-btn icon-btn--sm preview-dd__btn" title="Split Screen View" aria-haspopup="menu">' +
        '<svg viewBox="0 0 16 16"><path d="M2.5 2.5h11v11h-11z" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M2.5 13.5L13.5 2.5" stroke="currentColor" stroke-width="1.2"/><path d="M2.5 2.5h11L2.5 13.5z" fill="currentColor" opacity=".35"/></svg>' +
        '<span class="preview-dd__caret">▾</span></button>' +
        '<div class="preview-dd__menu" hidden>' +
        splitMenu +
        "</div></div>" +
        '<div class="preview-dd" data-dd="quality">' +
        '<button type="button" class="dropdown-chip preview-dd__btn" aria-haspopup="menu">' +
        '<span class="preview-dd__value">Preview (Auto)</span> <span class="dropdown-chip__caret">▾</span></button>' +
        '<div class="preview-dd__menu preview-dd__menu--quality" hidden>' +
        qualityMenu +
        "</div></div>" +
        '<div class="preview-dd" data-dd="zoom">' +
        '<button type="button" class="dropdown-chip preview-dd__btn" aria-haspopup="menu">' +
        '<span class="preview-dd__value">100 %</span> <span class="dropdown-chip__caret">▾</span></button>' +
        '<div class="preview-dd__menu" hidden>' +
        zoomMenu +
        "</div></div>" +
        '<div class="preview-dd" data-dd="overlays">' +
        '<button type="button" class="icon-btn icon-btn--sm preview-dd__btn" title="Overlays" aria-haspopup="menu">' +
        '<span style="font-size:12px;font-weight:700;line-height:1">#</span>' +
        '<span class="preview-dd__caret">▾</span></button>' +
        '<div class="preview-dd__menu preview-dd__menu--wide" hidden>' +
        overlayMenu +
        "</div></div>" +
        I("Copy Snapshot to Clipboard", '<path d="M5 2.5h8v10H5V2.5zm-2.5 2.5H5v9.5H2.5V5z"/>') +
        I("Save Snapshot to File", '<path d="M2 2h9l3 3v9H2V2zm2 0v4h7V2M4 10h8v4H4z"/>') +
        '<button class="icon-btn icon-btn--sm is-disabled" type="button" title="360° Video" disabled><span style="font-size:8px;font-weight:700">360</span></button>' +
        '<button class="icon-btn icon-btn--sm is-disabled" type="button" title="HDR" disabled><span style="font-size:8px;font-weight:700">HDR</span></button>';
    });

    initPreviewToolbarDropdowns();
  }

  function initPreviewToolbarDropdowns() {
    function closeAll(except) {
      document.querySelectorAll(".preview-dd.is-open").forEach((dd) => {
        if (dd === except) return;
        dd.classList.remove("is-open");
        const menu = dd.querySelector(":scope > .preview-dd__menu");
        if (menu) menu.hidden = true;
        dd.querySelectorAll(".preview-dd__item.is-open").forEach((it) => it.classList.remove("is-open"));
      });
    }

    document.querySelectorAll(".preview-dd").forEach((dd) => {
      if (dd.dataset.wired) return;
      dd.dataset.wired = "1";
      const btn = dd.querySelector(":scope > .preview-dd__btn");
      const menu = dd.querySelector(":scope > .preview-dd__menu");
      if (!btn || !menu) return;

      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        const willOpen = menu.hidden;
        closeAll();
        if (willOpen) {
          dd.classList.add("is-open");
          menu.hidden = false;
        }
      });

      menu.addEventListener("click", (e) => e.stopPropagation());

      menu.querySelectorAll(":scope > .preview-dd__item.has-submenu").forEach((item) => {
        item.addEventListener("mouseenter", () => {
          menu.querySelectorAll(":scope > .preview-dd__item.is-open").forEach((sib) => {
            if (sib !== item) sib.classList.remove("is-open");
          });
          item.classList.add("is-open");
        });
      });

      // Selection within menus
      menu.addEventListener("click", (e) => {
        const item = e.target.closest(".preview-dd__item");
        if (!item || item.classList.contains("is-disabled")) return;
        if (item.classList.contains("has-submenu")) return;

        const mark = item.querySelector(".preview-dd__mark");
        const mode = item.getAttribute("data-mode");
        const group = item.parentElement;
        const peers = [...group.querySelectorAll(":scope > .preview-dd__item")].filter(
          (el) => !el.classList.contains("has-submenu")
        );

        if (mode === "radio" || (!mode && item.closest(".preview-dd__flyout"))) {
          peers.forEach((el) => {
            el.classList.remove("is-checked");
            const m = el.querySelector(".preview-dd__mark");
            if (m) m.textContent = "";
          });
          item.classList.add("is-checked");
          if (mark) mark.textContent = "●";

          if (dd.dataset.dd === "quality" && item.closest(".preview-dd__flyout")) {
            const qItem = item.closest(".preview-dd__item.has-submenu");
            if (qItem) {
              menu.querySelectorAll(":scope > .preview-dd__item").forEach((el) => {
                el.classList.remove("is-checked");
                const m = el.querySelector(":scope > .preview-dd__mark");
                if (m) m.textContent = "";
              });
              qItem.classList.add("is-checked");
              const qm = qItem.querySelector(":scope > .preview-dd__mark");
              if (qm) qm.textContent = "●";
              const valueEl = dd.querySelector(".preview-dd__value");
              if (valueEl) {
                valueEl.textContent =
                  (qItem.getAttribute("data-label") || "Preview") +
                  " (" +
                  (item.getAttribute("data-label") || "Auto") +
                  ")";
              }
            }
            closeAll();
            return;
          }

          if (dd.dataset.dd === "zoom") {
            const lab = item.getAttribute("data-label") || "";
            if (/^\d+\s*%$/.test(lab)) {
              const valueEl = dd.querySelector(".preview-dd__value");
              if (valueEl) valueEl.textContent = lab;
              closeAll();
              return;
            }
          }
          return;
        }

        if (mode === "check") {
          const now = !item.classList.contains("is-checked");
          item.classList.toggle("is-checked", now);
          if (mark) mark.textContent = now ? "✓" : "";
          return;
        }

        closeAll();
      });
    });

    if (!document.body.dataset.previewDdDocWired) {
      document.body.dataset.previewDdDocWired = "1";
      document.addEventListener("click", () => closeAll());
      document.addEventListener("keydown", (e) => {
        if (e.key === "Escape") closeAll();
      });
    }
  }

  function enhancePreviewFooter() {
    document.querySelectorAll("[data-chrome='preview-footer']").forEach((el) => {
      if (el.dataset.filled) return;
      el.dataset.filled = "1";
      const project = fmtMeta(el.dataset.project || "1920x1080x32; 59,940p");
      const previewRaw = el.dataset.preview || "480x270x32; 59,940p";
      const previewParts = splitPreviewMeta(fmtMeta(previewRaw));
      const frame = el.dataset.frame || "0";
      const display = fmtMeta(el.dataset.display || "850x478x32");

      const moreItem = (label, shortcut, svg) =>
        '<button type="button" class="preview-more__item" title="' + label + '">' +
        '<span class="preview-more__ico"><svg viewBox="0 0 16 16">' + svg + "</svg></span>" +
        '<span class="preview-more__label">' + label + "</span>" +
        '<span class="preview-more__shortcut">' + (shortcut || "") + "</span>" +
        "</button>";

      el.innerHTML =
        '<div class="preview-footer__transport-row">' +
        '<div class="preview-transport">' +
        I("Loop Playback", '<path d="M3 8a5 5 0 019-3M12 3v3h-3M13 8a5 5 0 01-9 3M4 13v-3h3" fill="none" stroke="currentColor" stroke-width="1.3"/>', true) +
        I("Play", '<path d="M4 2.5l10 5.5L4 13.5V2.5z"/>') +
        I("Pause", '<path d="M3 2.5h3.5V13.5H3V2.5zm6.5 0H13V13.5H9.5V2.5z"/>') +
        I("Stop", '<path d="M3 3h10v10H3z"/>') +
        '<div class="preview-more">' +
        '<button type="button" class="icon-btn preview-more__btn" title="More" aria-haspopup="menu" aria-expanded="false">' +
        '<svg viewBox="0 0 16 16"><circle cx="3" cy="8" r="1.2"/><circle cx="8" cy="8" r="1.2"/><circle cx="13" cy="8" r="1.2"/></svg>' +
        "</button>" +
        '<div class="preview-more__menu" role="menu" hidden>' +
        moreItem("Record", "", '<circle cx="8" cy="8" r="5" fill="none" stroke="currentColor" stroke-width="1.2"/><circle cx="8" cy="8" r="2.4" fill="#c42b1c"/>') +
        moreItem("Play From Start", "Shift+Space", '<path d="M2 3v10h1.5V3H2zm3 0l9 5-9 5V3z"/>') +
        moreItem("Go to Start", "Ctrl+Home", '<path d="M2 3v10h1.5V3H2zm12 0L6 8l8 5V3z"/>') +
        moreItem("Go to End", "Ctrl+End", '<path d="M12.5 3v10H14V3h-1.5zM2 3l8 5-8 5V3z"/>') +
        moreItem("Previous Frame", "Alt+Left", '<path d="M11 3L5 8l6 5V3zM3.5 3v10H2V3h1.5z"/>') +
        moreItem("Next Frame", "Alt+Right", '<path d="M5 3l6 5-6 5V3zM12.5 3v10H14V3h-1.5z"/>') +
        '<div class="preview-more__sep"></div>' +
        '<button type="button" class="preview-more__item preview-more__item--text" title="Edit Visible Button Set...">' +
        '<span class="preview-more__ico"></span>' +
        '<span class="preview-more__label">Edit Visible Button Set...</span>' +
        '<span class="preview-more__shortcut"></span>' +
        "</button>" +
        "</div></div>" +
        "</div></div>" +
        '<div class="preview-footer__info-row">' +
        '<div class="preview-footer__left">' +
        `Project: ${project}<br>` +
        `Preview: <span class="meta-warn">${previewParts.res}</span>` +
        (previewParts.rest ? `; ${previewParts.rest}` : "") +
        "</div>" +
        `<div class="preview-footer__right">Frame: <b>${frame}</b><br>Display: ${display}</div>` +
        "</div>";
    });

    initPreviewMoreMenus();
  }

  function initPreviewMoreMenus() {
    document.querySelectorAll(".preview-more").forEach((wrap) => {
      if (wrap.dataset.wired) return;
      wrap.dataset.wired = "1";
      const btn = wrap.querySelector(".preview-more__btn");
      const menu = wrap.querySelector(".preview-more__menu");
      if (!btn || !menu) return;

      function close() {
        menu.hidden = true;
        btn.classList.remove("is-active");
        btn.setAttribute("aria-expanded", "false");
      }

      function open() {
        document.querySelectorAll(".preview-more__menu:not([hidden])").forEach((m) => {
          if (m !== menu) {
            m.hidden = true;
            const b = m.closest(".preview-more")?.querySelector(".preview-more__btn");
            if (b) {
              b.classList.remove("is-active");
              b.setAttribute("aria-expanded", "false");
            }
          }
        });
        menu.hidden = false;
        btn.classList.add("is-active");
        btn.setAttribute("aria-expanded", "true");
      }

      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        if (menu.hidden) open();
        else close();
      });

      menu.addEventListener("click", (e) => {
        e.stopPropagation();
        if (e.target.closest(".preview-more__item")) close();
      });
    });

    if (!document.body.dataset.previewMoreDocWired) {
      document.body.dataset.previewMoreDocWired = "1";
      document.addEventListener("click", () => {
        document.querySelectorAll(".preview-more__menu:not([hidden])").forEach((menu) => {
          menu.hidden = true;
          const b = menu.closest(".preview-more")?.querySelector(".preview-more__btn");
          if (b) {
            b.classList.remove("is-active");
            b.setAttribute("aria-expanded", "false");
          }
        });
      });
      document.addEventListener("keydown", (e) => {
        if (e.key !== "Escape") return;
        document.querySelectorAll(".preview-more__menu:not([hidden])").forEach((menu) => {
          menu.hidden = true;
          const b = menu.closest(".preview-more")?.querySelector(".preview-more__btn");
          if (b) {
            b.classList.remove("is-active");
            b.setAttribute("aria-expanded", "false");
          }
        });
      });
    }
  }

  function enhanceMasterBus() {
    const scale = [3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48, 51, 54, 57]
      .map((n) => `<span>${n}</span>`)
      .join("");

    document.querySelectorAll("[data-chrome='master-bus']").forEach((el) => {
      if (el.dataset.filled) return;
      el.dataset.filled = "1";
      const l = Number(el.dataset.levelL ?? "0");
      const r = Number(el.dataset.levelR ?? "0");
      const lFill = Number.isFinite(l) ? Math.max(0, l) : 0;
      const rFill = Number.isFinite(r) ? Math.max(0, r) : 0;
      const lPeak =
        lFill > 0
          ? `<div class="vu-peak" style="bottom:${Math.min(96, lFill + 2)}%"></div>`
          : "";
      const rPeak =
        rFill > 0
          ? `<div class="vu-peak" style="bottom:${Math.min(96, rFill + 2)}%"></div>`
          : "";
      el.innerHTML =
        '<div class="master-bus__toolbar">' +
        I(
          "Master Bus Properties",
          '<circle cx="8" cy="8" r="2.2"/><path d="M8 1.5v1.6M8 12.9v1.6M1.5 8h1.6M12.9 8h1.6M3.3 3.3l1.1 1.1M11.6 11.6l1.1 1.1M12.7 3.3l-1.1 1.1M4.4 11.6l-1.1 1.1" fill="none" stroke="currentColor" stroke-width="1.2"/>'
        ) +
        I(
          "Audio Device",
          '<path d="M3 6.5h2.2L8 3.8v8.4L5.2 9.5H3V6.5zM10 6.2a2.2 2.2 0 010 3.6M11.6 5a3.8 3.8 0 010 6" fill="none" stroke="currentColor" stroke-width="1.2"/>'
        ) +
        I(
          "Downmix / Monitor",
          '<path d="M2.5 6.5h2L7 4v8L4.5 9.5h-2V6.5zM9.5 5.5v5M11.5 4.5v7M13.5 3.5v9" fill="none" stroke="currentColor" stroke-width="1.2"/>'
        ) +
        I(
          "Mixing Console",
          '<path d="M3 3v10M6.5 5v8M10 4v9M13.5 6v7" stroke="currentColor" stroke-width="1.4" fill="none"/><circle cx="3" cy="7" r="1.2"/><circle cx="6.5" cy="9" r="1.2"/><circle cx="10" cy="6.5" r="1.2"/><circle cx="13.5" cy="10" r="1.2"/>'
        ) +
        "</div>" +
        '<div class="master-bus__title">' +
        '<span class="master-bus__title-ico" aria-hidden="true">' +
        '<svg viewBox="0 0 12 12"><rect x="1" y="1" width="10" height="10" fill="none" stroke="currentColor" stroke-width="1.2"/><rect x="4" y="4" width="4" height="4" fill="currentColor"/></svg>' +
        "</span>" +
        "<span>Master</span>" +
        "</div>" +
        '<div class="master-bus__btns">' +
        '<button class="ms-btn is-fx" type="button" title="Track FX">fx</button>' +
        '<button class="ms-btn" type="button" title="Automation Write">' +
        '<svg viewBox="0 0 12 12" aria-hidden="true"><circle cx="6" cy="6" r="4.2" fill="none" stroke="currentColor" stroke-width="1.2"/><circle cx="6" cy="6" r="1.5" fill="currentColor"/></svg>' +
        "</button>" +
        '<button class="ms-btn" type="button" title="Mute">M</button>' +
        '<button class="ms-btn" type="button" title="Solo">S</button>' +
        "</div>" +
        '<div class="master-bus__meterblock">' +
        '<div class="master-bus__fadercol">' +
        '<div class="master-fader-track">' +
        '<input class="master-fader" type="range" min="0" max="100" value="55" aria-label="Master volume" />' +
        "</div></div>" +
        '<div class="vu-meters">' +
        `<div class="vu-channel"><div class="vu-fill" style="height:${lFill}%"></div>${lPeak}</div>` +
        `<div class="vu-scale">${scale}</div>` +
        `<div class="vu-channel"><div class="vu-fill" style="height:${rFill}%"></div>${rPeak}</div>` +
        "</div></div>" +
        '<div class="master-bus__levels">' +
        '<button type="button" class="master-bus__lock" title="Lock Fader">' +
        '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M3.5 5.2V3.8a2.5 2.5 0 015 0v1.4H9.5v5.3h-7V5.2H3.5zm1.2 0h2.6V3.8a1.3 1.3 0 00-2.6 0v1.4z" fill="currentColor"/></svg>' +
        "</button>" +
        '<div class="master-bus__peaks">' +
        '<span class="master-bus__level">0,0</span>' +
        '<span class="master-bus__peaks-gap" aria-hidden="true"></span>' +
        '<span class="master-bus__level">0,0</span>' +
        "</div></div>" +
        '<div class="panel-tabs master-bus__tabs">' +
        '<button type="button" class="panel-tab is-active">Master Bus</button>' +
        '<span class="panel-tabs__spacer"></span>' +
        '<button type="button" class="panel-tab-ico" title="Maximize">▣</button>' +
        '<button type="button" class="panel-tab-ico" title="Close">×</button>' +
        "</div>";
    });
  }

  function enhanceTreeIcons() {
    document.querySelectorAll(".tree-item:not(.tree-item--nested)").forEach((item) => {
      if (item.querySelector(".tree-ico")) return;
      const ico = document.createElement("span");
      ico.className = "tree-ico";
      ico.setAttribute("aria-hidden", "true");
      const arrow = item.querySelector(".arrow");
      if (arrow) arrow.after(ico);
      else item.prepend(ico);
    });
  }

  function enhanceMediaToolbar() {
    document.querySelectorAll("[data-chrome='media-toolbar']").forEach((el) => {
      if (el.dataset.filled) return;
      el.dataset.filled = "1";
      el.innerHTML =
        '<button type="button" class="text-btn text-btn--dd" title="Import Media">' +
        '<svg viewBox="0 0 16 16"><path d="M8 2v8M5 7l3 3 3-3M2 13h12" fill="none" stroke="currentColor" stroke-width="1.3"/></svg>' +
        " Import Media... <span class=\"icon-btn__caret\">▾</span></button>" +
        SEP +
        I(
          "Auto Preview",
          '<path d="M3.5 2.5h2v4h-2zM10.5 2.5h2v4h-2zM5.5 4.5h5M4.5 9.5l3.5 3.5 3.5-3.5" fill="none" stroke="currentColor" stroke-width="1.25"/><path d="M8 6.5v6.5" stroke="currentColor" stroke-width="1.25"/>'
        ) +
        I(
          "Capture Video",
          '<rect x="2" y="4.5" width="9.5" height="7" rx="0.8" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M11.5 7l3-1.5v5L11.5 9V7z"/><circle cx="6.5" cy="8" r="1.6" fill="#c42b1c"/>'
        ) +
        I(
          "Extract Audio from CD",
          '<circle cx="7" cy="8" r="4.5" fill="none" stroke="currentColor" stroke-width="1.2"/><circle cx="7" cy="8" r="1.3"/><circle cx="12.2" cy="12.2" r="2.4" fill="none" stroke="currentColor" stroke-width="1.15"/><path d="M13.7 13.7l1.5 1.5" stroke="currentColor" stroke-width="1.2"/>'
        ) +
        I(
          "Get Media from the Web",
          '<circle cx="8" cy="8" r="5.5" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M2.5 8h11M8 2.5c1.8 1.8 2.8 3.6 2.8 5.5S9.8 11.7 8 13.5C6.2 11.7 5.2 9.9 5.2 8S6.2 4.3 8 2.5z" fill="none" stroke="currentColor" stroke-width="1.1"/>'
        ) +
        SEP +
        I(
          "Remove Selected Media",
          '<path d="M3.2 3.2l9.6 9.6M12.8 3.2L3.2 12.8" stroke="#c42b1c" fill="none" stroke-width="1.7" stroke-linecap="round"/>'
        ) +
        I("Media Properties", GEAR) +
        '<button class="icon-btn icon-btn--fx" type="button" title="Apply Non-Real-Time Event FX">' +
        '<span class="fx-badge">fx</span></button>' +
        SEP +
        I("Start Preview", '<path d="M4 2.8l9.5 5.2L4 13.2V2.8z"/>') +
        I("Stop Preview", '<path d="M3.4 3.4h9.2v9.2H3.4z"/>') +
        I(
          "Open in Audio Editor",
          '<path d="M2 10.5c1.2-3 2.2-4.5 3-4.5s1.5 3 2.5 3 1.8-4.5 2.5-4.5 1.8 3 3 4.5" fill="none" stroke="currentColor" stroke-width="1.25"/><path d="M2 13h12" stroke="currentColor" stroke-width="1.1"/>'
        ) +
        SEP +
        I(
          "Views",
          '<path d="M2 2h5v5H2V2zm7 0h5v5H9V2zM2 9h5v5H2V9zm7 0h5v5H9V9z"/>',
          { caret: true, active: true }
        ) +
        '<span class="toolbar-spacer"></span>' +
        I(
          "Search Media",
          '<circle cx="7" cy="7" r="4.2" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M10.2 10.2l3.5 3.5" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/>'
        ) +
        I(
          "Filter Media",
          '<path d="M2.5 3.5h11l-4 5v4l-3 1.5v-5.5z" fill="none" stroke="currentColor" stroke-width="1.25" stroke-linejoin="round"/>'
        );
    });
  }

  function enhanceDockTabs() {
    const order = [
      { id: "project-media", label: "Project Media" },
      { id: "explorer", label: "Explorer" },
      { id: "video-fx", label: "Video FX" },
      { id: "media-generators", label: "Media Generator" },
      { id: "transitions", label: "Transitions" },
      { id: "project-notes", label: "Project Notes" },
    ];

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

    document.querySelectorAll("[data-tabs] > .panel-tabs").forEach((bar) => {
      if (bar.dataset.dockEnhanced) return;
      bar.dataset.dockEnhanced = "1";

      const activeId =
        bar.querySelector(".panel-tab.is-active")?.dataset.tab || "project-media";

      bar.innerHTML = order
        .map((t) => {
          const on = t.id === activeId;
          return (
            '<div class="panel-tab-wrap' +
            (on ? " is-active" : "") +
            '">' +
            '<button type="button" class="panel-tab' +
            (on ? " is-active" : "") +
            '" data-tab="' +
            t.id +
            '">' +
            t.label +
            "</button>" +
            (on ? chromeHtml() : "") +
            "</div>"
          );
        })
        .join("");
    });
  }

  function initToolToggles() {
    document.querySelectorAll(".main-toolbar, .timeline-tools").forEach((bar) => {
      bar.addEventListener("click", (e) => {
        const btn = e.target.closest(".icon-btn");
        if (!btn || btn.disabled || btn.classList.contains("is-disabled") || !bar.contains(btn)) {
          return;
        }
        if (
          btn.title &&
          /Edit Tool|Normal Edit|Envelope Edit|Selection Edit|Zoom Edit/.test(btn.title)
        ) {
          const editGroup = bar.querySelector(".timeline-tools__edit") || bar;
          if (editGroup.contains(btn) || bar.classList.contains("main-toolbar")) {
            const scope = bar.classList.contains("main-toolbar") ? bar : editGroup;
            scope.querySelectorAll(".icon-btn").forEach((b) => {
              if (/Edit Tool|Normal Edit|Envelope Edit|Selection Edit|Zoom Edit/.test(b.title || "")) {
                b.classList.remove("is-active");
              }
            });
            btn.classList.add("is-active");
            return;
          }
        }
        if (
          /Snapping|Ripple|Lock Envelopes|^Lock$|Crossfade|Ignore|Loop Playback/.test(
            btn.title || ""
          )
        ) {
          btn.classList.toggle("is-active");
        }
      });
    });
  }

  function initTreeSelection() {
    document.querySelectorAll(".media-tree").forEach((tree) => {
      tree.addEventListener("click", (e) => {
        const item = e.target.closest(".tree-item");
        if (!item) return;
        tree.querySelectorAll(".tree-item.is-selected").forEach((el) => el.classList.remove("is-selected"));
        item.classList.add("is-selected");
      });
    });
  }

  function enhancePreviewPanelTabs() {
    document.querySelectorAll(".preview-panel").forEach((panel) => {
      if (panel.querySelector(":scope > .panel-tabs")) return;
      const tabs = document.createElement("div");
      tabs.className = "panel-tabs";
      tabs.innerHTML = '<button type="button" class="panel-tab is-active">Video Preview</button>';
      panel.appendChild(tabs);
    });
  }

  document.addEventListener("DOMContentLoaded", () => {
    fill('[data-chrome="main-toolbar"]', MAIN_TOOLBAR);
    fill('[data-chrome="timeline-tools"]', TIMELINE_TOOLS);
    enhanceTreeIcons();
    enhanceMediaToolbar();
    enhanceDockTabs();
    enhancePreviewToolbar();
    enhancePreviewFooter();
    enhancePreviewPanelTabs();
    enhanceMasterBus();
    initToolToggles();
    initTreeSelection();
  });
})();
