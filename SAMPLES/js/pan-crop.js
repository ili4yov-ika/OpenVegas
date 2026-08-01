/**
 * Video Event FX — Event Pan/Crop window (Vegas-style mockup).
 */
(function () {
  const WIN_ID = "pan-crop-window";

  function mediaNameFromEvent(eventEl) {
    if (!eventEl) return "sample_for_project_video";
    if (eventEl.classList?.contains("track-header")) {
      return (
        eventEl.dataset.trackName ||
        eventEl.querySelector(".track-header__name")?.textContent?.trim() ||
        (eventEl.classList.contains("track-header--audio") ? "Audio" : "Video")
      );
    }
    const label = eventEl.querySelector(".event__label")?.textContent?.trim() || "";
    return label.replace(/\.[^.]+$/, "") || label || "sample_for_project_video";
  }

  function propRow(label, value, opts) {
    const yesNo = opts && opts.yesNo;
    return (
      '<div class="pc-prop">' +
      '<span class="pc-prop__name">' +
      label +
      "</span>" +
      '<span class="pc-prop__val' +
      (yesNo ? " pc-prop__val--choice" : "") +
      '">' +
      value +
      "</span>" +
      "</div>"
    );
  }

  function propGroup(title, rowsHtml) {
    return (
      '<details class="pc-group" open>' +
      "<summary>" +
      title +
      "</summary>" +
      '<div class="pc-group__body">' +
      rowsHtml +
      "</div>" +
      "</details>"
    );
  }

  function colorWheelBlock(label) {
    return (
      '<details class="cc-band" open>' +
      "<summary>" +
      label +
      ":</summary>" +
      '<div class="cc-band__body">' +
      '<div class="cc-band__coords">' +
      '<input type="text" value="0,0 , 0,000" readonly aria-label="' +
      label +
      ' coords" />' +
      '<button type="button" class="cc-anim" title="Animate parameter">◷</button>' +
      "</div>" +
      '<div class="cc-wheel-row">' +
      '<div class="cc-wheel" aria-hidden="true">' +
      '<span class="cc-wheel__disc"></span>' +
      '<span class="cc-wheel__cross"></span>' +
      '<span class="cc-lab cc-lab--r">R</span>' +
      '<span class="cc-lab cc-lab--mg">Mg</span>' +
      '<span class="cc-lab cc-lab--b">B</span>' +
      '<span class="cc-lab cc-lab--cy">Cy</span>' +
      '<span class="cc-lab cc-lab--g">G</span>' +
      '<span class="cc-lab cc-lab--yl">Yl</span>' +
      "</div>" +
      '<div class="cc-eyedrop">' +
      '<button type="button" class="cc-drop cc-drop--add" title="Pick color">+</button>' +
      '<button type="button" class="cc-drop cc-drop--sub" title="Clear color">−</button>' +
      "</div>" +
      "</div>" +
      "</div>" +
      "</details>"
    );
  }

  function ccSliderRow(label, value) {
    return (
      '<div class="cc-slider">' +
      "<label>" +
      label +
      "</label>" +
      '<input type="range" min="0" max="200" value="100" aria-label="' +
      label +
      '" />' +
      '<input type="text" class="cc-slider__val" value="' +
      value +
      '" readonly />' +
      '<button type="button" class="cc-anim" title="Animate parameter">◷</button>' +
      "</div>"
    );
  }

  function colorCorrectorMarkup() {
    return (
      '<div class="pc-view pc-view--cc is-hidden" data-pc-view="Color Corrector">' +
      '<div class="cc-host">' +
      '<div class="cc-host__head">' +
      "<strong>Color Corrector</strong>" +
      '<div class="cc-host__links"><button type="button">About</button><button type="button">?</button></div>' +
      "</div>" +
      colorWheelBlock("Low") +
      colorWheelBlock("Mid") +
      colorWheelBlock("High") +
      '<div class="cc-sliders">' +
      ccSliderRow("Saturation", "1,000") +
      ccSliderRow("Gamma", "1,000") +
      ccSliderRow("Gain", "1,000") +
      ccSliderRow("Offset", "0,000") +
      "</div>" +
      "</div>" +
      "</div>"
    );
  }

  function buildWindow() {
    let win = document.getElementById(WIN_ID);
    if (win) return win;

    win = document.createElement("div");
    win.id = WIN_ID;
    win.className = "pc-window";
    win.setAttribute("role", "dialog");
    win.setAttribute("aria-label", "Video Event FX");
    win.hidden = true;
    win.dataset.activeFx = "Pan/Crop";

    win.innerHTML =
      '<div class="pc-window__titlebar" data-pc-drag>' +
      '<span class="pc-window__title">Video Event FX</span>' +
      '<div class="pc-window__winbtns">' +
      '<button type="button" class="pc-winbtn" title="Minimize" aria-label="Minimize">─</button>' +
      '<button type="button" class="pc-winbtn" title="Maximize" aria-label="Maximize">☐</button>' +
      '<button type="button" class="pc-winbtn pc-winbtn--close" data-pc-close title="Close" aria-label="Close">✕</button>' +
      "</div>" +
      "</div>" +
      '<div class="pc-window__subheader">' +
      '<span class="pc-window__fxname" data-pc-subtitle>Event Pan/Crop: <em data-pc-media>sample_for_project_video</em></span>' +
      '<div class="pc-window__subtools">' +
      '<button type="button" class="pc-chip is-on" title="Enable effect" aria-label="Enable"><span></span><span></span></button>' +
      '<button type="button" class="pc-chip" title="Bypass" aria-label="Bypass"><span></span><span></span></button>' +
      '<button type="button" class="pc-ico-btn" title="Add Plug-In" aria-label="Add Plug-In">fx+</button>' +
      '<button type="button" class="pc-ico-btn" title="Remove Plug-In" aria-label="Remove">fx×</button>' +
      '<button type="button" class="pc-ico-btn" title="More" aria-label="More">≡</button>' +
      "</div>" +
      "</div>" +
      '<div class="pc-chain" data-pc-chain></div>' +
      '<div class="pc-preset">' +
      "<label>Preset:</label>" +
      '<select aria-label="Preset"><option>(Default)</option></select>' +
      '<button type="button" class="pc-ico-btn" title="Save Preset" aria-label="Save">S</button>' +
      '<button type="button" class="pc-ico-btn" title="Delete Preset" aria-label="Delete">✕</button>' +
      "</div>" +
      '<div class="pc-view pc-view--pancrop" data-pc-view="Pan/Crop">' +
      '<div class="pc-main">' +
      '<div class="pc-tools" aria-label="Tools">' +
      '<button type="button" class="pc-tool" title="Settings" aria-label="Settings">⚙</button>' +
      '<button type="button" class="pc-tool is-active" title="Normal Edit" aria-label="Edit">↖</button>' +
      '<button type="button" class="pc-tool" title="Zoom" aria-label="Zoom">+</button>' +
      '<button type="button" class="pc-tool" title="Enable Snapping" aria-label="Snap">#</button>' +
      '<button type="button" class="pc-tool" title="Lock Aspect Ratio" aria-label="Lock">⬚</button>' +
      '<button type="button" class="pc-tool" title="Size About Center" aria-label="Center">◎</button>' +
      "</div>" +
      '<div class="pc-props">' +
      propGroup(
        "Position",
        propRow("Width", "1 920,000") +
          propRow("Height", "1 080,000") +
          propRow("X Center", "960,000") +
          propRow("Y Center", "540,000")
      ) +
      propGroup(
        "Rotation",
        propRow("Angle", "0,000") + propRow("X Center", "960,000") + propRow("Y Center", "540,000")
      ) +
      propGroup("Keyframe interpolation", propRow("Smoothness", "0,000")) +
      propGroup(
        "Source",
        propRow("Maintain aspect ratio", "Yes", { yesNo: true }) +
          propRow("Stretch to fill frame", "Yes", { yesNo: true })
      ) +
      propGroup(
        "Workspace",
        propRow("Zoom", "30,900 %") +
          propRow("X Offset", "0,000") +
          propRow("Y Offset", "0,000") +
          propRow("Grid spacing", "16")
      ) +
      "</div>" +
      '<div class="pc-canvas-wrap">' +
      '<div class="pc-canvas" aria-hidden="true">' +
      '<div class="pc-frame">' +
      '<div class="pc-frame__media"></div>' +
      '<div class="pc-frame__box">' +
      '<span class="pc-h pc-h--tl"></span><span class="pc-h pc-h--t"></span><span class="pc-h pc-h--tr"></span>' +
      '<span class="pc-h pc-h--l"></span><span class="pc-h pc-h--r"></span>' +
      '<span class="pc-h pc-h--bl"></span><span class="pc-h pc-h--b"></span><span class="pc-h pc-h--br"></span>' +
      '<span class="pc-cross"></span>' +
      "</div>" +
      '<div class="pc-orbit"></div>' +
      "</div>" +
      "</div>" +
      "</div>" +
      "</div>" +
      '<div class="pc-kf">' +
      '<div class="pc-kf__headers">' +
      '<div class="pc-kf__corner"></div>' +
      '<div class="pc-kf__ruler">' +
      '<span style="left:0">00:00:00</span>' +
      '<span style="left:20%">00:00:05</span>' +
      '<span style="left:40%">00:00:10</span>' +
      '<span style="left:60%">00:00:15</span>' +
      '<span style="left:80%">00:00:20</span>' +
      "</div>" +
      "</div>" +
      '<div class="pc-kf__row is-active">' +
      '<div class="pc-kf__label"><input type="checkbox" checked aria-label="Position" /> Position</div>' +
      '<div class="pc-kf__lane">' +
      '<span class="pc-kf__diamond" style="left:0"></span>' +
      '<span class="pc-kf__diamond" style="left:12.5%"></span>' +
      "</div>" +
      "</div>" +
      '<div class="pc-kf__row">' +
      '<div class="pc-kf__label"><input type="checkbox" aria-label="Mask" /> Mask</div>' +
      '<div class="pc-kf__lane"></div>' +
      "</div>" +
      '<div class="pc-kf__playhead" aria-hidden="true"></div>' +
      '<div class="pc-kf__toolbar">' +
      '<div class="pc-kf__tb-left">' +
      '<button type="button" class="pc-ico-btn is-on" title="Sync Cursor" aria-label="Sync">⇄</button>' +
      '<button type="button" class="pc-ico-btn" title="Create Keyframe" aria-label="Add">+</button>' +
      '<button type="button" class="pc-ico-btn" title="Delete Keyframe" aria-label="Remove">−</button>' +
      "</div>" +
      '<span class="pc-kf__tc">00:00:02,703</span>' +
      "</div>" +
      "</div>" +
      "</div>" +
      colorCorrectorMarkup() +
      '<div class="pc-view pc-view--generic is-hidden" data-pc-view="generic">' +
      '<div class="pc-generic"><p data-pc-generic-name>Plug-In</p><p class="pc-generic__hint">Параметры эффекта (макет)</p></div>' +
      "</div>";

    document.body.appendChild(win);
    wireWindow(win);
    return win;
  }

  function wireWindow(win) {
    win.querySelectorAll("[data-pc-close]").forEach((btn) => {
      btn.addEventListener("click", () => closePanCrop());
    });

    const bar = win.querySelector("[data-pc-drag]");
    if (bar) {
      bar.addEventListener("pointerdown", (e) => {
        if (e.button !== 0) return;
        if (e.target.closest("button")) return;
        e.preventDefault();
        const rect = win.getBoundingClientRect();
        const ox = e.clientX - rect.left;
        const oy = e.clientY - rect.top;
        win.classList.add("is-dragging");

        function onMove(ev) {
          const x = Math.max(0, Math.min(window.innerWidth - 80, ev.clientX - ox));
          const y = Math.max(0, Math.min(window.innerHeight - 40, ev.clientY - oy));
          win.style.left = x + "px";
          win.style.top = y + "px";
          win.style.transform = "none";
        }
        function onUp() {
          win.classList.remove("is-dragging");
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        }
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      });
    }

    wireFxResize(win, { minW: 520, minH: 360 });

    win.querySelectorAll(".pc-tool").forEach((btn) => {
      btn.addEventListener("click", () => {
        win.querySelectorAll(".pc-tool").forEach((b) => b.classList.remove("is-active"));
        btn.classList.add("is-active");
      });
    });
  }

  function wireFxResize(win, opts) {
    if (!win || win.dataset.resizeWired) return;
    win.dataset.resizeWired = "1";
    const minW = opts?.minW || 420;
    const minH = opts?.minH || 320;

    let handle = win.querySelector(".fx-resize");
    if (!handle) {
      handle = document.createElement("div");
      handle.className = "fx-resize";
      handle.title = "Resize";
      handle.setAttribute("aria-hidden", "true");
      win.appendChild(handle);
    }

    handle.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      e.stopPropagation();
      const startW = win.offsetWidth;
      const startH = win.offsetHeight;
      const sx = e.clientX;
      const sy = e.clientY;
      const left = win.offsetLeft;
      const top = win.offsetTop;
      win.classList.add("is-resizing");
      document.body.classList.add("is-scrubbing-playhead");

      function onMove(ev) {
        const maxW = Math.max(minW, window.innerWidth - left - 8);
        const maxH = Math.max(minH, window.innerHeight - top - 8);
        const w = Math.max(minW, Math.min(maxW, startW + (ev.clientX - sx)));
        const h = Math.max(minH, Math.min(maxH, startH + (ev.clientY - sy)));
        win.style.width = Math.round(w) + "px";
        win.style.height = Math.round(h) + "px";
      }
      function onUp() {
        win.classList.remove("is-resizing");
        document.body.classList.remove("is-scrubbing-playhead");
        window.removeEventListener("pointermove", onMove);
        window.removeEventListener("pointerup", onUp);
      }
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });
  }

  function getVideoChain(eventEl) {
    if (!eventEl) return [];
    const raw = (eventEl.dataset.fxChain || "").trim();
    if (!raw) return [];
    return raw
      .split("|")
      .map((s) => s.trim())
      .filter(Boolean);
  }

  function setVideoChain(eventEl, names) {
    if (!eventEl) return;
    eventEl.dataset.fxChain = (names || []).join("|");
    window.VegasTimelineChrome?.syncEventFxBadge?.(eventEl);
    refreshVideoChainUi(eventEl);
  }

  function selectFxView(win, fxId) {
    if (!win) return;
    const id = fxId || "Pan/Crop";
    win.dataset.activeFx = id;

    win.querySelectorAll(".pc-chain__item").forEach((el) => {
      el.classList.toggle("is-active", el.getAttribute("data-fx") === id);
    });

    const isPan = id === "Pan/Crop";
    const isCc = /^color\s*corrector$/i.test(id);

    win.querySelectorAll(".pc-view").forEach((view) => {
      const key = view.getAttribute("data-pc-view");
      let show = false;
      if (isPan && key === "Pan/Crop") show = true;
      else if (isCc && key === "Color Corrector") show = true;
      else if (!isPan && !isCc && key === "generic") show = true;
      view.classList.toggle("is-hidden", !show);
    });

    const media = win.querySelector("[data-pc-media]")?.textContent || "";
    const subtitle = win.querySelector("[data-pc-subtitle]");
    const title = win.querySelector(".pc-window__title");
    if (isPan) {
      if (title) title.textContent = "Video Event FX";
      if (subtitle) {
        subtitle.innerHTML = 'Event Pan/Crop: <em data-pc-media>' + media + "</em>";
      }
    } else {
      if (title) title.textContent = "Video Event FX: " + media;
      if (subtitle) {
        subtitle.innerHTML = "<em data-pc-media>" + media + "</em>";
      }
      const genericName = win.querySelector("[data-pc-generic-name]");
      if (genericName) genericName.textContent = id;
    }
  }

  function refreshVideoChainUi(eventEl, preferFx) {
    const win = document.getElementById(WIN_ID);
    if (!win || win.hidden) return;
    const chainEl = win.querySelector("[data-pc-chain]");
    if (!chainEl) return;

    const extras = getVideoChain(eventEl).filter((n) => n.toLowerCase() !== "pan/crop");
    const active =
      preferFx ||
      win.dataset.activeFx ||
      (extras.length ? extras[extras.length - 1] : "Pan/Crop");

    let html =
      '<button type="button" class="pc-chain__item" data-fx="Pan/Crop">' +
      '<span class="pc-chain__dot"></span>Pan/Crop</button>';
    extras.forEach((name) => {
      html +=
        '<button type="button" class="pc-chain__item" data-fx="' +
        name.replace(/"/g, "") +
        '">' +
        '<input type="checkbox" checked tabindex="-1" aria-hidden="true" />' +
        name +
        "</button>";
    });
    chainEl.innerHTML = html;

    chainEl.querySelectorAll(".pc-chain__item").forEach((btn) => {
      btn.addEventListener("click", (e) => {
        if (e.target.closest("input")) e.preventDefault();
        selectFxView(win, btn.getAttribute("data-fx"));
      });
    });

    selectFxView(win, active);
  }

  function eventHasExtraPlugins(eventEl) {
    return getVideoChain(eventEl).some((name) => name.toLowerCase() !== "pan/crop");
  }

  function openPanCrop(eventEl, preferFx) {
    const win = buildWindow();
    const name = mediaNameFromEvent(eventEl);
    win._eventEl = eventEl || null;

    win.hidden = false;
    win.classList.add("is-open");

    // Seed media text before chain refresh rewrites subtitle
    const mediaEl = win.querySelector("[data-pc-media]");
    if (mediaEl) mediaEl.textContent = name;

    const extras = getVideoChain(eventEl).filter((n) => n.toLowerCase() !== "pan/crop");
    const initial =
      preferFx ||
      (extras.length ? extras[extras.length - 1] : "Pan/Crop");
    refreshVideoChainUi(eventEl, initial);

    // Keep media name after subtitle rewrite
    win.querySelectorAll("[data-pc-media]").forEach((el) => {
      el.textContent = name;
    });

    if (!win.style.left) {
      const w = win.offsetWidth || 920;
      const h = win.offsetHeight || 620;
      win.style.width = w + "px";
      win.style.height = h + "px";
      win.style.left = Math.max(24, Math.round((window.innerWidth - w) / 2) - 40) + "px";
      win.style.top = Math.max(24, Math.round((window.innerHeight - h) / 2) - 20) + "px";
      win.style.transform = "none";
    }
  }

  function closePanCrop() {
    const win = document.getElementById(WIN_ID);
    if (!win) return;
    win.classList.remove("is-open");
    win.hidden = true;
  }

  const CHOOSER_ID = "plugin-chooser-window";

  const PLUGIN_FOLDERS = [
    { id: "fx", label: "FX", children: true },
    { id: "fp", label: "32-bit floating point" },
    { id: "pack", label: "Filter Packages" },
    { id: "gpu", label: "GPU Accelerated" },
    { id: "new", label: "Newly Installed" },
    { id: "ofx", label: "OFX" },
    { id: "sony", label: "Sony" },
    { id: "third", label: "Third Party" },
    { id: "vegas", label: "VEGAS", active: true },
  ];

  const VEGAS_PLUGINS = [
    "Add Noise",
    "AI Auto Reframe",
    "Black and White",
    "Black Restore",
    "Border",
    "Brightness and Contrast",
    "Broadcast Colors",
    "Bump Map",
    "Channel Blend",
    "Color Balance",
    "Color Corrector",
    "Color Corrector (Secondary)",
    "Color Curves",
    "Color Match",
    "Convolution Kernel",
    "Cookie Cutter",
    "Defocus",
    "Deform",
    "Fill Light",
    "Film Effects",
    "Gaussian Blur",
    "Glow",
    "Gradient Map",
    "HSL Adjust",
    "Invert",
    "Lens Flare",
    "Levels",
    "Linear Blur",
    "Mask Generator",
    "Median",
    "Mirror",
    "News Print",
    "Page Curl",
    "Picture In Picture",
    "Pinch / Punch",
    "Pixelate",
    "Quick Flip",
    "Radial Blur",
    "Saturation Adjust",
    "Sepia",
    "Sharpen",
    "Spherize",
    "Swirl",
    "Timecode",
    "TV Simulator",
    "Unsharp Mask",
    "Wave",
    "White Balance",
  ];

  function buildChooser() {
    let win = document.getElementById(CHOOSER_ID);
    if (win) return win;

    win = document.createElement("div");
    win.id = CHOOSER_ID;
    win.className = "plc-window";
    win.setAttribute("role", "dialog");
    win.setAttribute("aria-label", "Plug-In Chooser - Video Event FX");
    win.hidden = true;

    const folders = PLUGIN_FOLDERS.map((f) => {
      const cls =
        "plc-tree__item" +
        (f.children ? " plc-tree__item--root is-open" : "") +
        (f.active ? " is-selected" : "");
      const pad = f.children ? "" : ' style="padding-left:18px"';
      return (
        '<button type="button" class="' +
        cls +
        '" data-folder="' +
        f.id +
        '"' +
        pad +
        ">" +
        (f.children ? '<span class="plc-tree__twist">▾</span>' : "") +
        '<span class="plc-tree__ico"></span>' +
        f.label +
        "</button>"
      );
    }).join("");

    const plugins = VEGAS_PLUGINS.map(
      (name, i) =>
        '<button type="button" class="plc-plugin' +
        (i === 0 ? " is-selected" : "") +
        '" data-plugin="' +
        name +
        '">' +
        name +
        "</button>"
    ).join("");

    win.innerHTML =
      '<div class="plc-window__titlebar" data-plc-drag>' +
      '<span class="plc-window__title">Plug-In Chooser - Video Event FX</span>' +
      '<div class="pc-window__winbtns">' +
      '<button type="button" class="pc-winbtn" title="Minimize" aria-label="Minimize">─</button>' +
      '<button type="button" class="pc-winbtn" title="Maximize" aria-label="Maximize">☐</button>' +
      '<button type="button" class="pc-winbtn pc-winbtn--close" data-plc-close title="Close" aria-label="Close">✕</button>' +
      "</div>" +
      "</div>" +
      '<div class="plc-status"><input type="text" readonly value="No plug-ins in chain" aria-label="Plug-in chain" /></div>' +
      '<div class="plc-body">' +
      '<div class="plc-tree" aria-label="FX folders">' +
      folders +
      "</div>" +
      '<div class="plc-plugins" aria-label="Plug-ins">' +
      plugins +
      "</div>" +
      '<div class="plc-actions">' +
      '<button type="button" class="plc-btn" data-plc-ok>OK</button>' +
      '<button type="button" class="plc-btn" data-plc-close>Cancel</button>' +
      '<button type="button" class="plc-btn" data-plc-add>Add</button>' +
      '<button type="button" class="plc-btn" disabled>Replace</button>' +
      '<button type="button" class="plc-btn" disabled>Remove</button>' +
      '<button type="button" class="plc-btn" disabled>Remove All</button>' +
      '<button type="button" class="plc-btn">Save As...</button>' +
      "</div>" +
      "</div>";

    document.body.appendChild(win);
    wireChooser(win);
    return win;
  }

  function selectedVideoPlugin(win) {
    return win.querySelector(".plc-plugin.is-selected")?.dataset?.plugin || "";
  }

  function applyVideoPlugin(win) {
    const name = selectedVideoPlugin(win);
    const eventEl = win._eventEl;
    if (!name || !eventEl) {
      closePluginChooser();
      return;
    }
    const chain = getVideoChain(eventEl).filter((n) => n.toLowerCase() !== "pan/crop");
    if (!chain.includes(name)) chain.push(name);
    setVideoChain(eventEl, chain);
    closePluginChooser();
    // Track FX: keep chooser result on the track; Event FX opens the editor window.
    if (eventEl.classList?.contains("track-header")) {
      window.VegasTimelineChrome?.syncEventFxBadge?.(eventEl);
      return;
    }
    openPanCrop(eventEl, name);
  }

  function wireChooser(win) {
    win.querySelectorAll("[data-plc-close]").forEach((btn) => {
      btn.addEventListener("click", () => closePluginChooser());
    });
    win.querySelector("[data-plc-ok]")?.addEventListener("click", () => applyVideoPlugin(win));
    win.querySelector("[data-plc-add]")?.addEventListener("click", () => applyVideoPlugin(win));

    win.querySelectorAll(".plc-plugin").forEach((btn) => {
      btn.addEventListener("click", () => {
        win.querySelectorAll(".plc-plugin").forEach((b) => b.classList.remove("is-selected"));
        btn.classList.add("is-selected");
      });
      btn.addEventListener("dblclick", () => applyVideoPlugin(win));
    });

    win.querySelectorAll(".plc-tree__item").forEach((btn) => {
      btn.addEventListener("click", () => {
        win.querySelectorAll(".plc-tree__item").forEach((b) => b.classList.remove("is-selected"));
        btn.classList.add("is-selected");
      });
    });

    const bar = win.querySelector("[data-plc-drag]");
    if (bar) {
      bar.addEventListener("pointerdown", (e) => {
        if (e.button !== 0) return;
        if (e.target.closest("button")) return;
        e.preventDefault();
        const rect = win.getBoundingClientRect();
        const ox = e.clientX - rect.left;
        const oy = e.clientY - rect.top;
        win.classList.add("is-dragging");
        function onMove(ev) {
          const x = Math.max(0, Math.min(window.innerWidth - 80, ev.clientX - ox));
          const y = Math.max(0, Math.min(window.innerHeight - 40, ev.clientY - oy));
          win.style.left = x + "px";
          win.style.top = y + "px";
          win.style.transform = "none";
        }
        function onUp() {
          win.classList.remove("is-dragging");
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        }
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      });
    }
  }

  function openPluginChooser(eventEl) {
    const win = buildChooser();
    win._eventEl = eventEl || null;
    const isTrack = !!eventEl?.classList?.contains("track-header");
    const title = win.querySelector(".plc-window__title");
    if (title) {
      title.textContent = isTrack
        ? "Plug-In Chooser - Video Track FX"
        : "Plug-In Chooser - Video Event FX";
    }
    win.setAttribute(
      "aria-label",
      isTrack ? "Plug-In Chooser - Video Track FX" : "Plug-In Chooser - Video Event FX"
    );
    const status = win.querySelector(".plc-status input");
    if (status) {
      status.value = eventHasExtraPlugins(eventEl)
        ? (eventEl.dataset.fxChain || "").replace(/\|/g, " → ")
        : "No plug-ins in chain";
    }

    win.hidden = false;
    win.classList.add("is-open");

    if (!win.style.left) {
      const fx = document.getElementById(WIN_ID);
      if (fx && !fx.hidden) {
        const r = fx.getBoundingClientRect();
        win.style.left = Math.min(window.innerWidth - 520, Math.round(r.left + 120)) + "px";
        win.style.top = Math.max(20, Math.round(r.top + 48)) + "px";
      } else {
        win.style.left = Math.max(40, Math.round((window.innerWidth - 640) / 2)) + "px";
        win.style.top = Math.max(40, Math.round((window.innerHeight - 480) / 2)) + "px";
      }
      win.style.transform = "none";
    }
  }

  /** Video Track FX — plug-in chooser for a video track header. */
  function openVideoTrackFx(header) {
    if (!header) return;
    openPluginChooser(header);
  }

  function closePluginChooser() {
    const win = document.getElementById(CHOOSER_ID);
    if (!win) return;
    win.classList.remove("is-open");
    win.hidden = true;
  }

  /** Video Event FX button: open Pan/Crop; if chain empty → also Plug-In Chooser */
  function openVideoEventFx(eventEl) {
    openPanCrop(eventEl);
    if (!eventHasExtraPlugins(eventEl)) {
      openPluginChooser(eventEl);
    }
  }

  document.addEventListener("DOMContentLoaded", () => {
    // Capture phase: runs even if tracks-area stops bubble on .event clicks.
    document.addEventListener(
      "click",
      (e) => {
        const pcBtn = e.target.closest(".event-btn--pc");
        if (pcBtn) {
          e.preventDefault();
          e.stopPropagation();
          openPanCrop(pcBtn.closest(".event"), "Pan/Crop");
          return;
        }

        const fxBtn = e.target.closest(".event-btn--fx");
        if (fxBtn && fxBtn.closest(".event--video")) {
          e.preventDefault();
          e.stopPropagation();
          openVideoEventFx(fxBtn.closest(".event"));
          return;
        }

        const addBtn = e.target.closest("#pan-crop-window .pc-ico-btn[title='Add Plug-In']");
        if (addBtn) {
          e.preventDefault();
          const fxWin = document.getElementById(WIN_ID);
          openPluginChooser(fxWin?._eventEl || null);
        }
      },
      true
    );

    document.addEventListener("vegas:context-action", (e) => {
      const { type, action, label, target } = e.detail || {};
      const eventEl =
        (target && target.closest?.(".event")) ||
        (type === "video-event" ? target : null) ||
        document.querySelector(".event.event--video.is-selected") ||
        document.querySelector(".event.event--video");

      const isPanCrop =
        action === "video-pan-crop" ||
        (typeof label === "string" && /Pan\/Crop/i.test(label));
      if (isPanCrop) {
        openPanCrop(eventEl, "Pan/Crop");
        return;
      }

      const isEventFx =
        action === "video-event-fx" ||
        (typeof label === "string" && /^(Media FX|Event FX|Video Event FX)/i.test(label));
      if (isEventFx && eventEl && eventEl.classList.contains("event--video")) {
        openVideoEventFx(eventEl);
      }
    });

    document.addEventListener("keydown", (e) => {
      if (e.key !== "Escape") return;
      const chooser = document.getElementById(CHOOSER_ID);
      if (chooser && !chooser.hidden) {
        e.preventDefault();
        closePluginChooser();
        return;
      }
      const win = document.getElementById(WIN_ID);
      if (win && !win.hidden) {
        e.preventDefault();
        closePanCrop();
      }
    });
  });

  window.VegasPanCrop = {
    open: openPanCrop,
    close: closePanCrop,
    openChooser: openPluginChooser,
    closeChooser: closePluginChooser,
    openEventFx: openVideoEventFx,
    openTrackFx: openVideoTrackFx,
  };
})();
