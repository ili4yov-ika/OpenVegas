/**
 * Upgrade timeline track headers + events to Vegas-like markup.
 */
(function () {
  const ICO = {
    film:
      '<svg class="track-type-svg" viewBox="0 0 12 12" aria-hidden="true"><rect x="1" y="1" width="10" height="10" rx="1" fill="none" stroke="currentColor" stroke-width="1"/><path d="M1 4h10M1 8h10M4 1v10M8 1v10" stroke="currentColor" stroke-width="1"/></svg>',
    note:
      '<svg class="track-type-svg" viewBox="0 0 12 12" aria-hidden="true"><path d="M5 2v6.2a2 2 0 11-1.2-1.85V4.5L10 3v4.7a2 2 0 11-1.2-1.85V2.2L5 2z" fill="currentColor"/></svg>',
    motion:
      '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2 2h3v3H2V2zm5 0h3v3H7V2zM2 7h3v3H2V7zm5 0h3v3H7V7z" fill="currentColor"/></svg>',
    more:
      '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2 3h8v1.4H2V3zm0 2.8h8v1.4H2V5.8zm0 2.8h8V10H2V8.6z" fill="currentColor"/></svg>',
    panCrop:
      '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M2 2h3v1H3v2H2V2zm5 0h3v3H9V3H7V2zM2 7h1v2h2v1H2V7zm7 0h1v3H7V9h2V7zM4 4h4v4H4V4z" fill="currentColor"/></svg>',
    fx:
      '<svg viewBox="0 0 12 12" aria-hidden="true"><path d="M1.8 8.7h2.1V10H1.8V8.7zm0-6.8H6v1.3H3.1V5h2.1v1.3H3.1V10H1.8V1.9zm5.5 0h1.4L9.9 4l1.3-2.1h1.4L10.7 5l1.9 5h-1.4L9.9 6.5 8.6 10H7.2l1.9-5-1.8-3.1z" fill="currentColor"/></svg>',
  };

  function eventToolsMarkup(kind) {
    if (kind === "video") {
      return (
        '<div class="event__tools">' +
        '<button type="button" class="event-btn event-btn--pc" title="Event Pan/Crop..." aria-label="Event Pan/Crop">' +
        ICO.panCrop +
        "</button>" +
        '<button type="button" class="event-btn event-btn--fx" title="Event FX" aria-label="Event FX">' +
        ICO.fx +
        "</button>" +
        '<button type="button" class="event-btn event-btn--more" title="More" aria-label="More" data-event-menu="video-event-more">' +
        ICO.more +
        "</button>" +
        "</div>"
      );
    }

    return (
      '<div class="event__tools">' +
      '<button type="button" class="event-btn event-btn--fx" title="Event FX" aria-label="Event FX">' +
      ICO.fx +
      "</button>" +
      '<button type="button" class="event-btn event-btn--more" title="More" aria-label="More" data-event-menu="audio-event-more">' +
      ICO.more +
      "</button>" +
      "</div>"
    );
  }

  function videoHeader(num, name) {
    const label = name || "Video";
    return (
      '<div class="track-header__rail" aria-hidden="true">' +
      '<span class="track-num">' +
      num +
      "</span>" +
      '<span class="track-type-ico" title="Video">' +
      ICO.film +
      "</span>" +
      "</div>" +
      '<div class="track-header__body">' +
      '<div class="track-header__ms">' +
      '<button class="ms-btn" type="button" title="Mute">M</button>' +
      '<button class="ms-btn" type="button" title="Solo">S</button>' +
      "</div>" +
      '<div class="track-header__name-wrap">' +
      '<span class="track-header__name" title="Double-click to rename">' +
      escapeHtml(label) +
      "</span>" +
      '<input class="track-header__name-input" type="text" spellcheck="false" maxlength="64" aria-label="Track name" hidden />' +
      "</div>" +
      '<div class="track-slider-row">' +
      '<div class="track-slider-row__meta"><label>Level:</label><span class="track-val">100,0 %</span></div>' +
      '<input class="track-slider" type="range" min="0" max="100" value="100" aria-label="Level" />' +
      "</div>" +
      '<div class="track-header__icons">' +
      '<button type="button" class="track-mini-btn" title="Track Motion">' +
      ICO.motion +
      "</button>" +
      '<button type="button" class="track-mini-btn" title="Track FX">fx</button>' +
      '<button type="button" class="track-mini-btn" title="More">' +
      ICO.more +
      "</button>" +
      "</div>" +
      "</div>"
    );
  }

  function audioHeader(num, name) {
    const label = name || "Audio";
    return (
      '<div class="track-header__rail" aria-hidden="true">' +
      '<span class="track-num">' +
      num +
      "</span>" +
      '<span class="track-type-ico" title="Audio">' +
      ICO.note +
      "</span>" +
      "</div>" +
      '<div class="track-header__body">' +
      '<div class="track-header__ms">' +
      '<button class="ms-btn" type="button" title="Mute">M</button>' +
      '<button class="ms-btn" type="button" title="Solo">S</button>' +
      "</div>" +
      '<div class="track-header__name-wrap">' +
      '<span class="track-header__name" title="Double-click to rename">' +
      escapeHtml(label) +
      "</span>" +
      '<input class="track-header__name-input" type="text" spellcheck="false" maxlength="64" aria-label="Track name" hidden />' +
      "</div>" +
      '<div class="track-slider-row">' +
      '<div class="track-slider-row__meta"><label>Vol:</label><span class="track-val">0,0 dB</span></div>' +
      '<input class="track-slider" type="range" min="0" max="100" value="70" aria-label="Volume" />' +
      "</div>" +
      '<div class="track-slider-row">' +
      '<div class="track-slider-row__meta"><label>Pan:</label><span class="track-val">Center</span></div>' +
      '<input class="track-slider" type="range" min="0" max="100" value="50" aria-label="Pan" />' +
      "</div>" +
      '<div class="track-header__icons">' +
      '<button type="button" class="track-mini-btn" title="Track FX">fx</button>' +
      '<button type="button" class="track-mini-btn track-mini-btn--rec" title="Record Arm">●</button>' +
      '<button type="button" class="track-mini-btn" title="More">' +
      ICO.more +
      "</button>" +
      "</div>" +
      "</div>"
    );
  }

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function upgradeHeaders() {
    document.querySelectorAll(".track-header").forEach((header) => {
      if (header.dataset.tlUpgraded) return;
      header.dataset.tlUpgraded = "1";
      const numEl = header.querySelector(".track-num");
      const num = numEl ? numEl.textContent.trim() : "1";
      const isVideo = header.classList.contains("track-header--video");
      const oldName =
        header.dataset.trackName ||
        header.querySelector(".track-name")?.value?.trim() ||
        header.querySelector(".track-header__name")?.textContent?.trim() ||
        (isVideo ? "Video" : "Audio");
      header.dataset.trackName = oldName;
      header.innerHTML = isVideo ? videoHeader(num, oldName) : audioHeader(num, oldName);
    });
  }

  function waveSvg() {
    // Stereo-looking waveform (L / R channels share the same SVG, CSS splits rows)
    return (
      '<svg class="event__wave-svg" viewBox="0 0 520 40" preserveAspectRatio="none" aria-hidden="true">' +
      '<path fill="rgba(255,255,255,0.55)" d="M0 20 L4 12 8 26 12 8 16 28 20 14 24 30 28 10 32 22 36 6 40 24 44 16 48 32 52 11 56 27 60 15 64 29 68 9 72 23 76 13 80 31 84 17 88 25 92 7 96 28 100 14 104 30 108 10 112 22 116 18 120 33 124 12 128 26 132 8 136 29 140 15 144 24 148 11 152 27 156 19 160 31 164 9 168 23 172 14 176 28 180 16 184 25 188 7 192 30 196 13 200 26 204 10 208 22 212 17 216 32 220 11 224 27 228 8 232 29 236 15 240 24 244 12 248 28 252 18 256 31 260 9 264 23 268 14 272 29 276 16 280 25 284 7 288 30 292 12 296 26 300 10 304 22 308 18 312 32 316 11 320 27 324 8 328 29 332 15 336 24 340 12 344 28 348 17 352 31 356 9 360 23 364 14 368 28 372 16 376 25 380 7 384 30 388 13 392 26 396 10 400 22 404 18 408 32 412 11 416 27 420 8 424 29 428 15 432 24 436 12 440 28 444 17 448 31 452 9 456 23 460 14 464 28 468 16 472 25 476 8 480 30 484 12 488 26 492 10 496 22 500 17 504 31 508 13 512 24 516 19 520 20 Z"/>' +
      "</svg>"
    );
  }

  function upgradeEvent(ev) {
    if (ev.dataset.tlUpgraded) return;
    ev.dataset.tlUpgraded = "1";

    const labelEl = ev.querySelector(".event__label");
    const label = labelEl
      ? labelEl.textContent.trim()
      : ev.classList.contains("event--video")
        ? "sample_for_project_video"
        : "sample_for_project_audio";

    const thumbs = ev.querySelector(".event__thumbs");
    const isVideo = ev.classList.contains("event--video");
    const isAudio = ev.classList.contains("event--audio");
    const showEnvelope = ev.classList.contains("event--crossfade") || ev.querySelector(".event__envelope");

    let body = "";
    if (isVideo && thumbs) {
      body = '<div class="event__body">' + thumbs.outerHTML + "</div>";
    } else if (isAudio) {
      body =
        '<div class="event__body"><div class="event__wave">' +
        '<div class="event__wave-ch">' +
        waveSvg() +
        "</div>" +
        '<div class="event__wave-ch">' +
        waveSvg() +
        "</div>" +
        "</div></div>";
    } else {
      body = '<div class="event__body"></div>';
    }

    const trims =
      '<span class="event__trim event__trim--l"></span><span class="event__trim event__trim--r"></span>';

    const fadeIn = Math.max(0, parseFloat(ev.dataset.fadeIn) || 0);
    const fadeOut = Math.max(0, parseFloat(ev.dataset.fadeOut) || 0);
    const curveIn = ev.dataset.fadeCurveIn || "smooth";
    const curveOut = ev.dataset.fadeCurveOut || "smooth";
    const cIn = FADE_CURVES.find((c) => c.id === curveIn) || FADE_CURVES[3];
    const cOut = FADE_CURVES.find((c) => c.id === curveOut) || FADE_CURVES[3];
    const fades =
      '<div class="event__fade event__fade--in' +
      (fadeIn > 0 ? " has-fade" : "") +
      '" data-fade-side="in" data-fade-curve="' +
      cIn.id +
      '" style="width:' +
      fadeIn +
      'px">' +
      '<svg class="event__fade-svg" viewBox="0 0 1 1" preserveAspectRatio="none" aria-hidden="true">' +
      '<path class="event__fade-fill" d="' +
      cIn.in.fill +
      '"></path>' +
      '<path class="event__fade-line" d="' +
      cIn.in.line +
      '"></path>' +
      "</svg>" +
      '<span class="event__fade-handle" title="Fade In (drag / right-click curve)"></span>' +
      "</div>" +
      '<div class="event__fade event__fade--out' +
      (fadeOut > 0 ? " has-fade" : "") +
      '" data-fade-side="out" data-fade-curve="' +
      cOut.id +
      '" style="width:' +
      fadeOut +
      'px">' +
      '<svg class="event__fade-svg" viewBox="0 0 1 1" preserveAspectRatio="none" aria-hidden="true">' +
      '<path class="event__fade-fill" d="' +
      cOut.out.fill +
      '"></path>' +
      '<path class="event__fade-line" d="' +
      cOut.out.line +
      '"></path>' +
      "</svg>" +
      '<span class="event__fade-handle" title="Fade Out (drag / right-click curve)"></span>' +
      "</div>";

    const envelope = showEnvelope ? '<div class="event__envelope"></div>' : "";

    const tools = eventToolsMarkup(isVideo ? "video" : "audio");

    const velocityHtml =
      isVideo &&
      (ev.classList.contains("is-compressed") ||
        ev.classList.contains("is-stretched"))
        ? '<div class="event__velocity" aria-hidden="true"></div>'
        : "";
    const rateHtml = ev.dataset.rate
      ? '<span class="event__rate">' + ev.dataset.rate + "</span>"
      : "";

    ev.innerHTML =
      trims +
      '<div class="event__titlebar">' +
      label.replace(/\.mp4$|\.wav$/i, "") +
      "</div>" +
      body +
      tools +
      fades +
      envelope +
      velocityHtml +
      rateHtml;
  }

  function syncEventFxBadge(eventEl) {
    if (!eventEl) return;
    if (eventEl.classList.contains("track-header")) {
      const btn = eventEl.querySelector('.track-mini-btn[title="Track FX"]');
      if (!btn) return;
      const raw = (eventEl.dataset.fxChain || "").trim();
      btn.classList.toggle("has-fx", !!raw);
      return;
    }
    const btn = eventEl.querySelector(".event-btn--fx");
    if (!btn) return;
    const raw = (eventEl.dataset.fxChain || "").trim();
    const hasExtra = raw
      .split("|")
      .map((s) => s.trim())
      .filter(Boolean)
      .some((name) => name.toLowerCase() !== "pan/crop");
    btn.classList.toggle("has-fx", hasExtra);
  }

  /** Push Pan/Crop · FX · More left of an outgoing crossfade overlap (Vegas). */
  function syncEventToolsForCrossfades(root) {
    const scope = root || document;
    scope.querySelectorAll(".track-lane").forEach((lane) => {
      const zones = Array.from(lane.querySelectorAll(".crossfade-zone")).map((z) => {
        const left = parseFloat(z.style.left) || 0;
        const width = parseFloat(z.style.width) || 0;
        return { left, right: left + width, width };
      });

      lane.querySelectorAll(".event").forEach((ev) => {
        const tools = ev.querySelector(".event__tools");
        if (!tools) return;
        const left = parseFloat(ev.style.left) || 0;
        const width = parseFloat(ev.style.width) || 0;
        const right = left + width;
        let outOverlap = 0;
        zones.forEach((z) => {
          // Outgoing CF: zone starts inside the event and reaches (near) its right edge
          if (z.left > left + 1 && z.left < right && z.right >= right - 2) {
            outOverlap = Math.max(outOverlap, right - z.left);
          }
        });
        const inset = Math.round(2 + outOverlap);
        tools.style.right = inset + "px";
        const rate = ev.querySelector(".event__rate");
        if (rate) rate.style.right = inset + "px";
        ev.classList.toggle("has-out-crossfade", outOverlap > 0);
      });
    });
  }

  const CF_MIN_OVERLAP = 4;

  function formatCfDur(px) {
    const sec = Math.max(0, px / pxPerSec());
    return sec.toFixed(2).replace(".", ",");
  }

  /**
   * Rebuild blue “X” crossfade zones from event overlaps on a track.
   * Hard cuts (no overlap) leave no zone. Matching fade widths follow overlap;
   * CF-linked fade curves are hidden so only the blue X is shown (Vegas).
   */
  function syncCrossfadeZones(root) {
    const scope = root || document;
    const lanes = [];
    if (scope.classList?.contains("track-lane")) {
      lanes.push(scope);
    } else {
      scope.querySelectorAll(".track-lane").forEach((l) => lanes.push(l));
    }

    lanes.forEach((lane) => {
      lane.querySelectorAll(".crossfade-zone").forEach((z) => z.remove());
      lane.querySelectorAll(".event__fade.is-cf-fade").forEach((f) => f.classList.remove("is-cf-fade"));

      const events = Array.from(lane.querySelectorAll(".event"))
        .map((el) => {
          const left = parseFloat(el.style.left) || 0;
          const width = parseFloat(el.style.width) || 0;
          return { el, left, right: left + width, width };
        })
        .filter((e) => e.width > 0)
        .sort((a, b) => a.left - b.left || a.right - b.right);

      events.forEach((e, i) => {
        e.el.style.zIndex = String(5 + i);
      });

      const fadeOutNeed = new Map();
      const fadeInNeed = new Map();

      for (let i = 0; i < events.length; i++) {
        const a = events[i];
        for (let j = i + 1; j < events.length; j++) {
          const b = events[j];
          if (b.left >= a.right - CF_MIN_OVERLAP) break;
          const ol = Math.max(a.left, b.left);
          const or_ = Math.min(a.right, b.right);
          const w = or_ - ol;
          if (w < CF_MIN_OVERLAP) continue;

          const zone = document.createElement("div");
          zone.className = "crossfade-zone";
          zone.style.left = Math.round(ol) + "px";
          zone.style.width = Math.round(w) + "px";
          zone.dataset.dur = formatCfDur(w);
          zone.title = "Crossfade";
          zone.innerHTML = buildCrossfadeXSvg(a.el, b.el);
          lane.appendChild(zone);

          // Classic CF: later clip starts inside earlier and earlier ends inside later.
          if (Math.abs(ol - b.left) < 1 && Math.abs(or_ - a.right) < 1) {
            fadeOutNeed.set(a.el, Math.max(fadeOutNeed.get(a.el) || 0, Math.round(w)));
            fadeInNeed.set(b.el, Math.max(fadeInNeed.get(b.el) || 0, Math.round(w)));
          }
        }
      }

      fadeOutNeed.forEach((w, el) => {
        setEventFade(el, "out", w);
        el.querySelector('.event__fade[data-fade-side="out"]')?.classList.add("is-cf-fade");
      });
      fadeInNeed.forEach((w, el) => {
        setEventFade(el, "in", w);
        el.querySelector('.event__fade[data-fade-side="in"]')?.classList.add("is-cf-fade");
      });
    });

    syncEventToolsForCrossfades(scope);
  }

  function upgradeEvents() {
    document.querySelectorAll(".event").forEach(upgradeEvent);
    document.querySelectorAll(".event").forEach(syncEventFxBadge);
    syncCrossfadeZones(document);
  }

  function syncPlayheadMarks() {
    document.querySelectorAll(".timeline-panel").forEach((panel) => {
      const playhead = panel.querySelector(".tracks-inner > .playhead");
      const mark = panel.querySelector(".ruler-playhead-mark");
      if (!playhead || !mark) return;
      mark.style.left = playhead.style.left || "0px";
    });
  }

  function isTypingTarget(el) {
    if (!el) return false;
    const tag = el.tagName;
    if (tag === "TEXTAREA" || tag === "SELECT") return true;
    if (tag === "INPUT") {
      const type = (el.type || "text").toLowerCase();
      // Allow hotkeys when focus is on sliders/checkboxes; block only text entry.
      if (type === "range" || type === "checkbox" || type === "radio" || type === "button") {
        return false;
      }
      return true;
    }
    if (el.isContentEditable) return true;
    return false;
  }

  function ensureMarkerLane(panel) {
    let lane = getMarkerLane(panel);
    if (lane) return lane;

    const ruler = panel.querySelector(".timeline-ruler");
    const ticks = panel.querySelector(".ruler-ticks");
    if (!ruler || !ticks) return null;

    let stack = ruler.querySelector(".ruler-stack");
    if (!stack) {
      stack = document.createElement("div");
      stack.className = "ruler-stack";
      ticks.replaceWith(stack);
      stack.appendChild(ticks);
    }

    let markerLane = stack.querySelector(".marker-lane");
    if (!markerLane) {
      markerLane = document.createElement("div");
      markerLane.className = "marker-lane";
      stack.insertBefore(markerLane, stack.firstChild);
    }

    let scroller = markerLane.querySelector(".marker-lane__scroller");
    if (!scroller) {
      scroller = document.createElement("div");
      scroller.className = "marker-lane__scroller";
      markerLane.appendChild(scroller);
    }
    return scroller;
  }

  function pxPerSec() {
    const v = parseFloat(
      getComputedStyle(document.documentElement).getPropertyValue("--px-per-sec")
    );
    return Number.isFinite(v) && v > 0 ? v : 40;
  }

  function pad2(n) {
    return String(n).padStart(2, "0");
  }

  function pad3(n) {
    return String(n).padStart(3, "0");
  }

  function formatTimecodeFromX(x) {
    const ms = Math.max(0, Math.round((x / pxPerSec()) * 1000));
    const h = Math.floor(ms / 3600000);
    const m = Math.floor((ms % 3600000) / 60000);
    const s = Math.floor((ms % 60000) / 1000);
    const milli = ms % 1000;
    return pad2(h) + ":" + pad2(m) + ":" + pad2(s) + "," + pad3(milli);
  }

  function formatTickLabel(sec) {
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = Math.floor(sec % 60);
    return pad2(h) + ":" + pad2(m) + ":" + pad2(s);
  }

  function getTracksInner(panel) {
    return panel.querySelector(".tracks-inner");
  }

  function getTicksScroller(panel) {
    const ticks =
      panel.querySelector('.ruler-ticks[data-context="timeline-ruler"]') ||
      panel.querySelector(".ruler-ticks");
    if (!ticks) return null;
    return ticks.querySelector(".ruler-ticks__scroller") || ticks;
  }

  function getMarkerLane(panel) {
    return panel.querySelector(".marker-lane__scroller") || panel.querySelector(".marker-lane");
  }

  function getPlayheadX(panel) {
    const mark = panel.querySelector(".ruler-playhead-mark");
    if (mark?.style?.left) {
      const n = parseFloat(mark.style.left);
      if (Number.isFinite(n)) return n;
    }
    const playhead = panel.querySelector(".tracks-inner > .playhead");
    if (playhead?.style?.left) {
      const n = parseFloat(playhead.style.left);
      if (Number.isFinite(n)) return n;
    }
    return null;
  }

  function nextMarkerId(panel) {
    const n = Number(panel.dataset.markerSeq || "0") + 1;
    panel.dataset.markerSeq = String(n);
    return "m" + n;
  }

  function renumberMarkers(panel) {
    const target = getMarkerLane(panel);
    if (!target) return;
    Array.from(target.querySelectorAll(".ruler-marker")).forEach((marker, i) => {
      const num = String(i + 1);
      marker.dataset.num = num;
      const numEl = marker.querySelector(".ruler-marker__num");
      if (numEl) numEl.textContent = num;
    });
  }

  function updateTimecodeDisplays(panel, x) {
    const tc = formatTimecodeFromX(x);
    const corner = panel.querySelector(".ruler-corner span:not(.ruler-corner__grip)");
    if (corner) corner.textContent = tc;
    panel.querySelectorAll("[data-chrome='timecode']").forEach((el) => {
      el.textContent = tc;
    });
    const tools = panel.querySelector(".timeline-tools");
    if (tools) tools.setAttribute("data-timecode", tc);
  }

  function setPlayheadX(panel, x) {
    const inner = getTracksInner(panel);
    const max = inner
      ? Math.max(0, (inner.scrollWidth || parseFloat(inner.style.minWidth) || 2000) - 1)
      : 100000;
    const clamped = Math.max(0, Math.min(max, Math.round(x)));
    const left = clamped + "px";
    const playhead = panel.querySelector(".tracks-inner > .playhead");
    const mark = panel.querySelector(".ruler-playhead-mark");
    if (playhead) playhead.style.left = left;
    if (mark) mark.style.left = left;
    updateTimecodeDisplays(panel, clamped);
    return clamped;
  }

  function clientXToTimelineX(panel, clientX) {
    const area = panel.querySelector(".tracks-area");
    if (area) {
      const rect = area.getBoundingClientRect();
      return clientX - rect.left + area.scrollLeft;
    }
    const ticks = panel.querySelector(".ruler-ticks");
    if (!ticks) return 0;
    const rect = ticks.getBoundingClientRect();
    return clientX - rect.left;
  }

  function syncLoopBand(panel) {
    const loop = getMarkerLane(panel)?.querySelector(".ruler-loop");
    const inner = getTracksInner(panel);
    if (!inner) return;
    let band = inner.querySelector(".timeline-loopband");
    const legacy = inner.querySelector(".timeline-selband");
    if (legacy) {
      legacy.classList.add("timeline-loopband");
      legacy.classList.remove("timeline-selband");
      band = legacy;
    }
    if (!loop) {
      if (band) band.remove();
      return;
    }
    if (!band) {
      band = document.createElement("div");
      band.className = "timeline-loopband";
      inner.insertBefore(band, inner.firstChild);
    }
    band.style.left = loop.style.left || "0px";
    band.style.width = loop.style.width || "0px";
  }

  function clearLoopSeed(panel) {
    getMarkerLane(panel)
      ?.querySelectorAll(".ruler-loop-seed")
      .forEach((el) => el.remove());
  }

  function ensureLoopSeed(panel, atX) {
    const scroller = ensureMarkerLane(panel);
    if (!scroller) return null;
    if (scroller.querySelector(".ruler-loop")) {
      clearLoopSeed(panel);
      return null;
    }
    let seed = scroller.querySelector(".ruler-loop-seed");
    if (!seed) {
      seed = document.createElement("span");
      seed.className = "ruler-loop-seed";
      seed.title = "Drag to create Loop Region";
      seed.setAttribute("aria-label", "Loop Region start");
      scroller.appendChild(seed);
    }
    const x = Number.isFinite(atX) ? atX : parseFloat(seed.style.left) || 0;
    seed.style.left = Math.max(0, Math.round(x)) + "px";
    return seed;
  }

  function decorateLoop(loop) {
    if (!loop) return;
    if (!loop.querySelector(".ruler-loop__handle--l")) {
      loop.insertAdjacentHTML(
        "afterbegin",
        '<span class="ruler-loop__handle ruler-loop__handle--l" data-loop-handle="l"></span>' +
          '<span class="ruler-loop__handle ruler-loop__handle--r" data-loop-handle="r"></span>'
      );
    }
  }

  function setLoopRegion(panel, left, width) {
    const scroller = ensureMarkerLane(panel);
    if (!scroller) return null;
    // Remove any loop left in the time-ticks row (old layout)
    getTicksScroller(panel)
      ?.querySelectorAll(".ruler-loop")
      .forEach((el) => el.remove());

    const w = Math.max(0, Math.round(width));
    const x = Math.max(0, Math.round(left));

    // Collapse to zero → single yellow triangle seed
    if (w <= 0) {
      scroller.querySelectorAll(".ruler-loop").forEach((el) => el.remove());
      syncLoopBand(panel);
      return ensureLoopSeed(panel, x);
    }

    clearLoopSeed(panel);

    let loop = scroller.querySelector(".ruler-loop");
    if (!loop) {
      loop = document.createElement("span");
      loop.className = "ruler-loop";
      scroller.appendChild(loop);
    }
    decorateLoop(loop);
    loop.style.left = x + "px";
    loop.style.width = w + "px";
    scroller.querySelectorAll(".ruler-selection").forEach((el) => el.remove());
    syncLoopBand(panel);
    return loop;
  }

  function rebuildTimeTicks(panel) {
    const scroller = getTicksScroller(panel);
    if (!scroller) return;
    const inner = getTracksInner(panel);
    const minW = inner
      ? parseFloat(inner.style.minWidth) ||
        parseFloat(getComputedStyle(inner).minWidth) ||
        Number(inner.dataset.baseMinW) ||
        2000
      : 2000;
    const pps = pxPerSec();
    const durationSec = Math.max(30, Math.ceil(minW / pps) + 2);

    scroller.querySelectorAll(".ruler-tick, .ruler-selection").forEach((el) => el.remove());

    for (let sec = 0; sec <= durationSec; sec++) {
      const tick = document.createElement("span");
      tick.style.left = sec * pps + "px";
      if (sec % 5 === 0) {
        tick.className = "ruler-tick ruler-tick--major";
        tick.textContent = formatTickLabel(sec);
      } else {
        tick.className = "ruler-tick ruler-tick--minor";
      }
      scroller.appendChild(tick);
    }
  }

  function enhanceTimelineRuler(panel) {
    const ruler = panel.querySelector(".timeline-ruler");
    if (!ruler || ruler.dataset.timeEnhanced) return;
    ruler.dataset.timeEnhanced = "1";

    const ticks =
      ruler.querySelector('.ruler-ticks[data-context="timeline-ruler"]') ||
      ruler.querySelector(".ruler-ticks");
    if (!ticks) return;

    let stack = ruler.querySelector(".ruler-stack");
    if (!stack) {
      stack = document.createElement("div");
      stack.className = "ruler-stack";
      ticks.replaceWith(stack);
      stack.appendChild(ticks);
    }

    let markerLane = stack.querySelector(".marker-lane");
    if (!markerLane) {
      markerLane = document.createElement("div");
      markerLane.className = "marker-lane";
      stack.insertBefore(markerLane, stack.firstChild);
    }
    if (!markerLane.querySelector(".marker-lane__scroller")) {
      const laneScroller = document.createElement("div");
      laneScroller.className = "marker-lane__scroller";
      markerLane.appendChild(laneScroller);
    }

    // Preserve playhead mark / loop; rebuild time ticks
    const existingLoop =
      ticks.querySelector(".ruler-loop") ||
      getMarkerLane(panel)?.querySelector(".ruler-loop");
    const loopLeft = existingLoop ? parseFloat(existingLoop.style.left) : NaN;
    const loopWidth = existingLoop ? parseFloat(existingLoop.style.width) : NaN;

    // Move stray markers AND old loop from ticks into indicator lane
    const laneScroller = getMarkerLane(panel);
    ticks.querySelectorAll(".ruler-marker, .ruler-loop").forEach((el) => {
      if (laneScroller) laneScroller.appendChild(el);
    });

    rebuildTimeTicks(panel);

    if (Number.isFinite(loopLeft) && Number.isFinite(loopWidth)) {
      setLoopRegion(panel, loopLeft, loopWidth);
    } else {
      const sel = panel.querySelector(".timeline-selband, .timeline-loopband");
      if (sel) {
        const l = parseFloat(sel.style.left);
        const w = parseFloat(sel.style.width);
        if (Number.isFinite(l) && Number.isFinite(w)) setLoopRegion(panel, l, w);
        else ensureLoopSeed(panel);
      } else {
        ensureLoopSeed(panel);
      }
    }

    // Ensure corner exists for empty pages that only had ticks
    if (!ruler.querySelector(".ruler-corner")) {
      const corner = document.createElement("div");
      corner.className = "ruler-corner";
      corner.setAttribute("data-context", "time-display");
      corner.innerHTML =
        '<span class="ruler-corner__grip" aria-hidden="true"></span><span>00:00:00,000</span>';
      ruler.insertBefore(corner, ruler.firstChild);
    }
  }

  function initPlayheadScrub() {
    document.querySelectorAll(".timeline-panel").forEach((panel) => {
      if (panel.dataset.playheadWired) return;
      panel.dataset.playheadWired = "1";

      const playhead = panel.querySelector(".tracks-inner > .playhead");
      const mark = panel.querySelector(".ruler-playhead-mark");
      const area = panel.querySelector(".tracks-area");
      const ticks = panel.querySelector(".ruler-ticks");

      function scrubTo(clientX) {
        setPlayheadX(panel, clientXToTimelineX(panel, clientX));
      }

      function startDrag(e) {
        if (e.button !== 0) return;
        e.preventDefault();
        e.stopPropagation();
        document.body.classList.add("is-scrubbing-playhead");
        if (playhead) playhead.classList.add("is-dragging");
        scrubTo(e.clientX);

        function onMove(ev) {
          scrubTo(ev.clientX);
        }
        function onUp() {
          document.body.classList.remove("is-scrubbing-playhead");
          if (playhead) playhead.classList.remove("is-dragging");
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        }
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      }

      if (playhead) playhead.addEventListener("pointerdown", startDrag);
      if (mark) mark.addEventListener("pointerdown", startDrag);

      if (ticks) {
        ticks.addEventListener("pointerdown", (e) => {
          if (e.button !== 0) return;
          if (e.target.closest(".ruler-marker, .ruler-loop, .ruler-loop__handle, .ruler-loop-seed")) return;
          startDrag(e);
        });
      }

      const markerLane = panel.querySelector(".marker-lane");
      if (markerLane) {
        markerLane.addEventListener("pointerdown", (e) => {
          if (e.button !== 0) return;
          if (e.target.closest(".ruler-marker, .ruler-loop, .ruler-loop__handle, .ruler-loop-seed")) return;
          // Click empty indicator strip → move playhead
          startDrag(e);
        });
      }

      if (area) {
        area.addEventListener("pointerdown", (e) => {
          if (e.button !== 0) return;
          if (e.target.closest(".event, .event-btn, .playhead, button, input")) return;
          startDrag(e);
        });
      }
    });
  }

  function initLoopInteractions() {
    document.querySelectorAll(".timeline-panel").forEach((panel) => {
      if (panel.dataset.loopWired) return;
      panel.dataset.loopWired = "1";

      panel.addEventListener("pointerdown", (e) => {
        if (e.button !== 0) return;

        const seed = e.target.closest(".ruler-loop-seed");
        if (seed && panel.contains(seed)) {
          e.preventDefault();
          e.stopPropagation();

          const startClientX = e.clientX;
          const seedLeft = parseFloat(seed.style.left) || 0;
          let created = false;
          document.body.classList.add("is-scrubbing-playhead");

          function onMove(ev) {
            const dx = ev.clientX - startClientX;
            if (dx < 4 && !created) return;
            created = true;
            setLoopRegion(panel, seedLeft, Math.max(0, dx));
          }
          function onUp() {
            document.body.classList.remove("is-scrubbing-playhead");
            window.removeEventListener("pointermove", onMove);
            window.removeEventListener("pointerup", onUp);
            // Click without drag: keep seed. If region was created, seed is already gone.
            if (!created) ensureLoopSeed(panel);
          }
          window.addEventListener("pointermove", onMove);
          window.addEventListener("pointerup", onUp);
          return;
        }

        const handle = e.target.closest("[data-loop-handle]");
        const loop = e.target.closest(".ruler-loop");
        if (!loop || !panel.contains(loop)) return;
        e.preventDefault();
        e.stopPropagation();

        const startX = e.clientX;
        const origLeft = parseFloat(loop.style.left) || 0;
        const origWidth = parseFloat(loop.style.width) || 160;
        const mode = handle ? handle.getAttribute("data-loop-handle") : "move";

        function onMove(ev) {
          const dx = ev.clientX - startX;
          if (mode === "l") {
            const nextLeft = Math.max(0, origLeft + dx);
            const nextWidth = origWidth - (nextLeft - origLeft);
            setLoopRegion(panel, nextLeft, nextWidth);
          } else if (mode === "r") {
            setLoopRegion(panel, origLeft, origWidth + dx);
          } else {
            setLoopRegion(panel, Math.max(0, origLeft + dx), origWidth);
          }
        }
        function onUp() {
          // If handles were removed mid-drag (collapsed to seed), stop cleanly.
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        }
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      });
    });
  }

  function setMarkerX(panel, marker, x) {
    if (!marker || !panel) return;
    const inner = getTracksInner(panel);
    const max = inner
      ? Math.max(0, (inner.scrollWidth || parseFloat(inner.style.minWidth) || 2000) - 1)
      : 100000;
    const clamped = Math.max(0, Math.min(max, Math.round(x)));
    const left = clamped + "px";
    marker.style.left = left;
    const id = marker.dataset.markerId;
    if (id) {
      panel.querySelectorAll('.marker-guide[data-marker-id="' + id + '"]').forEach((g) => {
        g.style.left = left;
      });
    }
    return clamped;
  }

  function initMarkerDrag() {
    document.querySelectorAll(".timeline-panel").forEach((panel) => {
      if (panel.dataset.markerDragWired) return;
      panel.dataset.markerDragWired = "1";

      panel.addEventListener("pointerdown", (e) => {
        const head = e.target.closest(".ruler-marker__head");
        if (!head || e.button !== 0) return;
        const marker = head.closest(".ruler-marker");
        if (!marker || !panel.contains(marker)) return;

        e.preventDefault();
        e.stopPropagation();

        const startClientX = e.clientX;
        const origLeft = parseFloat(marker.style.left) || 0;
        marker.classList.add("is-dragging");
        document.body.classList.add("is-scrubbing-playhead");

        function onMove(ev) {
          const dx = ev.clientX - startClientX;
          setMarkerX(panel, marker, origLeft + dx);
        }
        function onUp() {
          marker.classList.remove("is-dragging");
          document.body.classList.remove("is-scrubbing-playhead");
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        }
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      });
    });
  }

  function applyMarkerLabel(marker, name) {
    if (!marker) return;
    const labelEl = marker.querySelector(".ruler-marker__label");
    const text = String(name || "").trim();
    if (labelEl) labelEl.textContent = text;
    marker.title = text || ("Marker " + (marker.dataset.num || ""));
  }

  function endMarkerEdit(marker, commit) {
    if (!marker) return;
    const input = marker.querySelector(".ruler-marker__input");
    const labelEl = marker.querySelector(".ruler-marker__label");
    if (!input) {
      marker.classList.remove("is-editing");
      return;
    }
    if (commit) applyMarkerLabel(marker, input.value);
    input.remove();
    marker.classList.remove("is-editing");
    if (labelEl) labelEl.hidden = false;
  }

  function beginMarkerEdit(marker) {
    if (!marker) return;
    const other = document.querySelector(".ruler-marker.is-editing");
    if (other && other !== marker) endMarkerEdit(other, true);

    const labelEl = marker.querySelector(".ruler-marker__label");
    const current = (labelEl?.textContent || "").trim();

    marker.classList.add("is-editing");
    if (labelEl) labelEl.hidden = true;

    let input = marker.querySelector(".ruler-marker__input");
    if (!input) {
      input = document.createElement("input");
      input.type = "text";
      input.className = "ruler-marker__input";
      input.setAttribute("spellcheck", "false");
      input.setAttribute("autocomplete", "off");
      marker.appendChild(input);
    }

    input.value = current;

    let done = false;
    function finish(commit) {
      if (done) return;
      done = true;
      input.removeEventListener("keydown", onKey);
      input.removeEventListener("blur", onBlur);
      endMarkerEdit(marker, commit);
    }
    function onKey(e) {
      if (e.key === "Enter") {
        e.preventDefault();
        e.stopPropagation();
        finish(true);
      } else if (e.key === "Escape") {
        e.preventDefault();
        e.stopPropagation();
        finish(false);
      }
      e.stopPropagation();
    }
    function onBlur() {
      finish(true);
    }
    input.addEventListener("keydown", onKey);
    input.addEventListener("blur", onBlur);

    requestAnimationFrame(() => {
      if (!input.isConnected) return;
      input.focus();
      input.select();
    });
  }

  function placeMarker(panel, x, opts) {
    opts = opts || {};
    const target = ensureMarkerLane(panel);
    const inner = getTracksInner(panel);
    if (!target) return null;

    const id = opts.id || nextMarkerId(panel);
    const num = opts.num != null ? String(opts.num) : String(target.querySelectorAll(".ruler-marker").length + 1);
    const label = opts.label != null ? String(opts.label) : "";

    const marker = document.createElement("span");
    marker.className = "ruler-marker";
    marker.style.left = x + "px";
    marker.dataset.num = num;
    marker.dataset.markerId = id;
    marker.setAttribute("data-context", "timeline-marker");
    marker.title = label || "Marker " + num;
    marker.innerHTML =
      '<span class="ruler-marker__head" title="Drag marker">' +
      '<span class="ruler-marker__num">' +
      num +
      "</span>" +
      "</span>" +
      '<span class="ruler-marker__label"></span>';

    target.appendChild(marker);
    applyMarkerLabel(marker, label);

    if (inner) {
      const guide = document.createElement("div");
      guide.className = "marker-guide";
      guide.dataset.markerId = id;
      guide.style.left = x + "px";
      guide.setAttribute("aria-hidden", "true");
      inner.appendChild(guide);
    }

    return marker;
  }

  function seedMarkersFromDataset(panel) {
    const raw = panel.dataset.markers;
    if (!raw) return;
    let items;
    try {
      items = JSON.parse(raw);
    } catch (_) {
      return;
    }
    if (!Array.isArray(items) || !items.length) return;

    const pps = pxPerSec();
    let maxNum = 0;
    items.forEach((item) => {
      const t = Number(item.t);
      const x = Number.isFinite(item.x)
        ? Number(item.x)
        : Number.isFinite(t)
          ? t * pps
          : NaN;
      if (!Number.isFinite(x)) return;
      const num = item.num != null ? Number(item.num) : null;
      if (Number.isFinite(num)) maxNum = Math.max(maxNum, num);
      placeMarker(panel, x, {
        num: Number.isFinite(num) ? num : undefined,
        label: item.label || "",
        id: item.id,
      });
    });
    if (maxNum > 0) {
      const cur = Number(panel.dataset.markerSeq || "0");
      if (maxNum > cur) panel.dataset.markerSeq = String(maxNum);
    }
  }

  function insertMarkerAtPlayhead(panel) {
    if (!panel) return;
    let x = getPlayheadX(panel);
    if (x === null) x = 0;
    const marker = placeMarker(panel, x);
    if (marker) requestAnimationFrame(() => beginMarkerEdit(marker));
  }

  function deleteMarker(marker) {
    if (!marker) return;
    endMarkerEdit(marker, false);
    const panel = marker.closest(".timeline-panel");
    const id = marker.dataset.markerId;
    marker.remove();
    if (panel && id) {
      panel.querySelectorAll('.marker-guide[data-marker-id="' + id + '"]').forEach((g) => g.remove());
      renumberMarkers(panel);
    }
  }

  function renameMarker(marker) {
    beginMarkerEdit(marker);
  }

  function goToMarker(marker) {
    if (!marker) return;
    const panel = marker.closest(".timeline-panel");
    if (!panel) return;
    const x = parseFloat(marker.style.left);
    if (!Number.isFinite(x)) return;
    setPlayheadX(panel, x);
  }

  function setEventFade(eventEl, side, widthPx) {
    if (!eventEl) return;
    const fade = eventEl.querySelector('.event__fade[data-fade-side="' + side + '"]');
    if (!fade) return;
    const w = Math.max(0, Math.round(widthPx));
    fade.style.width = w + "px";
    fade.classList.toggle("has-fade", w > 0);
    if (side === "in") eventEl.dataset.fadeIn = String(w);
    else eventEl.dataset.fadeOut = String(w);
  }

  // Vegas-style fade curve presets (viewBox 0 0 1 1; y=0 top).
  const FADE_CURVES = [
    {
      id: "linear",
      in: { line: "M0,1 L1,0", fill: "M0,0 L0,1 L1,0 Z" },
      out: { line: "M0,0 L1,1", fill: "M0,0 L1,1 L1,0 Z" },
    },
    {
      id: "fast-slow",
      in: { line: "M0,1 Q0.2,0.15 1,0", fill: "M0,0 L0,1 Q0.2,0.15 1,0 Z" },
      out: { line: "M0,0 Q0.8,0.15 1,1", fill: "M0,0 Q0.8,0.15 1,1 L1,0 Z" },
    },
    {
      id: "slow-fast",
      in: { line: "M0,1 Q0.8,0.85 1,0", fill: "M0,0 L0,1 Q0.8,0.85 1,0 Z" },
      out: { line: "M0,0 Q0.2,0.85 1,1", fill: "M0,0 Q0.2,0.85 1,1 L1,0 Z" },
    },
    {
      // Vegas-like Smooth: arched pair that forms an “eye” in a crossfade.
      id: "smooth",
      in: {
        line: "M0,1 Q0.5,0.82 1,0",
        fill: "M0,0 L0,1 Q0.5,0.82 1,0 Z",
      },
      out: {
        line: "M0,0 Q0.5,0.18 1,1",
        fill: "M0,0 Q0.5,0.18 1,1 L1,0 Z",
      },
    },
    {
      id: "sharp-slow-fast",
      in: { line: "M0,1 Q0.92,0.92 1,0", fill: "M0,0 L0,1 Q0.92,0.92 1,0 Z" },
      out: { line: "M0,0 Q0.08,0.92 1,1", fill: "M0,0 Q0.08,0.92 1,1 L1,0 Z" },
    },
    {
      id: "sharp-fast-slow",
      in: { line: "M0,1 Q0.08,0.08 1,0", fill: "M0,0 L0,1 Q0.08,0.08 1,0 Z" },
      out: { line: "M0,0 Q0.92,0.08 1,1", fill: "M0,0 Q0.92,0.08 1,1 L1,0 Z" },
    },
    {
      id: "sharp-s",
      in: { line: "M0,1 C0.55,1 0.45,0 1,0", fill: "M0,0 L0,1 C0.55,1 0.45,0 1,0 Z" },
      out: { line: "M0,0 C0.45,0 0.55,1 1,1", fill: "M0,0 C0.45,0 0.55,1 1,1 L1,0 Z" },
    },
  ];

  function getFadeCurve(fadeEl) {
    const id = fadeEl?.dataset?.fadeCurve || "smooth";
    return FADE_CURVES.find((c) => c.id === id) || FADE_CURVES[3];
  }

  function curveById(id) {
    return FADE_CURVES.find((c) => c.id === id) || FADE_CURVES[3];
  }

  /** Blue arched X from the outgoing + incoming fade curves (Vegas-style). */
  function buildCrossfadeXSvg(outEvent, inEvent) {
    const outFade = outEvent?.querySelector?.('.event__fade[data-fade-side="out"]');
    const inFade = inEvent?.querySelector?.('.event__fade[data-fade-side="in"]');
    const outId =
      outEvent?.dataset?.fadeCurveOut || outFade?.dataset?.fadeCurve || "smooth";
    const inId =
      inEvent?.dataset?.fadeCurveIn || inFade?.dataset?.fadeCurve || "smooth";
    const outPath = curveById(outId).out.line;
    const inPath = curveById(inId).in.line;
    return (
      '<svg class="crossfade-zone__x" viewBox="0 0 1 1" preserveAspectRatio="none" aria-hidden="true">' +
      '<path class="crossfade-zone__line crossfade-zone__line--out" d="' +
      outPath +
      '"></path>' +
      '<path class="crossfade-zone__line crossfade-zone__line--in" d="' +
      inPath +
      '"></path>' +
      "</svg>"
    );
  }

  function applyFadeCurve(fadeEl, curveId) {
    if (!fadeEl) return;
    const side = fadeEl.dataset.fadeSide === "out" ? "out" : "in";
    const curve = curveById(curveId);
    fadeEl.dataset.fadeCurve = curve.id;
    const eventEl = fadeEl.closest(".event");
    if (eventEl) {
      if (side === "in") eventEl.dataset.fadeCurveIn = curve.id;
      else eventEl.dataset.fadeCurveOut = curve.id;
    }
    const paths = curve[side];
    const fill = fadeEl.querySelector(".event__fade-fill");
    const line = fadeEl.querySelector(".event__fade-line");
    if (fill) fill.setAttribute("d", paths.fill);
    if (line) line.setAttribute("d", paths.line);

    // Refresh CF “X” so it follows the chosen curve.
    if (fadeEl.classList.contains("is-cf-fade")) {
      const lane = fadeEl.closest(".track-lane");
      if (lane) syncCrossfadeZones(lane);
    }
  }

  function curveThumbSvg(curve, side) {
    const paths = curve[side === "out" ? "out" : "in"];
    return (
      '<svg class="fade-curve-menu__svg" viewBox="0 0 1 1" preserveAspectRatio="none" aria-hidden="true">' +
      '<path class="fade-curve-menu__fill" d="' +
      paths.fill +
      '"></path>' +
      '<path class="fade-curve-menu__line" d="' +
      paths.line +
      '"></path>' +
      "</svg>"
    );
  }

  function closeFadeCurveMenu() {
    document.getElementById("fade-curve-menu")?.remove();
  }

  function openFadeCurveMenu(handle, clientX, clientY) {
    closeFadeCurveMenu();
    const fade = handle.closest(".event__fade");
    if (!fade) return;
    const side = fade.dataset.fadeSide === "out" ? "out" : "in";
    const current = fade.dataset.fadeCurve || "smooth";
    const lowpass = fade.dataset.fadeLowpass === "1";

    const menu = document.createElement("div");
    menu.id = "fade-curve-menu";
    menu.className = "fade-curve-menu";
    menu.innerHTML =
      '<div class="fade-curve-menu__list">' +
      FADE_CURVES.map(
        (c) =>
          '<button type="button" class="fade-curve-menu__item' +
          (c.id === current ? " is-selected" : "") +
          '" data-curve="' +
          c.id +
          '">' +
          '<span class="fade-curve-menu__check" aria-hidden="true">' +
          (c.id === current ? "✓" : "") +
          "</span>" +
          '<span class="fade-curve-menu__thumb">' +
          curveThumbSvg(c, side) +
          "</span>" +
          "</button>"
      ).join("") +
      "</div>" +
      '<div class="fade-curve-menu__sep"></div>' +
      '<button type="button" class="fade-curve-menu__lowpass' +
      (lowpass ? " is-checked" : "") +
      '" data-fade-lowpass>' +
      '<span class="fade-curve-menu__check" aria-hidden="true">' +
      (lowpass ? "✓" : "") +
      "</span>" +
      "Fade Lowpass</button>";

    document.body.appendChild(menu);

    const pad = 6;
    const w = menu.offsetWidth || 150;
    const h = menu.offsetHeight || 260;
    let left = clientX;
    let top = clientY;
    if (left + w > window.innerWidth - pad) left = window.innerWidth - w - pad;
    if (top + h > window.innerHeight - pad) top = window.innerHeight - h - pad;
    menu.style.left = Math.max(pad, left) + "px";
    menu.style.top = Math.max(pad, top) + "px";

    menu.addEventListener("mousedown", (e) => e.stopPropagation());
    menu.addEventListener("contextmenu", (e) => e.preventDefault());
    menu.addEventListener("click", (e) => {
      e.stopPropagation();
      const lowBtn = e.target.closest("[data-fade-lowpass]");
      if (lowBtn) {
        const on = fade.dataset.fadeLowpass !== "1";
        fade.dataset.fadeLowpass = on ? "1" : "0";
        fade.classList.toggle("has-lowpass", on);
        lowBtn.classList.toggle("is-checked", on);
        lowBtn.querySelector(".fade-curve-menu__check").textContent = on ? "✓" : "";
        return;
      }
      const item = e.target.closest("[data-curve]");
      if (!item) return;
      applyFadeCurve(fade, item.getAttribute("data-curve"));
      closeFadeCurveMenu();
    });
  }

  function initFadeCurveMenu() {
    document.addEventListener(
      "contextmenu",
      (e) => {
        const handle = e.target.closest(".event__fade-handle");
        if (!handle) return;
        e.preventDefault();
        e.stopPropagation();
        window.VegasMenus?.closeAllDropdowns?.();
        document.getElementById("context-menu")?.classList.remove("is-open");
        openFadeCurveMenu(handle, e.clientX, e.clientY);
      },
      true
    );

    document.addEventListener("mousedown", (e) => {
      if (!e.target.closest("#fade-curve-menu, .event__fade-handle")) {
        closeFadeCurveMenu();
      }
    });

    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape") closeFadeCurveMenu();
    });
  }

  function initFadeDrag() {
    document.addEventListener(
      "pointerdown",
      (e) => {
        if (e.button !== 0) return;
        const handle = e.target.closest(".event__fade-handle");
        if (!handle) return;
        const fade = handle.closest(".event__fade");
        const eventEl = handle.closest(".event");
        if (!fade || !eventEl) return;

        e.preventDefault();
        e.stopPropagation();
        closeFadeCurveMenu();

        const side = fade.dataset.fadeSide === "out" ? "out" : "in";
        const rect = eventEl.getBoundingClientRect();
        const maxFade = Math.max(8, rect.width * 0.48);
        const otherSide = side === "in" ? "out" : "in";
        const otherW = parseFloat(eventEl.dataset[otherSide === "in" ? "fadeIn" : "fadeOut"]) || 0;

        fade.classList.add("is-dragging");
        document.body.classList.add("is-dragging-fade");

        function widthFromClientX(clientX) {
          let w;
          if (side === "in") w = clientX - rect.left;
          else w = rect.right - clientX;
          w = Math.max(0, Math.min(maxFade, w));
          if (w + otherW > rect.width - 4) w = Math.max(0, rect.width - 4 - otherW);
          return w;
        }

        setEventFade(eventEl, side, widthFromClientX(e.clientX));

        function onMove(ev) {
          setEventFade(eventEl, side, widthFromClientX(ev.clientX));
        }
        function onUp() {
          fade.classList.remove("is-dragging");
          document.body.classList.remove("is-dragging-fade");
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        }
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      },
      true
    );
  }

  function placeLoopRegion(panel) {
    const x = getPlayheadX(panel);
    if (x === null) return;
    const existing = getMarkerLane(panel)?.querySelector(".ruler-loop");
    const defaultW = existing?.style?.width ? parseFloat(existing.style.width) : 160;
    const width = Number.isFinite(defaultW) ? defaultW : 160;
    setLoopRegion(panel, x, width);
  }

  document.addEventListener("DOMContentLoaded", () => {
    upgradeHeaders();
    window.VegasTimelineResize?.ensureTrackHeightGrips?.(document);
    upgradeEvents();
    document.querySelectorAll(".timeline-panel").forEach((panel) => {
      enhanceTimelineRuler(panel);
      seedMarkersFromDataset(panel);
      syncPlayheadMarks();
      const startX = parseFloat(panel.dataset.initialPlayhead);
      setPlayheadX(panel, Number.isFinite(startX) ? startX : 0);
    });
    initPlayheadScrub();
    initLoopInteractions();
    initMarkerDrag();
    initFadeDrag();
    initFadeCurveMenu();

    document.addEventListener("click", (e) => {
      const btn = e.target.closest(".event-btn--more");
      if (!btn) return;
      e.preventDefault();
      e.stopPropagation();
      const menuType = btn.getAttribute("data-event-menu");
      if (!menuType || !window.VegasMenus?.openContextMenu) return;
      window.VegasMenus.closeAllDropdowns?.();
      const rect = btn.getBoundingClientRect();
      window.VegasMenus.openContextMenu(menuType, rect.left, rect.bottom + 2, btn.closest(".event"));
    }, true);

    document.addEventListener("click", (e) => {
      const btn = e.target.closest('button[title="Insert Marker"], button[title="Insert Region"]');
      if (!btn) return;
      const panel = btn.closest(".timeline-panel");
      if (!panel) return;

      const title = btn.getAttribute("title");
      if (title === "Insert Marker") insertMarkerAtPlayhead(panel);
      else if (title === "Insert Region") placeLoopRegion(panel);
    });

    document.addEventListener("keydown", (e) => {
      if (e.repeat) return;
      if (e.defaultPrevented) return;
      if (isTypingTarget(e.target)) return;
      if (e.ctrlKey || e.metaKey || e.altKey) return;

      // Use e.code so hotkeys work on RU keyboard layout (KeyM → «ь»).
      if (e.code === "KeyM") {
        e.preventDefault();
        const panel = document.querySelector(".timeline-panel");
        if (panel) insertMarkerAtPlayhead(panel);
      } else if (e.code === "KeyL") {
        e.preventDefault();
        const panel = document.querySelector(".timeline-panel");
        if (panel) placeLoopRegion(panel);
      }
    });

    document.addEventListener("vegas:context-action", (e) => {
      const { type, action, target } = e.detail || {};
      if (type !== "timeline-marker" || !target) return;
      const marker = target.closest(".ruler-marker") || target;
      if (action === "marker-delete") deleteMarker(marker);
      else if (action === "marker-rename") renameMarker(marker);
      else if (action === "marker-goto") goToMarker(marker);
    });
  });

  window.VegasTimelineChrome = {
    syncEventFxBadge,
    syncEventToolsForCrossfades,
    syncCrossfadeZones,
    rebuildTimeTicks,
    pxPerSec,
    applyFadeCurve,
    upgradeEvent,
    upgradeEvents,
  };
})();

document.addEventListener("vegas:events-changed", () => {
  window.VegasTimelineChrome?.upgradeEvents?.();
  window.VegasTimelineChrome?.syncCrossfadeZones?.(document);
});
