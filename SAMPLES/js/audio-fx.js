/**
 * Audio Event FX + Plug-In Chooser (Vegas-style mockup).
 * Empty chain → chooser; existing chain → Audio Event FX (e.g. Auto-Key VST3).
 */
(function () {
  const CHOOSER_ID = "audio-plugin-chooser";
  const FX_WIN_ID = "audio-event-fx-window";

  function mediaNameFromEvent(eventEl) {
    if (!eventEl) return "sample_for_project_video";
    if (eventEl.classList?.contains("track-header")) {
      return (
        eventEl.dataset.trackName ||
        eventEl.querySelector(".track-header__name")?.textContent?.trim() ||
        "Audio"
      );
    }
    const label = eventEl.querySelector(".event__label")?.textContent?.trim() || "";
    return label.replace(/\.[^.]+$/, "") || label || "sample_for_project_video";
  }

  function getAudioChain(eventEl) {
    if (!eventEl) return [];
    const raw = (eventEl.dataset.fxChain || "").trim();
    if (!raw) return [];
    return raw
      .split("|")
      .map((s) => s.trim())
      .filter(Boolean);
  }

  function setAudioChain(eventEl, names) {
    if (!eventEl) return;
    eventEl.dataset.fxChain = (names || []).join("|");
    window.VegasTimelineChrome?.syncEventFxBadge?.(eventEl);
  }

  const AUDIO_TREE = [
    { id: "vst", label: "VST", root: true },
    { id: "audio", label: "Audio", pad: 1 },
    { id: "all", label: "All", pad: 2 },
    { id: "vegas", label: "VEGAS", pad: 2 },
    { id: "third", label: "Third Party", pad: 2 },
    { id: "auto", label: "Automatable", pad: 2 },
    { id: "track", label: "Track Optimized FX", pad: 2 },
    { id: "vst-leaf", label: "VST", pad: 2, active: true },
    { id: "51", label: "5.1 FX", pad: 2 },
    { id: "pack", label: "FX Packages", pad: 2 },
  ];

  const AUDIO_PLUGINS = [
    "Auto-Key",
    "Auto-Tune Access",
    "Auto-Tune Artist",
    "Auto-Tune EFX+",
    "Auto-Tune Pro",
    "eFX ChorusFlanger",
    "eFX Compressor",
    "eFX Gate",
    "eFX Reverb",
    "FabFilter Micro",
    "FabFilter One",
    "FabFilter Pro-C 2",
    "FabFilter Pro-G",
    "FabFilter Pro-L 2",
    "FabFilter Pro-MB",
    "FabFilter Pro-Q 3",
    "FabFilter Saturn 2",
    "FabFilter Timeless 3",
    "FabFilter Volcano 3",
    "GClip",
    "GGate",
    "GMulti",
    "GNormal",
    "GSnap",
    "Ozone 12 Dynamic EQ",
    "Ozone 12 Dynamics",
    "Ozone 12 Equalizer",
    "Ozone 12 Imager",
    "Ozone 12 Maximizer",
    "Ozone 12 Spectral Shaper",
    "Ozone 12 Vintage Compressor",
    "Ozone 12 Vintage EQ",
    "Ozone 12 Vintage Limiter",
    "Ozone 12 Vintage Tape",
    "TrackEQ",
    "TrackFX",
    "Wave Hammer",
  ];

  function positionWindow(win, w, h, offsetX, offsetY) {
    if (win.style.left) return;
    const width = Math.min(w || 560, window.innerWidth - 32);
    const height = Math.min(h || 480, window.innerHeight - 32);
    win.style.width = width + "px";
    win.style.height = height + "px";
    win.style.left =
      Math.max(24, Math.round((window.innerWidth - width) / 2) + (offsetX || 0)) + "px";
    win.style.top =
      Math.max(24, Math.round((window.innerHeight - height) / 2) + (offsetY || 0)) + "px";
    win.style.transform = "none";
  }

  function wireDrag(win, handleSel) {
    const bar = win.querySelector(handleSel);
    if (!bar) return;
    bar.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      if (e.target.closest("button")) return;
      e.preventDefault();
      const rect = win.getBoundingClientRect();
      const ox = e.clientX - rect.left;
      const oy = e.clientY - rect.top;
      win.classList.add("is-dragging");
      function onMove(ev) {
        win.style.left = Math.max(0, Math.min(window.innerWidth - 80, ev.clientX - ox)) + "px";
        win.style.top = Math.max(0, Math.min(window.innerHeight - 40, ev.clientY - oy)) + "px";
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

  function buildChooser() {
    let win = document.getElementById(CHOOSER_ID);
    if (win) return win;

    win = document.createElement("div");
    win.id = CHOOSER_ID;
    win.className = "aplc-window";
    win.setAttribute("role", "dialog");
    win.setAttribute("aria-label", "Plug-In Chooser");
    win.hidden = true;

    const folders = AUDIO_TREE.map((f) => {
      const cls =
        "aplc-tree__item" +
        (f.root ? " aplc-tree__item--root is-open" : "") +
        (f.active ? " is-selected" : "");
      return (
        '<button type="button" class="' +
        cls +
        '" data-folder="' +
        f.id +
        '" style="padding-left:' +
        (8 + (f.pad || 0) * 12) +
        'px">' +
        (f.root || f.pad === 1 ? '<span class="aplc-tree__twist">▾</span>' : "") +
        '<span class="aplc-tree__ico"></span>' +
        f.label +
        "</button>"
      );
    }).join("");

    const plugins = AUDIO_PLUGINS.map(
      (name, i) =>
        '<button type="button" class="aplc-plugin' +
        (name === "Auto-Key" ? " is-selected" : "") +
        '" data-plugin="' +
        name +
        '"><span class="aplc-plugin__fx">fx</span>' +
        name +
        "</button>"
    ).join("");

    win.innerHTML =
      '<div class="aplc-window__titlebar" data-aplc-drag>' +
      '<span class="aplc-window__title">Plug-In Chooser - <em data-aplc-media>sample_for_project_video</em></span>' +
      '<div class="pc-window__winbtns">' +
      '<button type="button" class="pc-winbtn">─</button>' +
      '<button type="button" class="pc-winbtn">☐</button>' +
      '<button type="button" class="pc-winbtn pc-winbtn--close" data-aplc-close>✕</button>' +
      "</div>" +
      "</div>" +
      '<div class="aplc-status">' +
      '<input type="text" readonly value="No plug-ins in chain" data-aplc-status />' +
      '<div class="aplc-status__icons" aria-hidden="true">' +
      "<span>+</span><span>−</span><span>↕</span>" +
      "</div>" +
      "</div>" +
      '<div class="aplc-body">' +
      '<div class="aplc-tree">' +
      folders +
      "</div>" +
      '<div class="aplc-plugins">' +
      plugins +
      "</div>" +
      '<div class="aplc-actions">' +
      '<button type="button" class="aplc-btn" data-aplc-ok>OK</button>' +
      '<button type="button" class="aplc-btn" data-aplc-close>Cancel</button>' +
      '<button type="button" class="aplc-btn" data-aplc-add>Add</button>' +
      '<button type="button" class="aplc-btn" disabled>Replace</button>' +
      '<button type="button" class="aplc-btn" disabled>Remove</button>' +
      '<button type="button" class="aplc-btn" disabled>Save As...</button>' +
      '<button type="button" class="aplc-btn" disabled>Save LUT...</button>' +
      "</div>" +
      "</div>";

    document.body.appendChild(win);
    wireChooser(win);
    return win;
  }

  function selectedPluginName(win) {
    return win.querySelector(".aplc-plugin.is-selected")?.dataset?.plugin || "";
  }

  function applySelectedPlugin(win) {
    const name = selectedPluginName(win);
    const eventEl = win._eventEl;
    if (!name || !eventEl) {
      closeAudioChooser();
      return;
    }
    const chainName =
      name === "Auto-Key" ? "Auto-Key(VST3, 64 Bit)" : name + "(VST3, 64 Bit)";
    setAudioChain(eventEl, [chainName]);
    closeAudioChooser();
    openAudioEventFx(eventEl);
  }

  function wireChooser(win) {
    win.querySelectorAll("[data-aplc-close]").forEach((b) =>
      b.addEventListener("click", () => closeAudioChooser())
    );
    win.querySelector("[data-aplc-ok]")?.addEventListener("click", () => applySelectedPlugin(win));
    win.querySelector("[data-aplc-add]")?.addEventListener("click", () => applySelectedPlugin(win));

    win.querySelectorAll(".aplc-plugin").forEach((btn) => {
      btn.addEventListener("click", () => {
        win.querySelectorAll(".aplc-plugin").forEach((b) => b.classList.remove("is-selected"));
        btn.classList.add("is-selected");
      });
      btn.addEventListener("dblclick", () => applySelectedPlugin(win));
    });

    win.querySelectorAll(".aplc-tree__item").forEach((btn) => {
      btn.addEventListener("click", () => {
        win.querySelectorAll(".aplc-tree__item").forEach((b) => b.classList.remove("is-selected"));
        btn.classList.add("is-selected");
      });
    });

    wireDrag(win, "[data-aplc-drag]");
  }

  function openAudioChooser(eventEl) {
    const win = buildChooser();
    win._eventEl = eventEl || null;
    const isTrack = !!eventEl?.classList?.contains("track-header");
    const title = win.querySelector(".aplc-window__title");
    if (title) {
      title.innerHTML = isTrack
        ? 'Plug-In Chooser - Audio Track FX — <em data-aplc-media></em>'
        : 'Plug-In Chooser - <em data-aplc-media></em>';
    }
    const media = win.querySelector("[data-aplc-media]");
    if (media) media.textContent = mediaNameFromEvent(eventEl);
    const status = win.querySelector("[data-aplc-status]");
    if (status) {
      const chain = getAudioChain(eventEl);
      status.value = chain.length ? chain.join(" → ") : "No plug-ins in chain";
    }

    win.hidden = false;
    win.classList.add("is-open");
    positionWindow(win, 720, 520);
  }

  function closeAudioChooser() {
    const win = document.getElementById(CHOOSER_ID);
    if (!win) return;
    win.classList.remove("is-open");
    win.hidden = true;
  }

  function pianoKeysHtml() {
    // White keys; C minor scale tones lit (approximate visual match to Auto-Key)
    const onIdx = new Set([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]);
    let whites = "";
    for (let i = 0; i < 15; i++) {
      whites +=
        '<span class="ak-key ak-key--white' + (onIdx.has(i) ? " is-on" : "") + '"></span>';
    }
    const blacks =
      '<span class="ak-blacks">' +
      '<i class="is-on" style="left:6%"></i>' +
      '<i class="is-on" style="left:12.5%"></i>' +
      '<i style="left:26%"></i>' +
      '<i class="is-on" style="left:32.5%"></i>' +
      '<i class="is-on" style="left:39%"></i>' +
      '<i class="is-on" style="left:52.5%"></i>' +
      '<i class="is-on" style="left:59%"></i>' +
      '<i style="left:72.5%"></i>' +
      '<i class="is-on" style="left:79%"></i>' +
      '<i class="is-on" style="left:85.5%"></i>' +
      "</span>";
    return whites + blacks;
  }

  function buildAudioFxWindow() {
    let win = document.getElementById(FX_WIN_ID);
    if (win) return win;

    win = document.createElement("div");
    win.id = FX_WIN_ID;
    win.className = "aefx-window";
    win.setAttribute("role", "dialog");
    win.setAttribute("aria-label", "Audio Event FX");
    win.hidden = true;

    win.innerHTML =
      '<div class="aefx-window__titlebar" data-aefx-drag>' +
      '<span class="aefx-window__title">Audio Event FX</span>' +
      '<div class="pc-window__winbtns">' +
      '<button type="button" class="pc-winbtn">─</button>' +
      '<button type="button" class="pc-winbtn">☐</button>' +
      '<button type="button" class="pc-winbtn pc-winbtn--close" data-aefx-close>✕</button>' +
      "</div>" +
      "</div>" +
      '<div class="aefx-subheader">' +
      '<span class="aefx-media"><span class="aefx-media__ico" aria-hidden="true">♪</span><em data-aefx-media>sample_for_project_video</em></span>' +
      '<div class="aefx-subtools">' +
      '<button type="button" class="pc-ico-btn" data-aefx-add title="Add Plug-In">fx+</button>' +
      '<button type="button" class="pc-ico-btn" data-aefx-remove title="Remove Plug-In">fx×</button>' +
      "</div>" +
      "</div>" +
      '<div class="aefx-chain" data-aefx-chain></div>' +
      '<div class="aefx-preset">' +
      "<label>Preset:</label>" +
      '<select aria-label="Preset"><option>(Untitled)</option></select>' +
      '<button type="button" class="pc-ico-btn" title="Open">O</button>' +
      '<button type="button" class="pc-ico-btn" title="Save">S</button>' +
      '<button type="button" class="pc-ico-btn" title="Delete">✕</button>' +
      "</div>" +
      '<div class="aefx-host" data-aefx-host>' +
      '<div class="ak-ui">' +
      '<div class="ak-ui__top">' +
      '<span class="ak-brand">ANTARES</span>' +
      '<span class="ak-tuning">A=440</span>' +
      "</div>" +
      '<div class="ak-ui__title">AUTO-KEY</div>' +
      '<div class="ak-ui__key">C Minor</div>' +
      '<div class="ak-ui__actions">' +
      '<button type="button" class="ak-btn">File...</button>' +
      '<button type="button" class="ak-btn ak-btn--primary">Send to Auto-Tune™</button>' +
      "</div>" +
      '<div class="ak-piano" aria-hidden="true">' +
      pianoKeysHtml() +
      "</div>" +
      '<div class="ak-ui__foot">' +
      "<span>v1.0.1</span>" +
      "<span>powered by ZPlane™</span>" +
      "</div>" +
      "</div>" +
      "</div>";

    document.body.appendChild(win);
    wireAudioFx(win);
    return win;
  }

  function renderChain(win, chain) {
    const el = win.querySelector("[data-aefx-chain]");
    if (!el) return;
    if (!chain.length) {
      el.innerHTML = '<span class="aefx-chain__empty">No plug-ins in chain</span>';
      return;
    }
    el.innerHTML = chain
      .map(
        (name, i) =>
          '<label class="aefx-chain__item' +
          (i === 0 ? " is-active" : "") +
          '">' +
          '<input type="checkbox" checked />' +
          "<span>" +
          name +
          "</span>" +
          "</label>"
      )
      .join("");
  }

  function wireAudioFx(win) {
    win.querySelectorAll("[data-aefx-close]").forEach((b) =>
      b.addEventListener("click", () => closeAudioEventFx())
    );
    win.querySelector("[data-aefx-add]")?.addEventListener("click", () => {
      openAudioChooser(win._eventEl);
    });
    win.querySelector("[data-aefx-remove]")?.addEventListener("click", () => {
      const eventEl = win._eventEl;
      if (!eventEl) return;
      setAudioChain(eventEl, []);
      closeAudioEventFx();
      openAudioChooser(eventEl);
    });
    wireDrag(win, "[data-aefx-drag]");
    wireFxResize(win, { minW: 420, minH: 320 });
  }

  function openAudioEventFx(eventEl) {
    const win = buildAudioFxWindow();
    win._eventEl = eventEl || null;
    const isTrack = !!eventEl?.classList?.contains("track-header");
    const title = win.querySelector(".aefx-window__title");
    if (title) title.textContent = isTrack ? "Audio Track FX" : "Audio Event FX";
    win.setAttribute("aria-label", isTrack ? "Audio Track FX" : "Audio Event FX");
    const media = win.querySelector("[data-aefx-media]");
    if (media) media.textContent = mediaNameFromEvent(eventEl);
    renderChain(win, getAudioChain(eventEl));

    win.hidden = false;
    win.classList.add("is-open");
    positionWindow(win, 560, 480, -20, -10);
  }

  function closeAudioEventFx() {
    const win = document.getElementById(FX_WIN_ID);
    if (!win) return;
    win.classList.remove("is-open");
    win.hidden = true;
  }

  function openAudioFx(eventEl) {
    const chain = getAudioChain(eventEl);
    if (chain.length) openAudioEventFx(eventEl);
    else openAudioChooser(eventEl);
  }

  /** Audio Track FX — chooser / chain window for an audio track header. */
  function openAudioTrackFx(header) {
    if (!header) return;
    openAudioFx(header);
  }

  document.addEventListener("DOMContentLoaded", () => {
    document.addEventListener(
      "click",
      (e) => {
        const fxBtn = e.target.closest(".event-btn--fx");
        if (!fxBtn) return;
        const audioEvent = fxBtn.closest(".event--audio");
        if (!audioEvent) return;
        e.preventDefault();
        e.stopPropagation();
        openAudioFx(audioEvent);
      },
      true
    );

    document.addEventListener("vegas:context-action", (e) => {
      const { type, action, label, target } = e.detail || {};
      const isAudioFx =
        action === "audio-event-fx" ||
        (typeof label === "string" && /Audio FX/i.test(label));
      if (!isAudioFx) return;
      const eventEl =
        (target && target.closest?.(".event--audio")) ||
        (type === "audio-event" ? target : null) ||
        document.querySelector(".event.event--audio.is-selected") ||
        document.querySelector(".event.event--audio");
      if (eventEl) openAudioFx(eventEl);
    });

    document.addEventListener("keydown", (e) => {
      if (e.key !== "Escape") return;
      const chooser = document.getElementById(CHOOSER_ID);
      if (chooser && !chooser.hidden) {
        e.preventDefault();
        closeAudioChooser();
        return;
      }
      const fx = document.getElementById(FX_WIN_ID);
      if (fx && !fx.hidden) {
        e.preventDefault();
        closeAudioEventFx();
      }
    });
  });

  window.VegasAudioFx = {
    open: openAudioFx,
    openChooser: openAudioChooser,
    openEventFx: openAudioEventFx,
    openTrackFx: openAudioTrackFx,
    closeChooser: closeAudioChooser,
    closeEventFx: closeAudioEventFx,
  };
})();
