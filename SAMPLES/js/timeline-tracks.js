/**
 * Track header context actions: insert / duplicate / delete / rename / color / mute / solo.
 */
(function () {
  const TRACK_COLORS = [
    "#6b4a8a",
    "#4a6a9a",
    "#2a8a8a",
    "#3a8a4a",
    "#8a8a2a",
    "#9a6a2a",
    "#9a3a2a",
    "#9a4a72",
    "#6a6a6a",
    "#3a3a3a",
  ];

  function getPanel(el) {
    return el?.closest?.(".timeline-panel") || document.querySelector(".timeline-panel");
  }

  function getHeaders(panel) {
    const root = panel?.querySelector(".track-headers");
    return root ? Array.from(root.querySelectorAll(":scope > .track-header")) : [];
  }

  function getLanes(panel) {
    const inner = panel?.querySelector(".tracks-inner");
    return inner ? Array.from(inner.querySelectorAll(":scope > .track-lane")) : [];
  }

  function headerKind(header) {
    if (!header) return null;
    if (header.classList.contains("track-header--video")) return "video";
    if (header.classList.contains("track-header--audio")) return "audio";
    return null;
  }

  function renumberTracks(panel) {
    if (window.VegasTimelineMove?.renumberTracks) {
      window.VegasTimelineMove.renumberTracks(panel);
      return;
    }
    getHeaders(panel).forEach((h, i) => {
      const num = h.querySelector(".track-num");
      if (num) num.textContent = String(i + 1);
    });
  }

  function resolveHeader(target) {
    if (!target) return null;
    if (target.classList?.contains("track-header")) return target;
    const fromHeader = target.closest?.(".track-header");
    if (fromHeader) return fromHeader;
    const lane = target.closest?.(".track-lane");
    if (lane) {
      const panel = getPanel(lane);
      const lanes = getLanes(panel);
      const headers = getHeaders(panel);
      const idx = lanes.indexOf(lane);
      if (idx >= 0) return headers[idx] || null;
    }
    return null;
  }

  function pairForHeader(panel, header) {
    const headers = getHeaders(panel);
    const lanes = getLanes(panel);
    const idx = headers.indexOf(header);
    if (idx < 0) return null;
    return { header, lane: lanes[idx] || null, idx };
  }

  function resetHeaderChrome(header) {
    header.querySelectorAll(".ms-btn.is-active").forEach((b) => b.classList.remove("is-active"));
    header.querySelectorAll(".track-mini-btn--rec.is-armed, .track-mini-btn.is-armed").forEach((b) => {
      b.classList.remove("is-armed");
    });
    const rail = header.querySelector(".track-header__rail");
    if (rail) rail.style.background = "";
    delete header.dataset.trackColor;
    header.classList.remove("is-renaming", "is-dragging-track", "is-drop-target");
    const kind = headerKind(header);
    const fallback = kind === "audio" ? "Audio" : "Video";
    header.dataset.trackName = fallback;
    ensureNameChrome(header);
    const label = header.querySelector(".track-header__name");
    const input = header.querySelector(".track-header__name-input");
    if (label) {
      label.hidden = false;
      label.textContent = fallback;
    }
    if (input) {
      input.hidden = true;
      input.value = fallback;
    }
    header.title = fallback;
  }

  function makeEmptyLane(kind) {
    const lane = document.createElement("div");
    lane.className = "track-lane track-lane--" + kind;
    lane.dataset.context = kind + "-track-empty";
    return lane;
  }

  function makeHeader(kind, template) {
    let header;
    if (template && headerKind(template) === kind) {
      header = template.cloneNode(true);
    } else {
      header = document.createElement("div");
      header.className = "track-header track-header--" + kind;
      header.dataset.context = kind + "-track-header";
      header.dataset.tlUpgraded = "1";
      // Minimal chrome if no template (empty project) — clone from any existing or build shell.
      const any = document.querySelector(".track-header--" + kind);
      if (any) {
        header = any.cloneNode(true);
        header.className = "track-header track-header--" + kind;
        header.dataset.context = kind + "-track-header";
      } else {
        header.innerHTML =
          '<div class="track-header__rail"><span class="track-num">1</span></div>' +
          '<div class="track-header__body">' +
          '<div class="track-header__ms">' +
          '<button class="ms-btn" type="button" title="Mute">M</button>' +
          '<button class="ms-btn" type="button" title="Solo">S</button>' +
          "</div></div>";
      }
    }
    resetHeaderChrome(header);
    header.classList.remove("is-dragging-track", "is-drop-target");
    return header;
  }

  function insertTrack(panel, kind, afterHeader, opts) {
    if (!panel) return null;
    const headersRoot = panel.querySelector(".track-headers");
    const inner = panel.querySelector(".tracks-inner");
    if (!headersRoot || !inner) return null;

    const headers = getHeaders(panel);
    const lanes = getLanes(panel);
    const template =
      headers.find((h) => headerKind(h) === kind) ||
      document.querySelector(".track-header--" + kind) ||
      afterHeader;

    const newHeader = makeHeader(kind, template);
    const newLane = makeEmptyLane(kind);
    delete newHeader.dataset.trackHBase;
    newHeader.querySelector(":scope > .track-header__h-resize")?.remove();

    if (afterHeader && headers.includes(afterHeader)) {
      const idx = headers.indexOf(afterHeader);
      afterHeader.after(newHeader);
      if (lanes[idx]) lanes[idx].after(newLane);
      else inner.appendChild(newLane);
    } else {
      headersRoot.appendChild(newHeader);
      const lastLane = lanes[lanes.length - 1];
      if (lastLane) lastLane.after(newLane);
      else inner.appendChild(newLane);
    }

    renumberTracks(panel);
    syncMuteSolo(panel);
    window.VegasTimelineResize?.ensureTrackHeightGrips?.(panel);
    window.VegasTimelineResize?.applyTrackHeights?.(panel);
    if (!opts || opts.rename !== false) {
      requestAnimationFrame(() => beginRenameTrack(newHeader));
    }
    return newHeader;
  }

  function duplicateTrack(panel, header) {
    const pair = pairForHeader(panel, header);
    if (!pair || !pair.lane) return null;
    const newHeader = header.cloneNode(true);
    newHeader.classList.remove("is-dragging-track", "is-drop-target");
    const newLane = pair.lane.cloneNode(true);
    newLane.querySelectorAll(".event.is-selected").forEach((el) => el.classList.remove("is-selected"));
    pair.header.after(newHeader);
    pair.lane.after(newLane);
    renumberTracks(panel);
    window.VegasTimelineChrome?.syncCrossfadeZones?.(newLane);
    window.VegasTimelineResize?.ensureTrackHeightGrips?.(panel);
    window.VegasTimelineResize?.applyTrackHeights?.(panel);
    syncMuteSolo(panel);
    return newHeader;
  }

  function deleteTrack(panel, header) {
    const pair = pairForHeader(panel, header);
    if (!pair) return;
    pair.header.remove();
    pair.lane?.remove();
    renumberTracks(panel);
    const area = panel.querySelector(".tracks-area");
    window.VegasTimelineChrome?.syncCrossfadeZones?.(area || panel);
    window.VegasTimelineSelect?.syncSelectionChrome?.();
  }

  function ensureNameChrome(header) {
    if (!header) return null;
    let wrap = header.querySelector(".track-header__name-wrap");
    const body = header.querySelector(".track-header__body");
    if (!body) return null;
    if (!wrap) {
      wrap = document.createElement("div");
      wrap.className = "track-header__name-wrap";
      wrap.innerHTML =
        '<span class="track-header__name" title="Double-click to rename"></span>' +
        '<input class="track-header__name-input" type="text" spellcheck="false" maxlength="64" aria-label="Track name" hidden />';
      const ms = body.querySelector(".track-header__ms");
      if (ms && ms.nextSibling) body.insertBefore(wrap, ms.nextSibling);
      else body.insertBefore(wrap, body.firstChild);
    }
    const label = wrap.querySelector(".track-header__name");
    const kind = headerKind(header);
    const fallback = kind === "audio" ? "Audio" : "Video";
    const name = header.dataset.trackName || label?.textContent?.trim() || fallback;
    header.dataset.trackName = name;
    if (label && !label.textContent.trim()) label.textContent = name;
    return wrap;
  }

  function setTrackName(header, name) {
    if (!header) return;
    const wrap = ensureNameChrome(header);
    const label = wrap?.querySelector(".track-header__name");
    const kind = headerKind(header);
    const fallback = kind === "audio" ? "Audio" : "Video";
    const next = (name || "").trim() || fallback;
    header.dataset.trackName = next;
    header.title = next;
    if (label) label.textContent = next;
  }

  function endRenameTrack(header, commit) {
    if (!header) return;
    const wrap = header.querySelector(".track-header__name-wrap");
    const label = wrap?.querySelector(".track-header__name");
    const input = wrap?.querySelector(".track-header__name-input");
    if (!wrap || !label || !input || input.hidden) return;
    const prev = header.dataset.trackName || label.textContent.trim();
    if (commit) setTrackName(header, input.value);
    else {
      label.textContent = prev;
      input.value = prev;
    }
    input.hidden = true;
    label.hidden = false;
    wrap.classList.remove("is-editing");
    header.classList.remove("is-renaming");
  }

  function beginRenameTrack(header) {
    if (!header) return;
    endRenameTrack(document.querySelector(".track-header.is-renaming"), true);
    const wrap = ensureNameChrome(header);
    const label = wrap?.querySelector(".track-header__name");
    const input = wrap?.querySelector(".track-header__name-input");
    if (!wrap || !label || !input) return;

    const current =
      header.dataset.trackName ||
      label.textContent.trim() ||
      (headerKind(header) === "audio" ? "Audio" : "Video");
    header.dataset.trackName = current;
    input.value = current;
    label.hidden = true;
    input.hidden = false;
    wrap.classList.add("is-editing");
    header.classList.add("is-renaming");

    requestAnimationFrame(() => {
      input.focus();
      input.select();
    });
  }

  function renameTrack(header) {
    beginRenameTrack(header);
  }

  function initTrackRename() {
    document.addEventListener("dblclick", (e) => {
      const nameEl = e.target.closest(".track-header__name, .track-header__name-wrap");
      if (!nameEl) return;
      if (e.target.closest("button, .track-slider, .ms-btn")) return;
      const header = nameEl.closest(".track-header");
      if (!header) return;
      e.preventDefault();
      e.stopPropagation();
      beginRenameTrack(header);
    });

    document.addEventListener(
      "keydown",
      (e) => {
        const input = e.target.closest?.(".track-header__name-input");
        if (!input) return;
        const header = input.closest(".track-header");
        if (e.key === "Enter") {
          e.preventDefault();
          endRenameTrack(header, true);
        } else if (e.key === "Escape") {
          e.preventDefault();
          endRenameTrack(header, false);
        }
      },
      true
    );

    document.addEventListener(
      "focusout",
      (e) => {
        const input = e.target.closest?.(".track-header__name-input");
        if (!input || input.hidden) return;
        const header = input.closest(".track-header");
        // Defer so Enter handler can run first
        setTimeout(() => {
          if (header && !input.hidden && document.activeElement !== input) {
            endRenameTrack(header, true);
          }
        }, 0);
      },
      true
    );
  }

  function applyTrackColor(header, color) {
    if (!header) return;
    const rail = header.querySelector(".track-header__rail");
    if (!rail) return;
    if (!color) {
      rail.style.background = "";
      delete header.dataset.trackColor;
      return;
    }
    header.dataset.trackColor = color;
    rail.style.background = "linear-gradient(180deg, color-mix(in srgb, " + color + " 75%, #fff), " + color + ")";
  }

  function closeColorPicker() {
    document.getElementById("track-color-picker")?.remove();
  }

  function openColorPicker(header, clientX, clientY) {
    closeColorPicker();
    const pop = document.createElement("div");
    pop.id = "track-color-picker";
    pop.className = "track-color-picker";
    pop.innerHTML =
      '<div class="track-color-picker__title">Track Color</div>' +
      '<div class="track-color-picker__swatches">' +
      TRACK_COLORS.map(
        (c) =>
          '<button type="button" class="track-color-picker__swatch' +
          (header.dataset.trackColor === c ? " is-active" : "") +
          '" data-color="' +
          c +
          '" style="background:' +
          c +
          '" title="' +
          c +
          '"></button>'
      ).join("") +
      "</div>" +
      '<button type="button" class="track-color-picker__reset">Default</button>';

    document.body.appendChild(pop);
    const pad = 8;
    let left = clientX;
    let top = clientY;
    const w = pop.offsetWidth || 180;
    const h = pop.offsetHeight || 120;
    if (left + w > window.innerWidth - pad) left = window.innerWidth - w - pad;
    if (top + h > window.innerHeight - pad) top = window.innerHeight - h - pad;
    pop.style.left = Math.max(pad, left) + "px";
    pop.style.top = Math.max(pad, top) + "px";

    pop.addEventListener("click", (e) => {
      e.stopPropagation();
      const sw = e.target.closest(".track-color-picker__swatch");
      if (sw) {
        applyTrackColor(header, sw.getAttribute("data-color"));
        closeColorPicker();
        return;
      }
      if (e.target.closest(".track-color-picker__reset")) {
        applyTrackColor(header, null);
        closeColorPicker();
      }
    });
  }

  function toggleMs(header, which) {
    const btn = header.querySelector('.ms-btn[title="' + which + '"]');
    if (!btn) return;
    btn.classList.toggle("is-active");
    const panel = getPanel(header);
    if (panel) syncMuteSolo(panel);
  }

  function isMuted(header) {
    return !!header.querySelector('.ms-btn[title="Mute"].is-active');
  }

  function isSoloed(header) {
    return !!header.querySelector('.ms-btn[title="Solo"].is-active');
  }

  /** Vegas: if any Solo is on, non-solo tracks are effectively muted. */
  function syncMuteSolo(panel) {
    if (!panel) return;
    const headers = getHeaders(panel);
    const lanes = getLanes(panel);
    const anySolo = headers.some(isSoloed);

    headers.forEach((header, i) => {
      const muted = isMuted(header);
      const soloed = isSoloed(header);
      const effective = muted || (anySolo && !soloed);
      header.classList.toggle("is-muted", muted);
      header.classList.toggle("is-soloed", soloed);
      header.classList.toggle("is-effectively-muted", effective);
      const lane = lanes[i];
      if (lane) {
        lane.classList.toggle("is-muted", muted);
        lane.classList.toggle("is-soloed", soloed);
        lane.classList.toggle("is-effectively-muted", effective);
      }
    });
  }

  function initMuteSoloButtons() {
    document.addEventListener(
      "click",
      (e) => {
        const btn = e.target.closest('.track-header .ms-btn[title="Mute"], .track-header .ms-btn[title="Solo"]');
        if (!btn) return;
        e.preventDefault();
        e.stopPropagation();
        btn.classList.toggle("is-active");
        const header = btn.closest(".track-header");
        const panel = getPanel(header);
        if (panel) syncMuteSolo(panel);
      },
      true
    );

    document.querySelectorAll(".timeline-panel").forEach(syncMuteSolo);
  }

  function handleTrackAction(action, header, target, anchorEvent) {
    const panel = getPanel(header || target);
    if (!panel) return;

    if (action === "track-insert-video") {
      insertTrack(panel, "video", header);
      return;
    }
    if (action === "track-insert-audio") {
      insertTrack(panel, "audio", header);
      return;
    }

    if (!header) return;

    if (action === "track-duplicate") {
      duplicateTrack(panel, header);
      syncMuteSolo(panel);
    } else if (action === "track-delete") {
      deleteTrack(panel, header);
      syncMuteSolo(panel);
    } else if (action === "track-rename") renameTrack(header);
    else if (action === "track-color") {
      const x = anchorEvent?.clientX ?? header.getBoundingClientRect().left;
      const y = anchorEvent?.clientY ?? header.getBoundingClientRect().bottom + 4;
      openColorPicker(header, x, y);
    } else if (action === "track-mute") toggleMs(header, "Mute");
    else if (action === "track-solo") toggleMs(header, "Solo");
    else if (action === "track-record") toggleRecord(header);
    else if (action === "track-fx") openTrackFx(header);
  }

  function openTrackFx(header) {
    if (!header) return;
    const kind = headerKind(header);
    if (kind === "audio") {
      window.VegasAudioFx?.openTrackFx?.(header);
    } else {
      window.VegasPanCrop?.openTrackFx?.(header);
    }
  }

  function initTrackFxButtons() {
    document.addEventListener(
      "click",
      (e) => {
        const btn = e.target.closest('.track-mini-btn[title="Track FX"]');
        if (!btn) return;
        const header = btn.closest(".track-header");
        if (!header) return;
        e.preventDefault();
        e.stopPropagation();
        openTrackFx(header);
      },
      true
    );
  }

  function toggleRecord(header) {
    const btn =
      header.querySelector(".track-mini-btn--rec") ||
      header.querySelector('.track-mini-btn[title="Record Arm"]');
    if (btn) btn.classList.toggle("is-armed");
  }

  let lastContextPoint = { x: 0, y: 0 };

  document.addEventListener(
    "contextmenu",
    (e) => {
      lastContextPoint = { x: e.clientX, y: e.clientY };
    },
    true
  );

  document.addEventListener("vegas:context-action", (e) => {
    const { type, action, label, target } = e.detail || {};
    const act =
      action ||
      ({
        "Insert Video Track": "track-insert-video",
        "Insert Audio Track": "track-insert-audio",
        "Insert Adjustment Track": "track-insert-video",
        "Duplicate Track": "track-duplicate",
        "Delete Track": "track-delete",
        "Track Name...": "track-rename",
        "Track Color...": "track-color",
        Mute: "track-mute",
        Solo: "track-solo",
        "Record Arm": "track-record",
        "Track FX...": "track-fx",
        "Track Motion...": "track-motion",
      }[label] || "");

    if (!act.startsWith("track-")) return;

    const header = resolveHeader(target);
    if (
      type !== "video-track-header" &&
      type !== "audio-track-header" &&
      type !== "timeline-empty" &&
      type !== "video-track-empty" &&
      type !== "audio-track-empty"
    ) {
      // Still allow if action is track-* and we have a panel
      if (!header && type !== "timeline-empty") return;
    }

    handleTrackAction(act, header, target, lastContextPoint);
  });

  document.addEventListener("click", (e) => {
    if (!e.target.closest("#track-color-picker")) closeColorPicker();

    const more = e.target.closest('.track-mini-btn[title="More"]');
    if (!more) return;
    const header = more.closest(".track-header");
    if (!header || !window.VegasMenus?.openContextMenu) return;
    e.preventDefault();
    e.stopPropagation();
    const kind = headerKind(header);
    const menuType = kind === "audio" ? "audio-track-header" : "video-track-header";
    const rect = more.getBoundingClientRect();
    lastContextPoint = { x: rect.left, y: rect.bottom + 2 };
    window.VegasMenus.closeAllDropdowns?.();
    window.VegasMenus.openContextMenu(menuType, rect.left, rect.bottom + 2, header);
  }, true);

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") closeColorPicker();
  });

  document.addEventListener("DOMContentLoaded", () => {
    initMuteSoloButtons();
    initTrackRename();
    initTrackFxButtons();
    document.querySelectorAll(".track-header").forEach((h) => {
      ensureNameChrome(h);
      if (!h.dataset.trackName) {
        const label = h.querySelector(".track-header__name");
        const kind = headerKind(h);
        h.dataset.trackName =
          label?.textContent?.trim() || (kind === "audio" ? "Audio" : "Video");
      }
      window.VegasTimelineChrome?.syncEventFxBadge?.(h);
    });
  });

  window.VegasTimelineTracks = {
    insertTrack,
    duplicateTrack,
    deleteTrack,
    renameTrack,
    beginRenameTrack,
    setTrackName,
    applyTrackColor,
    syncMuteSolo,
    openTrackFx,
  };
})();
