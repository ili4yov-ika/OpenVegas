/**
 * Vegas Pro–style Trimmer window (video + audio).
 * Open: event context “Open in Trimmer”, media-card dblclick, VegasTrimmer.open().
 */
(function () {
  const WIN_ID = "trimmer-window";
  const MENU_ID = "trimmer-more-menu";

  const state = {
    kind: "video", // video | audio
    src: "",
    path: "",
    name: "",
    duration: 0,
    current: 0,
    inPoint: 0,
    outPoint: null, // null = end
    loop: false,
    overwrite: true,
    playing: false,
    markers: [],
    regions: [],
    raf: 0,
    eventEl: null,
  };

  function assetBase() {
    const scripts = document.querySelectorAll("script[src]");
    for (let i = scripts.length - 1; i >= 0; i--) {
      const src = scripts[i].getAttribute("src") || "";
      if (/trimmer\.js|timeline-event-actions\.js|chrome\.js/i.test(src)) {
        return src.replace(/[^/]+$/, "").replace(/js\/?$/, "") + "assets/";
      }
    }
    return "../assets/";
  }

  function resolveMedia(kind, nameHint) {
    const base = assetBase();
    const n = (nameHint || "").toLowerCase();
    if (kind === "audio" || /\.(wav|mp3|flac|ogg|aif)/i.test(n) || /audio/i.test(n)) {
      return {
        kind: "audio",
        src: base + "sample_for_project_audio.wav",
        name: /\.wav$/i.test(nameHint) ? nameHint : "sample_for_project_audio.wav",
        path: "D:\\Devs\\C++\\OpenVegas\\SAMPLES\\assets\\",
      };
    }
    return {
      kind: "video",
      src: base + "sample_for_project_video.mp4",
      name: /\.mp4$/i.test(nameHint) ? nameHint : "sample_for_project_video.mp4",
      path: "D:\\Devs\\C++\\OpenVegas\\SAMPLES\\assets\\",
    };
  }

  function formatTC(sec) {
    const s = Math.max(0, Number(sec) || 0);
    // Vegas-like measures.beats.ticks mock at ~120bpm / 4/4 → 0.5s per beat
    const totalBeats = s / 0.5;
    const measures = Math.floor(totalBeats / 4) + 1;
    const beats = (Math.floor(totalBeats) % 4) + 1;
    const ticks = Math.floor((totalBeats % 1) * 1000);
    return measures + "." + beats + "." + String(ticks).padStart(3, "0");
  }

  function formatClock(sec) {
    const s = Math.max(0, Number(sec) || 0);
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const ss = Math.floor(s % 60);
    const ms = Math.floor((s % 1) * 1000);
    return (
      String(h).padStart(2, "0") +
      ":" +
      String(m).padStart(2, "0") +
      ":" +
      String(ss).padStart(2, "0") +
      "," +
      String(ms).padStart(3, "0")
    );
  }

  function toast(msg) {
    if (window.VegasEventActions?.toast) window.VegasEventActions.toast(msg);
    else console.log("[Trimmer]", msg);
  }

  function svgIco(paths) {
    return (
      '<svg viewBox="0 0 16 16" width="14" height="14" aria-hidden="true">' + paths + "</svg>"
    );
  }

  const ICO = {
    loop: svgIco(
      '<path d="M4 4h6a4 4 0 010 8H7" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M4 4l2-2M4 4l2 2" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M12 12H6a4 4 0 010-8h3" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M12 12l-2 2M12 12l-2-2" fill="none" stroke="currentColor" stroke-width="1.4"/>'
    ),
    play: svgIco('<path d="M4 2.5v11l10-5.5z" fill="currentColor"/>'),
    pause: svgIco('<path d="M3.5 2.5h3.2v11H3.5zm5.8 0H12.5v11H9.3z" fill="currentColor"/>'),
    stop: svgIco('<rect x="3.5" y="3.5" width="9" height="9" fill="currentColor"/>'),
    toStart: svgIco(
      '<path d="M3 2.5v11M5.5 8l7.5-5v10z" fill="currentColor"/><path d="M3 2.5v11" stroke="currentColor" stroke-width="1.2"/>'
    ),
    add: svgIco(
      '<rect x="2.5" y="3" width="8" height="10" rx="0.8" fill="none" stroke="#6eb0ff" stroke-width="1.2"/><path d="M11 6.5l3 1.5-3 1.5V6.5z" fill="#6eb0ff"/>'
    ),
    more: '<span class="tr-more-dots">⋯</span>',
    pin: svgIco('<path d="M8 1.5l2 4 4 .4-3 3 .9 4.1L8 11.2 4.1 13l.9-4.1-3-3 4-.4z" fill="currentColor"/>'),
    sel: svgIco(
      '<rect x="3" y="3" width="10" height="10" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M3 8h3M8 3v3" stroke="#e8c84a" stroke-width="1.4"/>'
    ),
    list: svgIco(
      '<path d="M3 4h10M3 8h10M3 12h7" stroke="currentColor" stroke-width="1.3"/><path d="M12 10.5l1.5 3L15.5 9" fill="none" stroke="currentColor" stroke-width="1.2"/>'
    ),
    hist: svgIco(
      '<circle cx="8" cy="8" r="5.2" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M8 5v3.2l2 1.3" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M3.5 4.2l-1.2 2.2 2.4.2" fill="none" stroke="currentColor" stroke-width="1.1"/>'
    ),
    clear: svgIco('<path d="M4 4l8 8M12 4l-8 8" stroke="#e05555" stroke-width="1.8" stroke-linecap="round"/>'),
    props: svgIco(
      '<rect x="2.5" y="3.5" width="11" height="9" rx="0.8" fill="none" stroke="currentColor" stroke-width="1.2"/><path d="M5 7h6M5 9.5h4" stroke="currentColor" stroke-width="1.1"/>'
    ),
  };

  function mediaEl() {
    return document.getElementById("tr-media");
  }

  function winEl() {
    return document.getElementById(WIN_ID);
  }

  function buildWindow() {
    let win = winEl();
    if (win) return win;

    win = document.createElement("div");
    win.id = WIN_ID;
    win.className = "tr-window";
    win.setAttribute("role", "dialog");
    win.setAttribute("aria-label", "Trimmer");
    win.hidden = true;

    win.innerHTML =
      '<div class="tr-window__titlebar" data-tr-drag>' +
      '<span class="tr-window__title" data-tr-title>Trimmer</span>' +
      '<div class="tr-window__winbtns">' +
      '<button type="button" class="tr-winbtn" title="Minimize" aria-label="Minimize">─</button>' +
      '<button type="button" class="tr-winbtn" title="Maximize" aria-label="Maximize" disabled>☐</button>' +
      '<button type="button" class="tr-winbtn tr-winbtn--close" data-tr-close title="Close" aria-label="Close">✕</button>' +
      "</div>" +
      "</div>" +
      '<div class="tr-window__filebar">' +
      '<button type="button" class="tr-file" data-tr-file title="Media">' +
      '<span class="tr-file__name" data-tr-filename>sample_for_project_video.mp4</span>' +
      '<span class="tr-file__path" data-tr-filepath></span>' +
      '<span class="tr-file__caret">▾</span>' +
      "</button>" +
      '<div class="tr-filebar__tools">' +
      '<button type="button" class="tr-ico" title="History">' +
      ICO.list +
      "</button>" +
      '<button type="button" class="tr-ico" title="Recent">' +
      ICO.hist +
      "</button>" +
      '<button type="button" class="tr-ico" data-tr-clear title="Clear">' +
      ICO.clear +
      "</button>" +
      '<button type="button" class="tr-ico" title="Properties">' +
      ICO.props +
      "</button>" +
      "</div>" +
      "</div>" +
      '<div class="tr-stage" data-tr-stage>' +
      '<div class="tr-preview tr-preview--video" data-tr-preview="video">' +
      '<video id="tr-media-video" class="tr-media" playsinline preload="metadata"></video>' +
      '<div class="tr-playhead-line" data-tr-vline aria-hidden="true"></div>' +
      '<div class="tr-inout tr-inout--video" data-tr-inout-video>' +
      '<span class="tr-inout__sel" data-tr-insel-v></span>' +
      "</div>" +
      "</div>" +
      '<div class="tr-preview tr-preview--audio is-hidden" data-tr-preview="audio">' +
      '<div class="tr-audio-ruler" data-tr-aruler></div>' +
      '<div class="tr-wave-host">' +
      '<canvas class="tr-wave" data-tr-wave width="800" height="160"></canvas>' +
      '<div class="tr-playhead-line" data-tr-aline aria-hidden="true"></div>' +
      '<div class="tr-inout" data-tr-inout>' +
      '<span class="tr-inout__in" data-tr-inhandle title="In"></span>' +
      '<span class="tr-inout__sel" data-tr-insel></span>' +
      '<span class="tr-inout__out" data-tr-outhandle title="Out"></span>' +
      "</div>" +
      '<div class="tr-marks" data-tr-marks></div>' +
      "</div>" +
      '<audio id="tr-media-audio" preload="metadata"></audio>' +
      "</div>" +
      "</div>" +
      '<div class="tr-scrub">' +
      '<input type="range" class="tr-scrub__range" data-tr-scrub min="0" max="1000" value="0" aria-label="Scrub" />' +
      "</div>" +
      '<div class="tr-transport">' +
      '<div class="tr-transport__btns">' +
      '<button type="button" class="tr-tbtn" data-tr-act="loop" title="Loop Playback">' +
      ICO.loop +
      "</button>" +
      '<button type="button" class="tr-tbtn" data-tr-act="play" title="Play">' +
      ICO.play +
      "</button>" +
      '<button type="button" class="tr-tbtn" data-tr-act="pause" title="Pause">' +
      ICO.pause +
      "</button>" +
      '<button type="button" class="tr-tbtn" data-tr-act="stop" title="Stop">' +
      ICO.stop +
      "</button>" +
      '<button type="button" class="tr-tbtn" data-tr-act="to-start" title="Go to Start">' +
      ICO.toStart +
      "</button>" +
      '<button type="button" class="tr-tbtn tr-tbtn--accent" data-tr-act="add-timeline" title="Add to Timeline">' +
      ICO.add +
      "</button>" +
      '<button type="button" class="tr-tbtn is-disabled" disabled title="(unavailable)">▭</button>' +
      '<button type="button" class="tr-tbtn tr-tbtn--more" data-tr-act="more" title="More">' +
      ICO.more +
      "</button>" +
      "</div>" +
      '<div class="tr-transport__right">' +
      '<button type="button" class="tr-ico tr-ico--tiny" title="Pin">' +
      ICO.pin +
      "</button>" +
      '<span class="tr-tc" data-tr-tc>1.1.000</span>' +
      '<button type="button" class="tr-ico tr-ico--tiny" title="Selection">' +
      ICO.sel +
      "</button>" +
      "</div>" +
      "</div>" +
      '<div class="fx-resize" title="Resize" aria-hidden="true"></div>';

    document.body.appendChild(win);
    wireWindow(win);
    return win;
  }

  function activeMedia() {
    if (state.kind === "audio") return document.getElementById("tr-media-audio");
    return document.getElementById("tr-media-video");
  }

  function setMode(kind) {
    state.kind = kind;
    const win = winEl();
    if (!win) return;
    win.classList.toggle("tr-window--audio", kind === "audio");
    win.classList.toggle("tr-window--video", kind === "video");
    win.querySelector('[data-tr-preview="video"]')?.classList.toggle("is-hidden", kind !== "video");
    win.querySelector('[data-tr-preview="audio"]')?.classList.toggle("is-hidden", kind !== "audio");
  }

  function updateTitle() {
    const win = winEl();
    if (!win) return;
    const full = state.path + state.name;
    win.querySelector("[data-tr-title]").textContent = "Trimmer - " + full;
    win.querySelector("[data-tr-filename]").textContent = state.name;
    win.querySelector("[data-tr-filepath]").textContent = " [" + state.path + "]";
  }

  function updateTC() {
    const win = winEl();
    if (!win) return;
    win.querySelector("[data-tr-tc]").textContent = formatTC(state.current);
    const scrub = win.querySelector("[data-tr-scrub]");
    if (scrub && state.duration > 0 && document.activeElement !== scrub) {
      scrub.value = String(Math.round((state.current / state.duration) * 1000));
    }
    const pct = state.duration > 0 ? (state.current / state.duration) * 100 : 0;
    win.querySelectorAll(".tr-playhead-line").forEach((line) => {
      line.style.left = pct + "%";
    });
    syncInOutUI();
  }

  function syncInOutUI() {
    const win = winEl();
    if (!win || state.duration <= 0) return;
    const out = state.outPoint == null ? state.duration : state.outPoint;
    const inPct = (state.inPoint / state.duration) * 100;
    const outPct = (out / state.duration) * 100;

    win.querySelectorAll("[data-tr-inout], [data-tr-inout-video]").forEach((host) => {
      const sel = host.querySelector("[data-tr-insel], [data-tr-insel-v]");
      if (sel) {
        sel.style.left = inPct + "%";
        sel.style.width = Math.max(0, outPct - inPct) + "%";
      }
      const ih = host.querySelector("[data-tr-inhandle]");
      const oh = host.querySelector("[data-tr-outhandle]");
      if (ih) ih.style.left = inPct + "%";
      if (oh) oh.style.left = outPct + "%";
    });
  }

  function seek(t) {
    const media = activeMedia();
    const out = state.outPoint == null ? state.duration : state.outPoint;
    state.current = Math.max(0, Math.min(state.duration || 0, t));
    if (media && Number.isFinite(media.duration)) {
      try {
        media.currentTime = state.current;
      } catch (_) {}
    }
    if (state.playing && state.current >= out - 0.02) {
      if (state.loop) {
        state.current = state.inPoint;
        if (media) media.currentTime = state.inPoint;
      } else {
        pause();
      }
    }
    updateTC();
  }

  function tick() {
    const media = activeMedia();
    if (media && !media.paused) {
      state.current = media.currentTime || 0;
      const out = state.outPoint == null ? state.duration : state.outPoint;
      if (state.current >= out - 0.02) {
        if (state.loop) {
          media.currentTime = state.inPoint;
          state.current = state.inPoint;
        } else {
          pause();
          seek(out);
          return;
        }
      }
      updateTC();
      state.raf = requestAnimationFrame(tick);
    }
  }

  function play() {
    const media = activeMedia();
    if (!media) return;
    if (state.current < state.inPoint || (state.outPoint != null && state.current >= state.outPoint)) {
      seek(state.inPoint);
    }
    media.play().catch(() => {});
    state.playing = true;
    winEl()?.querySelector('[data-tr-act="play"]')?.classList.add("is-active");
    cancelAnimationFrame(state.raf);
    state.raf = requestAnimationFrame(tick);
  }

  function pause() {
    const media = activeMedia();
    if (media) media.pause();
    state.playing = false;
    winEl()?.querySelector('[data-tr-act="play"]')?.classList.remove("is-active");
    cancelAnimationFrame(state.raf);
  }

  function stop() {
    pause();
    seek(state.inPoint || 0);
  }

  function playFromStart() {
    seek(0);
    play();
  }

  function stepFrame(dir) {
    const fps = 30;
    seek(state.current + dir / fps);
  }

  function setIn() {
    state.inPoint = state.current;
    if (state.outPoint != null && state.outPoint <= state.inPoint) {
      state.outPoint = Math.min(state.duration, state.inPoint + 0.1);
    }
    syncInOutUI();
    toast("In point: " + formatClock(state.inPoint));
  }

  function setOut() {
    state.outPoint = state.current;
    if (state.outPoint <= state.inPoint) {
      state.inPoint = Math.max(0, state.outPoint - 0.1);
    }
    syncInOutUI();
    toast("Out point: " + formatClock(state.outPoint));
  }

  function insertMarker() {
    state.markers.push({ t: state.current, label: "M" + (state.markers.length + 1) });
    renderMarks();
    toast("Marker at " + formatClock(state.current));
  }

  function insertRegion() {
    const a = state.inPoint;
    const b = state.outPoint == null ? state.current : state.outPoint;
    state.regions.push({ a: Math.min(a, b), b: Math.max(a, b) });
    renderMarks();
    toast("Region inserted");
  }

  function renderMarks() {
    const host = winEl()?.querySelector("[data-tr-marks]");
    if (!host || state.duration <= 0) return;
    host.innerHTML = "";
    state.markers.forEach((m) => {
      const el = document.createElement("span");
      el.className = "tr-mark";
      el.style.left = (m.t / state.duration) * 100 + "%";
      el.title = m.label;
      host.appendChild(el);
    });
    state.regions.forEach((r) => {
      const el = document.createElement("span");
      el.className = "tr-region";
      el.style.left = (r.a / state.duration) * 100 + "%";
      el.style.width = ((r.b - r.a) / state.duration) * 100 + "%";
      host.appendChild(el);
    });
  }

  function drawAudioRuler() {
    const el = winEl()?.querySelector("[data-tr-aruler]");
    if (!el || state.duration <= 0) return;
    const n = Math.max(4, Math.ceil(state.duration * 2));
    let html = "";
    for (let i = 0; i <= n; i++) {
      const t = (i / n) * state.duration;
      html +=
        '<span class="tr-audio-ruler__tick" style="left:' +
        (i / n) * 100 +
        '%"><i>' +
        formatTC(t) +
        "</i></span>";
    }
    el.innerHTML = html;
  }

  function drawWaveform(buffer) {
    const canvas = winEl()?.querySelector("[data-tr-wave]");
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const w = (canvas.width = canvas.clientWidth * 2 || 800);
    const h = (canvas.height = canvas.clientHeight * 2 || 160);
    ctx.fillStyle = "#2c2c2c";
    ctx.fillRect(0, 0, w, h);

    if (!buffer) {
      // Placeholder stereo waveform
      ctx.strokeStyle = "#b8a0b4";
      for (let ch = 0; ch < 2; ch++) {
        const mid = h * (0.25 + ch * 0.5);
        ctx.beginPath();
        for (let x = 0; x < w; x++) {
          const t = x / w;
          const amp =
            (Math.sin(t * 40 + ch) * 0.35 + Math.sin(t * 17) * 0.25 + Math.sin(t * 90) * 0.1) *
            (h * 0.18);
          const y = mid + amp * Math.sin(t * Math.PI);
          if (x === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.stroke();
      }
      return;
    }

    const channels = Math.min(2, buffer.numberOfChannels);
    for (let ch = 0; ch < channels; ch++) {
      const data = buffer.getChannelData(ch);
      const mid = h * (channels === 1 ? 0.5 : 0.25 + ch * 0.5);
      const ampH = h * (channels === 1 ? 0.42 : 0.2);
      ctx.fillStyle = "#b8a0b4";
      const step = Math.max(1, Math.floor(data.length / w));
      for (let x = 0; x < w; x++) {
        let min = 1;
        let max = -1;
        const start = x * step;
        for (let i = 0; i < step; i++) {
          const v = data[start + i] || 0;
          if (v < min) min = v;
          if (v > max) max = v;
        }
        const y1 = mid + min * ampH;
        const y2 = mid + max * ampH;
        ctx.fillRect(x, y1, 1, Math.max(1, y2 - y1));
      }
    }
  }

  function loadAudioWaveform(src) {
    drawWaveform(null);
    if (!window.AudioContext && !window.webkitAudioContext) return;
    fetch(src)
      .then((r) => r.arrayBuffer())
      .then((buf) => {
        const AC = window.AudioContext || window.webkitAudioContext;
        const ac = new AC();
        return ac.decodeAudioData(buf).then((decoded) => {
          drawWaveform(decoded);
          ac.close?.();
        });
      })
      .catch(() => drawWaveform(null));
  }

  function addSelectionToTimeline() {
    const area = document.querySelector(".tracks-area");
    const panel = document.querySelector(".timeline-panel");
    if (!area || !panel) {
      toast("No timeline");
      return;
    }
    const kind = state.kind;
    const lane =
      area.querySelector(".track-lane--" + kind) ||
      area.querySelector(".event.is-selected")?.closest(".track-lane");
    if (!lane) {
      toast("No " + kind + " track");
      return;
    }
    const pps = parseFloat(panel.dataset.pxPerSec || "40") || 40;
    const out = state.outPoint == null ? state.duration : state.outPoint;
    const dur = Math.max(0.2, out - state.inPoint);
    const width = Math.max(40, Math.round(dur * pps));
    const ph = panel.querySelector(".tracks-inner > .playhead");
    const left = Math.round(parseFloat(ph?.style.left) || 0);

    const ev = document.createElement("div");
    ev.className = "event event--" + kind + " is-selected has-trimmers";
    ev.dataset.context = kind + "-event";
    ev.dataset.fadeIn = "0";
    ev.dataset.fadeOut = "0";
    ev.style.left = left + "px";
    ev.style.width = width + "px";
    const label = state.name.replace(/\.[^.]+$/, "");
    if (kind === "video") {
      ev.innerHTML =
        '<span class="event__label">' +
        label +
        "</span>" +
        '<div class="event__thumbs"><span style="background-image:url(\'' +
        assetBase() +
        "video-thumb.jpg')\"></span></div>";
    } else {
      ev.innerHTML = '<span class="event__label">' + label + '</span><div class="event__wave"></div>';
    }
    area.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
    lane.appendChild(ev);
    // Re-run chrome upgrade for the new event
    delete ev.dataset.tlUpgraded;
    window.VegasTimelineChrome?.syncCrossfadeZones?.(area);
    // Force upgrade via chrome if available
    document.dispatchEvent(new CustomEvent("vegas:events-changed"));
    toast("Added to timeline (" + formatClock(dur) + ")");
  }

  function createSubclipFromTrimmer() {
    const grid = document.querySelector(".media-grid");
    document.querySelector('.panel-tab[data-tab="project-media"]')?.click();
    const name =
      state.name.replace(/\.[^.]+$/, "") +
      " [" +
      formatClock(state.inPoint).slice(0, 8) +
      "-" +
      formatClock(state.outPoint == null ? state.duration : state.outPoint).slice(0, 8) +
      "]";
    if (grid) {
      const card = document.createElement("div");
      card.className = "media-card is-selected";
      card.dataset.trimmerSrc = state.src;
      card.dataset.trimmerKind = state.kind;
      card.innerHTML =
        '<div class="media-card__thumb' +
        (state.kind === "audio" ? ' media-card__thumb--audio">♪ SUB' : '"><img src="' + assetBase() + 'video-thumb.jpg" alt="" />') +
        "</div>" +
        '<div class="media-card__name">' +
        name +
        "</div>";
      grid.querySelectorAll(".media-card.is-selected").forEach((c) => c.classList.remove("is-selected"));
      grid.appendChild(card);
    }
    toast("Created Subclip");
  }

  function detectScenesToTimeline() {
    const media = activeMedia();
    const dur = state.duration || media?.duration || 10;
    const parts = Math.min(5, Math.max(2, Math.floor(dur / 3)));
    const slice = dur / parts;
    for (let i = 0; i < parts; i++) {
      state.inPoint = i * slice;
      state.outPoint = (i + 1) * slice;
      addSelectionToTimeline();
    }
    state.inPoint = 0;
    state.outPoint = null;
    syncInOutUI();
    toast("Detect Scenes: " + parts + " clips");
  }

  function beatDetection() {
    if (state.duration <= 0) return;
    const n = Math.min(16, Math.max(4, Math.floor(state.duration * 2)));
    state.markers = [];
    for (let i = 1; i < n; i++) {
      state.markers.push({ t: (state.duration * i) / n, label: "B" + i });
    }
    renderMarks();
    toast("Beat Detection: " + (n - 1) + " beats");
  }

  function moreMenuItems() {
    return [
      { label: "Play From Start", shortcut: "Shift+Space", action: "tr-play-start" },
      { label: "Go to Start", shortcut: "Ctrl+Home", action: "tr-go-start" },
      { label: "Go to End", shortcut: "Ctrl+End", action: "tr-go-end" },
      { label: "Previous Frame", shortcut: "Alt+Left", action: "tr-prev-frame" },
      { label: "Next Frame", shortcut: "Alt+Right", action: "tr-next-frame" },
      { sep: true },
      {
        label: "Enable Timeline Overwrite",
        check: true,
        checked: state.overwrite,
        action: "tr-overwrite",
      },
      { label: "Add to Timeline up to Cursor", shortcut: "Shift+A", disabled: true },
      { label: "Create Subclip...", action: "tr-subclip" },
      { sep: true },
      { label: "Set In Point", shortcut: "I", action: "tr-set-in" },
      { label: "Set Out Point", shortcut: "O", action: "tr-set-out" },
      { label: "Insert Marker", shortcut: "M", action: "tr-marker" },
      { label: "Insert Region", shortcut: "R", action: "tr-region" },
      { label: "Save Markers/Regions", shortcut: "S", disabled: true },
      { sep: true },
      { label: "Detect Scenes and Add to Timeline from Cursor", action: "tr-detect-scenes" },
      { label: "Beat Detection", shortcut: "B", action: "tr-beat" },
      { sep: true },
      { label: "Edit Visible Button Set...", action: "tr-edit-buttons" },
    ];
  }

  function closeMoreMenu() {
    document.getElementById(MENU_ID)?.remove();
    winEl()?.querySelector('[data-tr-act="more"]')?.classList.remove("is-active");
  }

  function openMoreMenu(anchor) {
    closeMoreMenu();
    const menu = document.createElement("div");
    menu.id = MENU_ID;
    menu.className = "tr-more-menu";
    menu.innerHTML = moreMenuItems()
      .map((it) => {
        if (it.sep) return '<div class="tr-more-menu__sep"></div>';
        const cls = ["tr-more-menu__item"];
        if (it.disabled) cls.push("is-disabled");
        if (it.checked) cls.push("is-checked");
        return (
          '<div class="' +
          cls.join(" ") +
          '"' +
          (it.disabled ? ' aria-disabled="true"' : "") +
          (it.action ? ' data-action="' + it.action + '"' : "") +
          ">" +
          '<span class="tr-more-menu__check">' +
          (it.check ? (it.checked ? "✓" : "") : "") +
          "</span>" +
          '<span class="tr-more-menu__label">' +
          it.label +
          "</span>" +
          '<span class="tr-more-menu__sc">' +
          (it.shortcut || "") +
          "</span>" +
          "</div>"
        );
      })
      .join("");

    document.body.appendChild(menu);
    anchor.classList.add("is-active");
    const r = anchor.getBoundingClientRect();
    menu.style.left = Math.min(r.left, window.innerWidth - menu.offsetWidth - 8) + "px";
    menu.style.top = Math.min(r.bottom + 2, window.innerHeight - menu.offsetHeight - 8) + "px";

    menu.addEventListener("click", (e) => {
      const item = e.target.closest(".tr-more-menu__item");
      if (!item || item.classList.contains("is-disabled")) return;
      const action = item.getAttribute("data-action") || "";
      closeMoreMenu();
      runMenuAction(action);
    });
  }

  function runMenuAction(action) {
    switch (action) {
      case "tr-play-start":
        playFromStart();
        break;
      case "tr-go-start":
        seek(0);
        break;
      case "tr-go-end":
        seek(state.duration);
        break;
      case "tr-prev-frame":
        stepFrame(-1);
        break;
      case "tr-next-frame":
        stepFrame(1);
        break;
      case "tr-overwrite":
        state.overwrite = !state.overwrite;
        toast(state.overwrite ? "Timeline Overwrite on" : "Timeline Overwrite off");
        break;
      case "tr-subclip":
        createSubclipFromTrimmer();
        break;
      case "tr-set-in":
        setIn();
        break;
      case "tr-set-out":
        setOut();
        break;
      case "tr-marker":
        insertMarker();
        break;
      case "tr-region":
        insertRegion();
        break;
      case "tr-detect-scenes":
        detectScenesToTimeline();
        break;
      case "tr-beat":
        beatDetection();
        break;
      case "tr-edit-buttons":
        toast("Edit Visible Button Set…");
        break;
      default:
        break;
    }
  }

  function onMediaMeta(media) {
    state.duration = media.duration || 0;
    if (state.outPoint == null || state.outPoint > state.duration) state.outPoint = null;
    updateTC();
    drawAudioRuler();
    renderMarks();
    if (state.kind === "audio") loadAudioWaveform(state.src);
  }

  function loadMedia(info) {
    pause();
    state.src = info.src;
    state.name = info.name;
    state.path = info.path;
    state.inPoint = 0;
    state.outPoint = null;
    state.markers = [];
    state.regions = [];
    state.current = 0;
    setMode(info.kind);
    updateTitle();

    const video = document.getElementById("tr-media-video");
    const audio = document.getElementById("tr-media-audio");
    if (info.kind === "video") {
      if (audio) {
        audio.pause();
        audio.removeAttribute("src");
        audio.load();
      }
      if (video) {
        video.src = info.src;
        video.onloadedmetadata = () => onMediaMeta(video);
        video.onerror = () => {
          toast("Cannot load video — showing placeholder");
          state.duration = 30;
          updateTC();
        };
        video.load();
      }
    } else {
      if (video) {
        video.pause();
        video.removeAttribute("src");
        video.load();
      }
      if (audio) {
        audio.src = info.src;
        audio.onloadedmetadata = () => onMediaMeta(audio);
        audio.onerror = () => {
          state.duration = 12;
          updateTC();
          drawAudioRuler();
          drawWaveform(null);
        };
        audio.load();
      }
      drawWaveform(null);
    }
  }

  function wireWindow(win) {
    win.querySelectorAll("[data-tr-close]").forEach((btn) => {
      btn.addEventListener("click", () => close());
    });

    win.querySelector("[data-tr-clear]")?.addEventListener("click", () => {
      pause();
      const video = document.getElementById("tr-media-video");
      const audio = document.getElementById("tr-media-audio");
      if (video) {
        video.removeAttribute("src");
        video.load();
      }
      if (audio) {
        audio.removeAttribute("src");
        audio.load();
      }
      state.duration = 0;
      state.current = 0;
      state.name = "(empty)";
      updateTitle();
      updateTC();
      toast("Trimmer cleared");
    });

    const bar = win.querySelector("[data-tr-drag]");
    bar?.addEventListener("pointerdown", (e) => {
      if (e.button !== 0 || e.target.closest("button")) return;
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

    // Resize
    const handle = win.querySelector(".fx-resize");
    handle?.addEventListener("pointerdown", (e) => {
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
      function onMove(ev) {
        const w = Math.max(480, Math.min(window.innerWidth - left - 8, startW + (ev.clientX - sx)));
        const h = Math.max(320, Math.min(window.innerHeight - top - 8, startH + (ev.clientY - sy)));
        win.style.width = w + "px";
        win.style.height = h + "px";
        if (state.kind === "audio") {
          drawWaveform(null);
          loadAudioWaveform(state.src);
        }
      }
      function onUp() {
        win.classList.remove("is-resizing");
        window.removeEventListener("pointermove", onMove);
        window.removeEventListener("pointerup", onUp);
      }
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });

    win.querySelectorAll("[data-tr-act]").forEach((btn) => {
      btn.addEventListener("click", (e) => {
        e.stopPropagation();
        const act = btn.getAttribute("data-tr-act");
        if (act === "loop") {
          state.loop = !state.loop;
          btn.classList.toggle("is-active", state.loop);
          return;
        }
        if (act === "play") {
          play();
          return;
        }
        if (act === "pause") {
          pause();
          return;
        }
        if (act === "stop") {
          stop();
          return;
        }
        if (act === "to-start") {
          seek(state.inPoint || 0);
          return;
        }
        if (act === "add-timeline") {
          addSelectionToTimeline();
          return;
        }
        if (act === "more") {
          if (document.getElementById(MENU_ID)) closeMoreMenu();
          else openMoreMenu(btn);
        }
      });
    });

    const scrub = win.querySelector("[data-tr-scrub]");
    scrub?.addEventListener("input", () => {
      if (state.duration <= 0) return;
      seek((parseFloat(scrub.value) / 1000) * state.duration);
    });

    // Click stage to seek
    win.querySelector("[data-tr-stage]")?.addEventListener("pointerdown", (e) => {
      if (e.target.closest("button, input, .tr-inout__in, .tr-inout__out")) return;
      const host =
        state.kind === "audio"
          ? win.querySelector(".tr-wave-host")
          : win.querySelector('[data-tr-preview="video"]');
      if (!host || state.duration <= 0) return;
      const r = host.getBoundingClientRect();
      const pct = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
      seek(pct * state.duration);
    });

    // Drag in/out handles (audio view; also usable conceptually)
    function wireHandle(sel, which) {
      const el = win.querySelector(sel);
      if (!el) return;
      el.addEventListener("pointerdown", (e) => {
        e.preventDefault();
        e.stopPropagation();
        const host = win.querySelector(".tr-wave-host") || win.querySelector("[data-tr-stage]");
        function onMove(ev) {
          const r = host.getBoundingClientRect();
          const pct = Math.max(0, Math.min(1, (ev.clientX - r.left) / r.width));
          const t = pct * state.duration;
          if (which === "in") {
            state.inPoint = Math.min(t, (state.outPoint == null ? state.duration : state.outPoint) - 0.05);
          } else {
            state.outPoint = Math.max(t, state.inPoint + 0.05);
          }
          syncInOutUI();
        }
        function onUp() {
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        }
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      });
    }
    wireHandle("[data-tr-inhandle]", "in");
    wireHandle("[data-tr-outhandle]", "out");
  }

  function open(opts) {
    const win = buildWindow();
    const kind = opts?.kind || "video";
    const name = opts?.name || (kind === "audio" ? "sample_for_project_audio.wav" : "sample_for_project_video.mp4");
    const info = opts?.src
      ? {
          kind,
          src: opts.src,
          name,
          path: opts.path || assetBase().replace(/\//g, "\\"),
        }
      : resolveMedia(kind, name);

    state.eventEl = opts?.eventEl || null;
    loadMedia(info);

    win.hidden = false;
    win.classList.add("is-open");
    if (!win.style.left) {
      const w = Math.min(760, window.innerWidth - 48);
      const h = Math.min(540, window.innerHeight - 48);
      win.style.left = Math.max(24, (window.innerWidth - w) / 2) + "px";
      win.style.top = Math.max(24, (window.innerHeight - h) / 2) + "px";
      win.style.width = w + "px";
      win.style.height = h + "px";
    }
    updateTC();
  }

  function openFromEvent(eventEl) {
    if (!eventEl) return;
    const isAudio = eventEl.classList.contains("event--audio");
    const label =
      eventEl.querySelector(".event__titlebar")?.textContent?.trim() ||
      eventEl.querySelector(".event__label")?.textContent?.trim() ||
      "";
    open({
      kind: isAudio ? "audio" : "video",
      name: label,
      eventEl,
    });
  }

  function openFromMediaCard(card) {
    if (!card) return;
    const name = card.querySelector(".media-card__name")?.textContent?.trim() || "";
    const isAudio =
      card.dataset.trimmerKind === "audio" ||
      !!card.querySelector(".media-card__thumb--audio") ||
      /\.(wav|mp3|flac)/i.test(name);
    open({
      kind: isAudio ? "audio" : "video",
      name,
      src: card.dataset.trimmerSrc || undefined,
    });
  }

  function close() {
    pause();
    closeMoreMenu();
    const win = winEl();
    if (!win) return;
    win.classList.remove("is-open");
    win.hidden = true;
  }

  function isOpen() {
    return !!winEl()?.classList.contains("is-open");
  }

  // Hotkeys when trimmer open (KeyboardEvent.code = layout-independent)
  document.addEventListener("keydown", (e) => {
    if (!isOpen()) return;
    if (e.target?.closest?.("input, textarea, select, [contenteditable=true]")) return;
    const code = e.code;
    if (e.key === "Escape") {
      if (document.getElementById(MENU_ID)) {
        closeMoreMenu();
        e.preventDefault();
        return;
      }
      close();
      e.preventDefault();
      return;
    }
    if (code === "Space" && !e.shiftKey) {
      e.preventDefault();
      state.playing ? pause() : play();
      return;
    }
    if (e.shiftKey && code === "Space") {
      e.preventDefault();
      playFromStart();
      return;
    }
    if (!e.ctrlKey && !e.metaKey && !e.altKey && code === "KeyI") {
      setIn();
      e.preventDefault();
    } else if (!e.ctrlKey && !e.metaKey && !e.altKey && code === "KeyO") {
      setOut();
      e.preventDefault();
    } else if (!e.ctrlKey && !e.metaKey && !e.altKey && code === "KeyM") {
      insertMarker();
      e.preventDefault();
    } else if (!e.ctrlKey && !e.metaKey && !e.altKey && code === "KeyR") {
      insertRegion();
      e.preventDefault();
    } else if (!e.ctrlKey && !e.metaKey && !e.altKey && code === "KeyB") {
      beatDetection();
      e.preventDefault();
    } else if (e.altKey && code === "ArrowLeft") {
      stepFrame(-1);
      e.preventDefault();
    } else if (e.altKey && code === "ArrowRight") {
      stepFrame(1);
      e.preventDefault();
    } else if ((e.ctrlKey || e.metaKey) && code === "Home") {
      seek(0);
      e.preventDefault();
    } else if ((e.ctrlKey || e.metaKey) && code === "End") {
      seek(state.duration);
      e.preventDefault();
    }
  });

  document.addEventListener("mousedown", (e) => {
    if (!document.getElementById(MENU_ID)) return;
    if (e.target.closest("#" + MENU_ID) || e.target.closest('[data-tr-act="more"]')) return;
    closeMoreMenu();
  });

  document.addEventListener("DOMContentLoaded", () => {
    // Double-click media card → Trimmer
    document.querySelector(".media-grid")?.addEventListener("dblclick", (e) => {
      const card = e.target.closest(".media-card");
      if (card) openFromMediaCard(card);
    });
  });

  window.VegasTrimmer = {
    open,
    openFromEvent,
    openFromMediaCard,
    close,
    isOpen,
  };
})();
