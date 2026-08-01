/**
 * Preferences dialog (Vegas Pro 22-style dark mockup).
 * Open: Options → Preferences…, or VegasPreferences.open().
 */
(function () {
  const WIN_ID = "preferences";

  const TAB_ROWS = [
    [
      { id: "ext", label: "External Control & Automation" },
      { id: "deprecated", label: "Deprecated Features" },
      { id: "fileio", label: "File I/O" },
    ],
    [
      { id: "editing", label: "Editing" },
      { id: "display", label: "Display" },
      { id: "cd", label: "CD Settings" },
      { id: "sync", label: "Sync" },
    ],
    [
      { id: "general", label: "General" },
      { id: "video", label: "Video" },
      { id: "preview", label: "Preview Device" },
      { id: "audio", label: "Audio" },
      { id: "audiodev", label: "Audio Device" },
      { id: "midi", label: "MIDI" },
      { id: "vst", label: "VST Effects" },
    ],
  ];

  const GENERAL_OPTS = [
    { label: "Automatically open last project on startup", checked: true, selected: true },
    { label: "Confirm media file deletion when still in use", checked: true },
    { label: "Save active prerenders on project close", checked: true },
    { label: "Close media files when not the active application", checked: true },
    { label: "Close audio and MIDI ports when not the active application", checked: true },
    { label: "Use Newsfeed to stay informed about product updates", checked: true },
    { label: "Enable autosave", checked: true },
    { label: "Prompt to keep files after recording", checked: true },
    { label: "Create undos for FX parameter changes", checked: true },
    { label: "Keep bypassed FX running (to avoid pause on bypass/enable)", checked: false },
    { label: "Automatically name regions and markers if not playing", checked: true },
    { label: "Use linear scrub range", checked: true },
    { label: "Allow Ctrl+drag cursor style scrub over events", checked: false },
    { label: "Make spacebar and F12 Play/Pause instead of Play/Stop", checked: false },
    { label: "Always draw marker lines", checked: true },
    { label: "Allow edit cursor to be dragged", checked: true },
    { label: "Double-click on media file loads into Trimmer instead of tracks", checked: false },
    { label: "Show video when editing in event reverse mode", checked: true },
    { label: "Enable Windows OS soft keyboard", checked: false },
    { label: "Prompt to adjust project settings to match first media added to timeline", checked: true },
    { label: "Check project file type associations at startup", checked: true },
    { label: "Enable joystick support", checked: false },
    { label: "Allow pulldown removal when opening 24p DV", checked: true },
    { label: "AAF Export - Use frame unit for audio", checked: true },
    { label: "AAF Export - Use clip-based audio envelope", checked: false },
    { label: "Import stereo as dual mono", checked: false },
    { label: "Use GPU processing for ACES color management", checked: true },
  ];

  function buildMarkup() {
    return (
      '<div class="pref-dialog" role="dialog" aria-modal="true" aria-labelledby="pref-title">' +
      '<div class="pref-dialog__titlebar" data-pref-drag>' +
      '<span class="pref-dialog__title" id="pref-title">Preferences</span>' +
      '<div class="pref-dialog__winbtns">' +
      '<button type="button" class="pref-winbtn" title="Help" aria-label="Help">?</button>' +
      '<button type="button" class="pref-winbtn pref-winbtn--close" data-pref-close title="Close" aria-label="Close">✕</button>' +
      "</div></div>" +
      '<div class="pref-dialog__body">' +
      '<div class="pref-tabs" role="tablist">' +
      TAB_ROWS.map((row, ri) => {
        return (
          '<div class="pref-tabs__row">' +
          row
            .map((t) => {
              const on = t.id === "general";
              return (
                '<button type="button" class="pref-tab' +
                (on ? " is-active" : "") +
                '" role="tab" aria-selected="' +
                on +
                '" data-pref-tab="' +
                t.id +
                '">' +
                t.label +
                "</button>"
              );
            })
            .join("") +
          "</div>"
        );
      }).join("") +
      "</div>" +
      '<div class="pref-panels">' +
      panelGeneral() +
      TAB_ROWS.flat()
        .filter((t) => t.id !== "general")
        .map((t) => panelStub(t.id, t.label))
        .join("") +
      "</div>" +
      '<div class="pref-dialog__foot-tools">' +
      '<button type="button" class="pref-btn" data-pref-defaults>Default All</button>' +
      "</div>" +
      "</div>" +
      '<div class="pref-dialog__footer">' +
      '<div class="pref-dialog__actions">' +
      '<button type="button" class="pref-btn pref-btn--default" data-pref-ok>OK</button>' +
      '<button type="button" class="pref-btn" data-pref-close>Cancel</button>' +
      '<button type="button" class="pref-btn" data-pref-apply disabled>Apply</button>' +
      "</div></div></div>"
    );
  }

  function panelGeneral() {
    const items = GENERAL_OPTS.map((o, i) => {
      return (
        '<label class="pref-opt' +
        (o.selected ? " is-selected" : "") +
        '" data-pref-opt>' +
        '<input type="checkbox" data-pref-field' +
        (o.checked ? " checked" : "") +
        " />" +
        "<span>" +
        o.label +
        "</span></label>"
      );
    }).join("");

    return (
      '<div class="pref-panel is-active" data-pref-panel="general" role="tabpanel">' +
      '<div class="pref-label">General preferences:</div>' +
      '<div class="pref-checklist" data-pref-list tabindex="0">' +
      items +
      "</div>" +
      '<div class="pref-row pref-row--recent">' +
      '<label class="pref-check"><input type="checkbox" data-pref-field checked /><span>Recently used project list:</span></label>' +
      '<div class="pref-spin">' +
      '<input type="text" value="6" data-pref-field aria-label="Recently used project list count" />' +
      '<div class="pref-spin__btns">' +
      '<button type="button" tabindex="-1" aria-label="Increase">▴</button>' +
      '<button type="button" tabindex="-1" aria-label="Decrease">▾</button>' +
      "</div></div></div>" +
      '<div class="pref-label" style="margin-top:10px">Temporary files folder:</div>' +
      '<div class="pref-path-row">' +
      '<input type="text" class="pref-field" data-pref-field value="C:\\Users\\Admin\\AppData\\Local\\VEGAS Pro\\22.0\\" />' +
      '<button type="button" class="pref-btn">Browse...</button>' +
      "</div>" +
      '<p class="pref-free">Free storage space in selected folder: 656,0 Gigabytes</p>' +
      "</div>"
    );
  }

  function panelStub(id, label) {
    return (
      '<div class="pref-panel" data-pref-panel="' +
      id +
      '" role="tabpanel" hidden>' +
      '<div class="pref-stub">' +
      "<p><strong>" +
      label +
      "</strong></p>" +
      "<p class=\"pref-stub__hint\">Параметры этой вкладки (макет OpenVegas)</p>" +
      "</div></div>"
    );
  }

  function ensure() {
    let root = document.getElementById(WIN_ID);
    if (root) return root;
    root = document.createElement("div");
    root.id = WIN_ID;
    root.className = "pref-backdrop";
    root.hidden = true;
    root.innerHTML = buildMarkup();
    document.body.appendChild(root);
    wire(root);
    return root;
  }

  function setDirty(root, dirty) {
    const apply = root.querySelector("[data-pref-apply]");
    if (apply) apply.disabled = !dirty;
    root.dataset.dirty = dirty ? "1" : "0";
  }

  function activateTab(root, id) {
    root.querySelectorAll("[data-pref-tab]").forEach((btn) => {
      const on = btn.dataset.prefTab === id;
      btn.classList.toggle("is-active", on);
      btn.setAttribute("aria-selected", on ? "true" : "false");
    });
    root.querySelectorAll("[data-pref-panel]").forEach((panel) => {
      const on = panel.dataset.prefPanel === id;
      panel.classList.toggle("is-active", on);
      panel.hidden = !on;
    });
  }

  function wire(root) {
    const dialog = root.querySelector(".pref-dialog");

    root.querySelectorAll("[data-pref-tab]").forEach((btn) => {
      btn.addEventListener("click", () => activateTab(root, btn.dataset.prefTab));
    });

    root.querySelectorAll("[data-pref-close]").forEach((btn) => {
      btn.addEventListener("click", () => close());
    });

    root.querySelector("[data-pref-ok]")?.addEventListener("click", () => {
      setDirty(root, false);
      close();
    });

    root.querySelector("[data-pref-apply]")?.addEventListener("click", () => setDirty(root, false));

    root.querySelector("[data-pref-defaults]")?.addEventListener("click", () => {
      root.querySelectorAll("[data-pref-panel='general'] input[type='checkbox']").forEach((cb, i) => {
        const def = GENERAL_OPTS[i];
        if (def) cb.checked = !!def.checked;
      });
      const spin = root.querySelector(".pref-spin input");
      if (spin) spin.value = "6";
      setDirty(root, true);
    });

    root.addEventListener("mousedown", (e) => {
      if (e.target === root) close();
    });

    root.addEventListener("input", () => setDirty(root, true));
    root.addEventListener("change", () => setDirty(root, true));

    const list = root.querySelector("[data-pref-list]");
    list?.addEventListener("click", (e) => {
      const opt = e.target.closest("[data-pref-opt]");
      if (!opt) return;
      list.querySelectorAll("[data-pref-opt]").forEach((el) => el.classList.remove("is-selected"));
      opt.classList.add("is-selected");
    });

    root.querySelectorAll(".pref-spin__btns button").forEach((btn) => {
      btn.addEventListener("click", () => {
        const input = btn.closest(".pref-spin")?.querySelector("input");
        if (!input) return;
        let n = parseInt(input.value, 10);
        if (Number.isNaN(n)) n = 0;
        const up = btn.getAttribute("aria-label") === "Increase";
        input.value = String(Math.max(0, Math.min(32, up ? n + 1 : n - 1)));
        setDirty(root, true);
      });
    });

    const drag = root.querySelector("[data-pref-drag]");
    if (drag && dialog) {
      let ox = 0;
      let oy = 0;
      let dragging = false;
      drag.addEventListener("mousedown", (e) => {
        if (e.target.closest(".pref-winbtn")) return;
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
    activateTab(root, tab || "general");
    setDirty(root, false);
    setTimeout(() => {
      root.querySelector(".pref-opt.is-selected")?.focus?.();
    }, 0);
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

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && isOpen()) {
      e.preventDefault();
      close();
    }
  });

  document.addEventListener("click", (e) => {
    const item = e.target.closest(".dropdown-menu__item");
    if (!item || item.classList.contains("is-disabled")) return;
    const label = (item.querySelector(".label")?.textContent || "").trim();
    const action = item.getAttribute("data-action") || "";
    if (action === "preferences" || label === "Preferences...") {
      e.stopPropagation();
      open();
    }
  });

  document.addEventListener("vegas:menu-action", (e) => {
    const action = e.detail?.action || "";
    const label = e.detail?.label || "";
    if (action === "preferences" || label === "Preferences...") open();
  });

  document.addEventListener("vegas:context-action", (e) => {
    const label = e.detail?.label || "";
    if (label === "Preferences...") open();
  });

  window.VegasPreferences = { open, close, isOpen };

  if (document.body?.dataset.prefOpen === "1") {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", () => open());
    } else {
      open();
    }
  }
})();
