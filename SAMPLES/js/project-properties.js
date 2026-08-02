/**
 * Project Properties dialog (Vegas Pro 22-style dark mockup).
 * Open: File → Project Properties…, toolbar, Alt+Enter, or VegasProjectProperties.open().
 */
(function () {
  const WIN_ID = "project-properties";

  const ICO = {
    save:
      '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M2.5 2.5h8.5L13.5 5v8.5h-11V2.5zm2 0v3.5h5.5V2.5M4.5 9h7v4h-7V9z" fill="none" stroke="currentColor" stroke-width="1.2"/></svg>',
    del:
      '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4 4l8 8M12 4L4 12" stroke="#c42b1c" stroke-width="1.6"/></svg>',
    match:
      '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M2 4.5h4.2l1.2 1.5H14v6.5H2V4.5z" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M10 7.5l2.2 2.2L10 11.9M8.5 9.7h3.8" fill="none" stroke="#0078d7" stroke-width="1.2"/></svg>',
  };

  function ak(text, letter) {
    const i = text.toLowerCase().indexOf(letter.toLowerCase());
    if (i < 0) return text;
    return text.slice(0, i) + "<u>" + text.charAt(i) + "</u>" + text.slice(i + 1);
  }

  function opt(list, selected) {
    return list
      .map((v) => {
        const sel = v === selected ? " selected" : "";
        return "<option" + sel + ">" + v + "</option>";
      })
      .join("");
  }

  function spin(value, attrs) {
    const a = attrs || {};
    return (
      '<div class="pp-spin">' +
      '<input type="text" value="' +
      value +
      '"' +
      (a.aria ? ' aria-label="' + a.aria + '"' : "") +
      (a.disabled ? " disabled" : "") +
      " />" +
      '<div class="pp-spin__btns">' +
      '<button type="button" tabindex="-1" aria-label="Increase">▴</button>' +
      '<button type="button" tabindex="-1" aria-label="Decrease">▾</button>' +
      "</div></div>"
    );
  }

  function buildMarkup() {
    return (
      '<div class="pp-dialog" role="dialog" aria-modal="true" aria-labelledby="pp-title">' +
      '<div class="pp-dialog__titlebar" data-pp-drag>' +
      '<span class="pp-dialog__title" id="pp-title">Project Properties</span>' +
      '<div class="pp-dialog__winbtns">' +
      '<button type="button" class="pp-winbtn" title="Help" aria-label="Help">?</button>' +
      '<button type="button" class="pp-winbtn pp-winbtn--close" data-pp-close title="Close" aria-label="Close">✕</button>' +
      "</div></div>" +
      '<div class="pp-dialog__body">' +
      '<div class="pp-tabs" role="tablist">' +
      tabBtn("video", "Video", true) +
      tabBtn("audio", "Audio") +
      tabBtn("ruler", "Ruler") +
      tabBtn("summary", "Summary") +
      tabBtn("audiocd", "Audio CD") +
      tabBtn("advanced", "Advanced") +
      "</div>" +
      '<div class="pp-panels">' +
      panelVideo() +
      panelAudio() +
      panelRuler() +
      panelSummary() +
      panelAudioCd() +
      panelAdvanced() +
      "</div></div>" +
      '<div class="pp-dialog__footer">' +
      '<label class="pp-check"><input type="checkbox" data-pp-field />' +
      "<span>" +
      ak("Start all new projects with these settings", "p") +
      "</span></label>" +
      '<div class="pp-dialog__actions">' +
      '<button type="button" class="pp-btn pp-btn--default" data-pp-ok>OK</button>' +
      '<button type="button" class="pp-btn" data-pp-close>Cancel</button>' +
      '<button type="button" class="pp-btn" data-pp-apply disabled>Apply</button>' +
      "</div></div></div>"
    );
  }

  function tabBtn(id, label, active) {
    return (
      '<button type="button" class="pp-tab' +
      (active ? " is-active" : "") +
      '" role="tab" aria-selected="' +
      (active ? "true" : "false") +
      '" data-pp-tab="' +
      id +
      '">' +
      label +
      "</button>"
    );
  }

  function panelVideo() {
    return (
      '<div class="pp-panel is-active" data-pp-panel="video" role="tabpanel">' +
      '<div class="pp-row pp-row--template">' +
      "<label class=\"pp-label\">" +
      ak("Template:", "T") +
      "</label>" +
      '<select data-pp-field aria-label="Template">' +
      opt(
        [
          "Custom (1920x1080; 59,940 fps)",
          "HD 1080-59.94p",
          "HD 1080-29.97p",
          "UHD 2160-29.97p",
        ],
        "Custom (1920x1080; 59,940 fps)"
      ) +
      "</select>" +
      '<div class="pp-template-tools">' +
      '<button type="button" class="pp-ico" title="Save Template" aria-label="Save Template">' +
      ICO.save +
      "</button>" +
      '<button type="button" class="pp-ico pp-ico--danger" title="Delete Template" aria-label="Delete Template">' +
      ICO.del +
      "</button>" +
      '<button type="button" class="pp-ico" title="Match Media Video Settings" aria-label="Match Media Video Settings">' +
      ICO.match +
      "</button>" +
      "</div></div>" +
      '<div class="pp-grid-video-top">' +
      "<div>" +
      '<div class="pp-row pp-row--tight"><label class="pp-label">' +
      ak("Width:", "W") +
      '</label><input type="text" class="pp-field" data-pp-field value="1 920" /></div>' +
      '<div class="pp-row pp-row--tight"><label class="pp-label">' +
      ak("Height:", "H") +
      '</label><input type="text" class="pp-field" data-pp-field value="1 080" /></div>' +
      '<div class="pp-row pp-row--tight"><label class="pp-label">' +
      ak("HDR Mode:", "M") +
      '</label><select data-pp-field data-pp-hdr>' +
      opt(["Off", "Rec.2020 / ST.2084", "ACES"], "Off") +
      "</select></div>" +
      "</div><div>" +
      '<div class="pp-row pp-row--tight"><label class="pp-label">' +
      ak("Field order:", "F") +
      '</label><select data-pp-field>' +
      opt(
        ["None (progressive scan)", "Upper field first", "Lower field first"],
        "None (progressive scan)"
      ) +
      "</select></div>" +
      '<div class="pp-row pp-row--tight"><label class="pp-label">' +
      ak("Pixel aspect ratio:", "P") +
      '</label><select data-pp-field>' +
      opt(["1,0000 (Square)", "1,2121 (PAL DV)", "0,9091 (NTSC DV)"], "1,0000 (Square)") +
      "</select></div>" +
      '<div class="pp-row pp-row--tight"><label class="pp-label">' +
      ak("Output rotation:", "O") +
      '</label><select data-pp-field>' +
      opt(["0° (original)", "90° clockwise", "180°", "90° counter-clockwise"], "0° (original)") +
      "</select></div>" +
      '<div class="pp-row pp-row--tight"><label class="pp-label">' +
      ak("Frame rate:", "r") +
      '</label><select data-pp-field>' +
      opt(
        ["59,940 (Double NTSC)", "29,970 (NTSC)", "25,000 (PAL)", "23,976 (Film)"],
        "59,940 (Double NTSC)"
      ) +
      "</select></div>" +
      "</div></div>" +
      '<hr class="pp-sep" />' +
      '<div class="pp-stack">' +
      rowSelect(ak("Pixel format:", "x"), ["8-bit (full range)", "8-bit (video levels)", "32-bit floating point"], "8-bit (full range)") +
      rowSelect(ak("Compositing gamma:", "g"), ["2,222 (Video)", "1,000 (Linear)"], "2,222 (Video)", true) +
      rowSelect(ak("ACES version:", "A"), ["0.7", "1.0", "1.1"], "0.7", true) +
      rowSelect(ak("ACES color space:", "c"), ["Default (ACES)", "ACEScg"], "Default (ACES)", true) +
      rowSelect(ak("View transform:", "V"), ["Off", "sRGB", "Rec.709"], "Off", true) +
      rowSelect(ak("Look modification transform:", "L"), ["None", "Custom"], "None", true) +
      rowSelect(ak("Full-resolution rendering quality:", "u"), ["Draft", "Good", "Best"], "Good") +
      rowSelect(ak("Motion blur type:", "b"), ["Gaussian", "Pyramid", "Box"], "Gaussian") +
      rowSelect(ak("Deinterlace method:", "D"), ["None", "Blend fields", "Interpolate fields"], "Blend fields") +
      rowSelect(ak("Resample mode:", "e"), ["Disable resample", "Force resample", "Smart resample"], "Disable resample") +
      "</div>" +
      '<hr class="pp-sep" />' +
      '<label class="pp-check"><input type="checkbox" data-pp-field checked />' +
      "<span>" +
      ak("Adjust source media to better match project or render settings", "j") +
      "</span></label>" +
      '<label class="pp-check"><input type="checkbox" data-pp-field data-pp-override />' +
      "<span>" +
      ak("Override project settings when prerendering video", "v") +
      "</span></label>" +
      '<div class="pp-hint" style="display:flex;align-items:flex-start;justify-content:space-between;gap:12px">' +
      "<div>Format: AVC/AAC MP4<br />Template: Internet 1920x1080 59,940 fps progressive (NVEnc)</div>" +
      '<button type="button" class="pp-btn" data-pp-select disabled>Select...</button>' +
      "</div>" +
      "<div class=\"pp-label\" style=\"margin-top:8px\">" +
      ak("Prerendered files folder:", "d") +
      "</div>" +
      '<div class="pp-path-row">' +
      '<input type="text" class="pp-field" data-pp-field value="C:\\Users\\Admin\\AppData\\Local\\VEGAS Pro\\22.0\\" />' +
      '<button type="button" class="pp-btn">Browse...</button>' +
      "</div>" +
      '<p class="pp-free">Free storage space in selected folder: 656,0 Gigabytes</p>' +
      "</div>"
    );
  }

  function panelAudio() {
    return (
      '<div class="pp-panel" data-pp-panel="audio" role="tabpanel" hidden>' +
      rowSelect(ak("Master bus mode:", "M"), ["Stereo", "Surround 5.1"], "Stereo") +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Number of stereo busses:", "N") +
      "</label>" +
      spin("0", { aria: "Number of stereo busses" }) +
      "</div>" +
      rowSelect(ak("Sample rate (Hz):", "S"), ["44 100", "48 000", "96 000", "192 000"], "48 000") +
      rowSelect(ak("Bit depth:", "B"), ["8", "16", "24", "32"], "16") +
      rowSelect(ak("Resample and stretch quality:", "R"), ["Draft", "Good", "Best"], "Good") +
      '<div style="margin-top:18px;opacity:0.55">' +
      '<label class="pp-check is-disabled"><input type="checkbox" disabled />' +
      "<span>" +
      ak("Enable low-pass filter on LFE (surround projects only)", "E") +
      "</span></label>" +
      rowSelect(
        ak("Cutoff frequency for low-pass filter (Hz):", "C"),
        ["80", "100", "120 (pro/film)", "150"],
        "120 (pro/film)",
        true
      ) +
      rowSelect(ak("Low-pass filter quality:", "L"), ["Draft", "Good", "Best"], "Good", true) +
      "</div>" +
      "<div class=\"pp-label\" style=\"margin-top:28px\">" +
      ak("Recorded files folder:", "f") +
      "</div>" +
      '<div class="pp-path-row">' +
      '<input type="text" class="pp-field" data-pp-field value="C:\\Users\\Admin\\Documents" />' +
      '<button type="button" class="pp-btn">Browse...</button>' +
      "</div>" +
      '<p class="pp-free">Free storage space in selected folder: 656,0 Gigabytes</p>' +
      "</div>"
    );
  }

  function panelRuler() {
    return (
      '<div class="pp-panel" data-pp-panel="ruler" role="tabpanel" hidden>' +
      rowSelect(
        ak("Ruler time format:", "R"),
        [
          "Time",
          "Seconds",
          "Time & Frames",
          "Absolute Frames",
          "Measures & Beats",
          "SMPTE Drop (29.97 fps)",
        ],
        "Time & Frames"
      ) +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Ruler start time:", "s") +
      '</label><input type="text" class="pp-field" data-pp-field value="00:00:00,00" /></div>' +
      '<div class="pp-sep--labeled">Measures &amp; Beats</div>' +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Beats per minute (tempo):", "B") +
      "</label>" +
      spin("120,000", { aria: "Beats per minute" }) +
      "</div>" +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Beats per measure:", "e") +
      "</label>" +
      spin("4", { aria: "Beats per measure" }) +
      "</div>" +
      rowSelect(ak("Note that gets one beat:", "N"), ["Whole", "Half", "Quarter", "Eighth"], "Quarter") +
      "</div>"
    );
  }

  function panelSummary() {
    return (
      '<div class="pp-panel pp-panel--summary" data-pp-panel="summary" role="tabpanel" hidden>' +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Title:", "T") +
      '</label><input type="text" class="pp-field" data-pp-field /></div>' +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Artist:", "A") +
      '</label><input type="text" class="pp-field" data-pp-field /></div>' +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Engineer:", "E") +
      '</label><input type="text" class="pp-field" data-pp-field /></div>' +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Copyright:", "C") +
      '</label><input type="text" class="pp-field" data-pp-field /></div>' +
      '<div class="pp-row" style="align-items:start"><label class="pp-label" style="padding-top:4px">' +
      ak("Comments:", "m") +
      '</label><textarea data-pp-field aria-label="Comments"></textarea></div>' +
      "</div>"
    );
  }

  function panelAudioCd() {
    return (
      '<div class="pp-panel pp-panel--sparse" data-pp-panel="audiocd" role="tabpanel" hidden>' +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Universal Product Code / Media Catalog Number:", "U") +
      '</label><input type="text" class="pp-field" data-pp-field /></div>' +
      '<div class="pp-row"><label class="pp-label">' +
      ak("First track number on disc:", "F") +
      "</label>" +
      spin("1", { aria: "First track number on disc" }) +
      "</div>" +
      "</div>"
    );
  }

  function panelAdvanced() {
    return (
      '<div class="pp-panel" data-pp-panel="advanced" role="tabpanel" hidden>' +
      '<div class="pp-master-row">' +
      "<label class=\"pp-label\">" +
      ak("Master Display:", "M") +
      "</label>" +
      '<select disabled data-pp-field>' +
      opt(["Rec.2020, 1000 Nits, D65, ST.2084, Full"], "Rec.2020, 1000 Nits, D65, ST.2084, Full") +
      "</select>" +
      '<button type="button" class="pp-btn" disabled>Customize...</button>' +
      "</div>" +
      '<hr class="pp-sep" />' +
      '<label class="pp-check"><input type="checkbox" data-pp-field />' +
      "<span>" +
      ak("360 Output", "3") +
      "</span></label>" +
      '<hr class="pp-sep" />' +
      '<div class="pp-row"><label class="pp-label">' +
      ak("Stereoscopic 3D mode:", "S") +
      '</label><div class="pp-inline"><select data-pp-field>' +
      opt(["Off", "Side by side", "Top / bottom", "Anaglyph"], "Off") +
      "</select>" +
      '<label class="pp-check" style="margin:0;white-space:nowrap"><input type="checkbox" data-pp-field /><span>Swap Left/Right</span></label>' +
      "</div></div>" +
      '<div class="pp-slider-row" style="opacity:0.55">' +
      "<label class=\"pp-label\">" +
      ak("Crosstalk cancellation:", "C") +
      "</label>" +
      '<input type="range" min="0" max="100" value="0" disabled />' +
      '<input type="text" value="0,000" disabled />' +
      "</div>" +
      '<label class="pp-check is-disabled"><input type="checkbox" disabled />' +
      "<span>" +
      ak("Include cancellation in renders and print to tape", "I") +
      "</span></label>" +
      "</div>"
    );
  }

  function rowSelect(labelHtml, options, selected, disabled) {
    return (
      '<div class="pp-row"><label class="pp-label">' +
      labelHtml +
      "</label><select data-pp-field" +
      (disabled ? " disabled" : "") +
      ">" +
      opt(options, selected) +
      "</select></div>"
    );
  }

  function ensure() {
    let root = document.getElementById(WIN_ID);
    if (root) return root;
    root = document.createElement("div");
    root.id = WIN_ID;
    root.className = "pp-backdrop";
    root.hidden = true;
    root.innerHTML = buildMarkup();
    document.body.appendChild(root);
    wire(root);
    return root;
  }

  function setDirty(root, dirty) {
    const apply = root.querySelector("[data-pp-apply]");
    if (apply) apply.disabled = !dirty;
    root.dataset.dirty = dirty ? "1" : "0";
  }

  function activateTab(root, id) {
    root.querySelectorAll("[data-pp-tab]").forEach((btn) => {
      const on = btn.dataset.ppTab === id;
      btn.classList.toggle("is-active", on);
      btn.setAttribute("aria-selected", on ? "true" : "false");
    });
    root.querySelectorAll("[data-pp-panel]").forEach((panel) => {
      const on = panel.dataset.ppPanel === id;
      panel.classList.toggle("is-active", on);
      panel.hidden = !on;
    });
  }

  function wire(root) {
    const dialog = root.querySelector(".pp-dialog");

    root.querySelectorAll("[data-pp-tab]").forEach((btn) => {
      btn.addEventListener("click", () => activateTab(root, btn.dataset.ppTab));
    });

    root.querySelectorAll("[data-pp-close]").forEach((btn) => {
      btn.addEventListener("click", () => close());
    });

    root.querySelector("[data-pp-ok]")?.addEventListener("click", () => {
      setDirty(root, false);
      close();
    });

    root.querySelector("[data-pp-apply]")?.addEventListener("click", () => {
      setDirty(root, false);
    });

    root.addEventListener("mousedown", (e) => {
      if (e.target === root) close();
    });

    root.addEventListener("input", (e) => {
      if (e.target.closest("[data-pp-field], .pp-spin, select, textarea")) setDirty(root, true);
    });
    root.addEventListener("change", (e) => {
      if (e.target.closest("[data-pp-field], select, input")) setDirty(root, true);
      if (e.target.matches("[data-pp-override]")) {
        const sel = root.querySelector("[data-pp-select]");
        if (sel) sel.disabled = !e.target.checked;
      }
    });

    root.querySelectorAll(".pp-spin__btns button").forEach((btn) => {
      btn.addEventListener("click", () => {
        const input = btn.closest(".pp-spin")?.querySelector("input");
        if (!input || input.disabled) return;
        const raw = String(input.value).replace(/\s/g, "").replace(",", ".");
        const n = parseFloat(raw);
        if (Number.isNaN(n)) return;
        const up = btn.getAttribute("aria-label") === "Increase";
        const next = up ? n + 1 : Math.max(0, n - 1);
        input.value = String(input.value).includes(",")
          ? String(next).replace(".", ",")
          : String(next);
        setDirty(root, true);
      });
    });

    const drag = root.querySelector("[data-pp-drag]");
    if (drag && dialog) {
      let ox = 0;
      let oy = 0;
      let dragging = false;
      drag.addEventListener("mousedown", (e) => {
        if (e.target.closest(".pp-winbtn")) return;
        dragging = true;
        const r = dialog.getBoundingClientRect();
        dialog.style.position = "fixed";
        dialog.style.margin = "0";
        dialog.style.left = r.left + "px";
        dialog.style.top = r.top + "px";
        ox = e.clientX - r.left;
        oy = e.clientY - r.top;
        e.preventDefault();
      });
      window.addEventListener("mousemove", (e) => {
        if (!dragging) return;
        dialog.style.left = Math.max(0, e.clientX - ox) + "px";
        dialog.style.top = Math.max(0, e.clientY - oy) + "px";
      });
      window.addEventListener("mouseup", () => {
        dragging = false;
      });
    }
  }

  function open(tab) {
    const root = ensure();
    root.hidden = false;
    root.classList.add("is-open");
    activateTab(root, tab || "video");
    setDirty(root, false);
    const first = root.querySelector(".pp-panel.is-active select, .pp-panel.is-active input");
    setTimeout(() => first && first.focus(), 0);
  }

  function close() {
    const root = document.getElementById(WIN_ID);
    if (!root) return;
    root.classList.remove("is-open");
    root.hidden = true;
  }

  function isOpen() {
    const root = document.getElementById(WIN_ID);
    return !!(root && root.classList.contains("is-open"));
  }

  function onOpenIntent() {
    open();
  }

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && isOpen()) {
      e.preventDefault();
      close();
      return;
    }
    if (e.altKey && e.key === "Enter") {
      e.preventDefault();
      open();
    }
  });

  document.addEventListener("click", (e) => {
    const tb = e.target.closest(
      '.icon-btn[title="Project Properties"], .icon-btn[title="Project Video Properties"]'
    );
    if (tb) {
      e.preventDefault();
      open();
      return;
    }
    const item = e.target.closest(".dropdown-menu__item");
    if (!item || item.classList.contains("is-disabled")) return;
    const label = (item.querySelector(".label")?.textContent || "").trim();
    const action = item.getAttribute("data-action") || "";
    if (
      action === "project-properties" ||
      label === "Project Properties..." ||
      (label === "Properties..." && !e.target.closest("#context-menu, .event"))
    ) {
      e.stopPropagation();
      open();
    }
  });

  document.addEventListener("vegas:menu-action", (e) => {
    const action = e.detail?.action || "";
    const label = e.detail?.label || "";
    if (action === "project-properties" || label === "Project Properties..." || label === "Properties...") open();
  });

  window.VegasProjectProperties = { open, close, isOpen };

  if (document.body?.dataset.ppOpen === "1") {
    document.addEventListener("DOMContentLoaded", () => open());
    if (document.readyState !== "loading") open();
  }
})();
