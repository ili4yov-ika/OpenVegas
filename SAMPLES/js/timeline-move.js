/**
 * Drag timeline events in time and between same-type tracks
 * (video ↔ video, audio ↔ audio).
 */
(function () {
  const DRAG_THRESHOLD = 4;

  function eventKind(ev) {
    if (ev.classList.contains("event--video")) return "video";
    if (ev.classList.contains("event--audio")) return "audio";
    return null;
  }

  function laneKind(lane) {
    if (!lane) return null;
    if (lane.classList.contains("track-lane--video")) return "video";
    if (lane.classList.contains("track-lane--audio")) return "audio";
    return null;
  }

  function parseLeft(el) {
    return parseFloat(el.style.left) || 0;
  }

  function setLeft(el, px) {
    el.style.left = Math.max(0, Math.round(px)) + "px";
  }

  function compatibleLanes(inner, kind) {
    return Array.from(inner.querySelectorAll(".track-lane--" + kind));
  }

  function laneAtClientY(lanes, clientY) {
    if (!lanes.length) return null;
    let best = lanes[0];
    let bestDist = Infinity;
    lanes.forEach((lane) => {
      const r = lane.getBoundingClientRect();
      const mid = (r.top + r.bottom) / 2;
      const dist =
        clientY >= r.top && clientY <= r.bottom ? 0 : Math.abs(clientY - mid);
      if (dist < bestDist) {
        bestDist = dist;
        best = lane;
      }
    });
    return best;
  }

  /** Same-kind lane hit-test, or empty timeline space → create a new track. */
  function resolveEventDrop(panel, kind, clientY, sameKindLanes) {
    for (let i = 0; i < sameKindLanes.length; i++) {
      const lane = sameKindLanes[i];
      const r = lane.getBoundingClientRect();
      if (clientY >= r.top && clientY <= r.bottom) {
        return { mode: "lane", lane };
      }
    }

    const area = panel.querySelector(".tracks-area");
    if (!area) {
      return { mode: "lane", lane: laneAtClientY(sameKindLanes, clientY) };
    }
    const ar = area.getBoundingClientRect();
    if (clientY < ar.top || clientY > ar.bottom) {
      return { mode: "lane", lane: laneAtClientY(sameKindLanes, clientY) };
    }

    const allLanes = getLanes(panel);
    for (let i = 0; i < allLanes.length; i++) {
      const r = allLanes[i].getBoundingClientRect();
      if (clientY >= r.top && clientY <= r.bottom) {
        return { mode: "lane", lane: laneAtClientY(sameKindLanes, clientY) };
      }
    }

    return { mode: "new", kind };
  }

  function createTrackForDrop(panel, kind) {
    const headers = getHeaders(panel);
    const after = headers[headers.length - 1] || null;
    const newHeader =
      window.VegasTimelineTracks?.insertTrack?.(panel, kind, after, { rename: false }) || null;
    if (!newHeader) return null;
    const idx = getHeaders(panel).indexOf(newHeader);
    return getLanes(panel)[idx] || null;
  }

  function renumberTracks(panel) {
    const headers = panel.querySelector(".track-headers");
    if (!headers) return;
    headers.querySelectorAll(".track-num").forEach((el, i) => {
      el.textContent = String(i + 1);
    });
  }

  /** Ensure at least two lanes of each existing media type so vertical moves work. */
  function ensureSpareTracks(panel) {
    const headers = panel.querySelector(".track-headers");
    const inner = panel.querySelector(".tracks-inner");
    if (!headers || !inner) return;

    function ensure(kind) {
      const laneSel = ".track-lane--" + kind;
      const headerSel = ".track-header--" + kind;
      const lanes = Array.from(inner.querySelectorAll(laneSel));
      if (lanes.length !== 1) return;

      const srcLane = lanes[0];
      const headerList = Array.from(headers.querySelectorAll(headerSel));
      const srcHeader = headerList[headerList.length - 1];
      if (!srcHeader) return;

      const newLane = document.createElement("div");
      newLane.className = "track-lane track-lane--" + kind;
      newLane.dataset.context = kind + "-track-empty";
      srcLane.after(newLane);

      const newHeader = srcHeader.cloneNode(true);
      newHeader.classList.remove("is-active", "is-renaming", "is-dragging-track", "is-drop-target");
      const fallback = kind === "video" ? "Video" : "Audio";
      newHeader.dataset.trackName = fallback;
      const label = newHeader.querySelector(".track-header__name");
      const input = newHeader.querySelector(".track-header__name-input");
      if (label) {
        label.hidden = false;
        label.textContent = fallback;
      }
      if (input) {
        input.hidden = true;
        input.value = fallback;
      }
      newHeader.querySelectorAll(".ms-btn.is-active").forEach((b) => b.classList.remove("is-active"));
      srcHeader.after(newHeader);
    }

    ensure("video");
    ensure("audio");
    renumberTracks(panel);
  }

  function clearSelection(area) {
    area.querySelectorAll(".event.is-selected").forEach((el) => el.classList.remove("is-selected"));
  }

  function syncAfterMove(area) {
    const scope = area || document;
    window.VegasTimelineChrome?.syncCrossfadeZones?.(scope);
    window.VegasTimelineSelect?.syncSelectionChrome?.();
  }

  function clampFadeToWidth(ev, side, width) {
    if (side === "r") {
      const fadeOut = parseFloat(ev.dataset.fadeOut) || 0;
      if (fadeOut > width) {
        ev.dataset.fadeOut = String(width);
        const outEl = ev.querySelector('.event__fade[data-fade-side="out"]');
        if (outEl) {
          outEl.style.width = width + "px";
          outEl.classList.toggle("has-fade", width > 0);
        }
      }
      return;
    }
    const fadeIn = parseFloat(ev.dataset.fadeIn) || 0;
    if (fadeIn > width) {
      ev.dataset.fadeIn = String(width);
      const inEl = ev.querySelector('.event__fade[data-fade-side="in"]');
      if (inEl) {
        inEl.style.width = width + "px";
        inEl.classList.toggle("has-fade", width > 0);
      }
    }
  }

  /** Peers that should share duration trim (AV group), unless Ignore Event Grouping. */
  function trimPeersFor(eventEl, area) {
    if (!eventEl) return [];
    if (window.VegasEventGroup?.isIgnoreGrouping?.(area)) {
      return eventEl.classList.contains("is-event-locked") ? [] : [eventEl];
    }
    const list = window.VegasEventGroup?.expandToGroups
      ? window.VegasEventGroup.expandToGroups([eventEl], area)
      : [eventEl];
    return list.filter((ev) => !ev.classList.contains("is-event-locked"));
  }

  function initTrimResize() {
    document.addEventListener(
      "pointerdown",
      (e) => {
        if (e.button !== 0) return;
        const trim = e.target.closest(".event__trim");
        if (!trim) return;
        const eventEl = trim.closest(".event");
        if (!eventEl || eventEl.classList.contains("is-event-locked")) return;

        const area = eventEl.closest(".tracks-area");
        const side = trim.classList.contains("event__trim--l") ? "l" : "r";
        e.preventDefault();
        e.stopPropagation();

        window.VegasTimelineSelect?.applySelectionModifiers?.(eventEl, e);

        const startX = e.clientX;
        const minW = 16;
        // Vegas: edge-trim applies the same delta to all group members (video ↔ audio).
        const peers = trimPeersFor(eventEl, area);
        if (!peers.length) return;
        const origins = peers.map((ev) => ({
          ev,
          left: parseLeft(ev),
          width: Math.max(1, parseFloat(ev.style.width) || ev.offsetWidth || 1),
        }));

        document.body.classList.add("is-trimming-event");
        peers.forEach((ev) => ev.classList.add("is-trimming"));

        function onMove(ev) {
          const dx = ev.clientX - startX;
          const lanes = new Set();
          origins.forEach(({ ev: peer, left: originLeft, width: originWidth }) => {
            if (side === "r") {
              const w = Math.max(minW, Math.round(originWidth + dx));
              peer.style.width = w + "px";
              clampFadeToWidth(peer, "r", w);
            } else {
              let newLeft = Math.round(originLeft + dx);
              let newW = Math.round(originWidth - dx);
              if (newW < minW) {
                newW = minW;
                newLeft = Math.round(originLeft + originWidth - minW);
              }
              if (newLeft < 0) {
                newW = Math.max(minW, newW + newLeft);
                newLeft = 0;
              }
              peer.style.left = newLeft + "px";
              peer.style.width = newW + "px";
              clampFadeToWidth(peer, "l", newW);
            }
            const lane = peer.closest(".track-lane");
            if (lane) lanes.add(lane);
          });
          lanes.forEach((lane) => window.VegasTimelineChrome?.syncCrossfadeZones?.(lane));
          window.VegasTimelineSelect?.syncSelectionChrome?.();
        }

        function onUp() {
          document.body.classList.remove("is-trimming-event");
          peers.forEach((ev) => ev.classList.remove("is-trimming"));
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
          window.removeEventListener("pointercancel", onUp);
          syncAfterMove(area);
        }

        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
        window.addEventListener("pointercancel", onUp);
      },
      true
    );
  }

  function initDrag() {
    document.addEventListener(
      "pointerdown",
      (e) => {
        if (e.button !== 0) return;
        if (e.target.closest(".event-btn, .event__fade, .event__fade-handle, .event__trim, .playhead")) {
          return;
        }

        const eventEl = e.target.closest(".event");
        if (!eventEl) return;

        const area = eventEl.closest(".tracks-area");
        const additive = e.ctrlKey || e.metaKey;
        const range = e.shiftKey;

        if (eventEl.classList.contains("is-event-locked")) {
          if (area) {
            window.VegasTimelineSelect?.applySelectionModifiers?.(eventEl, e);
            syncAfterMove(area);
          }
          return;
        }

        const kind = eventKind(eventEl);
        const lane = eventEl.closest(".track-lane");
        const inner = eventEl.closest(".tracks-inner");
        const panel = eventEl.closest(".timeline-panel");
        if (!kind || !lane || !inner || !area || !panel) return;

        e.preventDefault();

        // Selection: Ctrl toggle, Shift range, plain click replaces unless already in multi-sel.
        if (range || additive) {
          window.VegasTimelineSelect?.applySelectionModifiers?.(eventEl, e);
          // Ctrl toggle-off: don't start a move.
          if (additive && !eventEl.classList.contains("is-selected")) {
            syncAfterMove(area);
            return;
          }
        } else if (!eventEl.classList.contains("is-selected")) {
          window.VegasTimelineSelect?.selectEvent?.(eventEl);
        } else {
          window.VegasTimelineSelect?.setAnchor?.(eventEl);
        }

        let movers;
        let timePeers = [];
        // All selected events participate (any kind); lane changes only for drag kind.
        let seeds = Array.from(area.querySelectorAll(".event.is-selected"));
        if (!seeds.includes(eventEl)) seeds.push(eventEl);

        if (window.VegasEventGroup?.collectMoveSet) {
          const set = window.VegasEventGroup.collectMoveSet(seeds, kind, area);
          movers = set.movers;
          timePeers = set.timePeers;
          if (area.dataset.ignoreGrouping !== "1" && set.all) {
            set.all.forEach((ev) => ev.classList.add("is-selected"));
          }
        } else {
          movers = seeds.filter(
            (ev) => eventKind(ev) === kind && !ev.classList.contains("is-event-locked")
          );
          timePeers = seeds.filter(
            (ev) => eventKind(ev) !== kind && !ev.classList.contains("is-event-locked")
          );
          if (!movers.length) movers.push(eventEl);
        }

        if (!movers.length) return;

        const startX = e.clientX;
        const startY = e.clientY;
        const origins = movers.map((ev) => ({
          el: ev,
          left: parseLeft(ev),
          lane: ev.closest(".track-lane"),
          kind: "move",
        }));
        timePeers.forEach((ev) => {
          origins.push({
            el: ev,
            left: parseLeft(ev),
            lane: ev.closest(".track-lane"),
            kind: "time",
          });
        });

        let dragging = false;
        let lastTarget = lane;
        let lanes = compatibleLanes(inner, kind);
        const filler = inner.querySelector(".tracks-filler");

        function clearDropHints() {
          lanes.forEach((l) => l.classList.remove("is-drop-target"));
          filler?.classList.remove("is-drop-new-track");
          area.classList.remove("is-drop-new-track");
        }

        function onMove(ev) {
          const dx = ev.clientX - startX;
          const dy = ev.clientY - startY;
          if (!dragging) {
            if (Math.abs(dx) < DRAG_THRESHOLD && Math.abs(dy) < DRAG_THRESHOLD) return;
            dragging = true;
            document.body.classList.add("is-dragging-event");
            origins.forEach((o) => o.el.classList.add("is-dragging"));
          }

          // All group / multi-selected members shift in time; only primary-kind change lanes.
          origins.forEach((o) => setLeft(o.el, o.left + dx));

          lanes = compatibleLanes(inner, kind);
          const drop = resolveEventDrop(panel, kind, ev.clientY, lanes);
          clearDropHints();

          if (drop.mode === "new") {
            area.classList.add("is-drop-new-track");
            filler?.classList.add("is-drop-new-track");
          } else if (drop.lane) {
            if (drop.lane !== lastTarget) {
              lastTarget = drop.lane;
              movers.forEach((m) => {
                if (m.parentElement !== drop.lane) drop.lane.appendChild(m);
              });
            }
            drop.lane.classList.add("is-drop-target");
          }

          const touched = new Set(origins.map((o) => o.lane).filter(Boolean));
          if (lastTarget) touched.add(lastTarget);
          timePeers.forEach((p) => {
            const l = p.closest(".track-lane");
            if (l) touched.add(l);
          });
          touched.forEach((l) => window.VegasTimelineChrome?.syncCrossfadeZones?.(l));
        }

        function onUp(ev) {
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
          window.removeEventListener("pointercancel", onUp);
          document.body.classList.remove("is-dragging-event");
          origins.forEach((o) => o.el.classList.remove("is-dragging"));

          if (dragging) {
            lanes = compatibleLanes(inner, kind);
            const drop = resolveEventDrop(panel, kind, ev?.clientY ?? startY, lanes);
            clearDropHints();

            if (drop.mode === "new") {
              const newLane = createTrackForDrop(panel, kind);
              if (newLane) {
                movers.forEach((m) => newLane.appendChild(m));
                lastTarget = newLane;
                window.VegasTimelineTracks?.syncMuteSolo?.(panel);
              }
            } else if (drop.lane) {
              movers.forEach((m) => {
                if (m.parentElement !== drop.lane) drop.lane.appendChild(m);
              });
              lastTarget = drop.lane;
            }

            const touched = new Set(origins.map((o) => o.lane).filter(Boolean));
            origins.forEach((o) => {
              const l = o.el.closest(".track-lane");
              if (l) touched.add(l);
            });
            touched.forEach((l) => window.VegasTimelineChrome?.syncCrossfadeZones?.(l));
            window.VegasTimelineSelect?.syncSelectionChrome?.();
          } else {
            clearDropHints();
          }
        }

        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
        window.addEventListener("pointercancel", onUp);
        return;
      },
      true
    );
  }

  function getHeaders(panel) {
    const root = panel.querySelector(".track-headers");
    return root ? Array.from(root.querySelectorAll(":scope > .track-header")) : [];
  }

  function getLanes(panel) {
    const inner = panel.querySelector(".tracks-inner");
    return inner ? Array.from(inner.querySelectorAll(":scope > .track-lane")) : [];
  }

  function headerKind(header) {
    if (header.classList.contains("track-header--video")) return "video";
    if (header.classList.contains("track-header--audio")) return "audio";
    return null;
  }

  function placeTrackAt(panel, fromIdx, toIdx) {
    if (fromIdx === toIdx || fromIdx < 0 || toIdx < 0) return false;
    const headers = getHeaders(panel);
    const lanes = getLanes(panel);
    if (fromIdx >= headers.length || toIdx >= headers.length) return false;
    if (fromIdx >= lanes.length || toIdx >= lanes.length) return false;

    const h = headers[fromIdx];
    const l = lanes[fromIdx];
    if (toIdx > fromIdx) {
      headers[toIdx].after(h);
      lanes[toIdx].after(l);
    } else {
      headers[toIdx].before(h);
      lanes[toIdx].before(l);
    }
    renumberTracks(panel);
    return true;
  }

  function headerAtClientY(headers, clientY) {
    if (!headers.length) return null;
    let best = headers[0];
    let bestDist = Infinity;
    headers.forEach((h) => {
      const r = h.getBoundingClientRect();
      const mid = (r.top + r.bottom) / 2;
      const dist = clientY >= r.top && clientY <= r.bottom ? 0 : Math.abs(clientY - mid);
      if (dist < bestDist) {
        bestDist = dist;
        best = h;
      }
    });
    return best;
  }

  function initTrackReorder() {
    document.addEventListener(
      "pointerdown",
      (e) => {
        if (e.button !== 0) return;
        // Only empty chrome — not M/S, fx, sliders, record, menus.
        if (
          e.target.closest(
            "button, input, select, textarea, .ms-btn, .track-mini-btn, .track-slider, .event, .track-header__name, .track-header__name-wrap, .track-header__name-input, .track-header__h-resize"
          )
        ) {
          return;
        }

        const header = e.target.closest(".track-header");
        if (!header) return;

        const panel = header.closest(".timeline-panel");
        if (!panel) return;

        const kind = headerKind(header);
        if (!kind) return;

        e.preventDefault();
        e.stopPropagation();

        const startY = e.clientY;
        let dragging = false;

        function onMove(ev) {
          const dy = ev.clientY - startY;
          if (!dragging) {
            if (Math.abs(dy) < DRAG_THRESHOLD) return;
            dragging = true;
            document.body.classList.add("is-dragging-track");
            header.classList.add("is-dragging-track");
          }

          // Any track order (audio above video is allowed), like Vegas.
          const headers = getHeaders(panel);
          const over = headerAtClientY(headers, ev.clientY);
          headers.forEach((h) => h.classList.toggle("is-drop-target", h === over && h !== header));
          if (!over || over === header) return;

          const fromIdx = headers.indexOf(header);
          const toIdx = headers.indexOf(over);
          if (fromIdx < 0 || toIdx < 0 || fromIdx === toIdx) return;
          placeTrackAt(panel, fromIdx, toIdx);
        }

        function onUp() {
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
          window.removeEventListener("pointercancel", onUp);
          document.body.classList.remove("is-dragging-track");
          header.classList.remove("is-dragging-track");
          getHeaders(panel).forEach((h) => h.classList.remove("is-drop-target"));
          if (dragging) {
            const area = panel.querySelector(".tracks-area");
            window.VegasTimelineChrome?.syncCrossfadeZones?.(area || panel);
            window.VegasTimelineSelect?.syncSelectionChrome?.();
          }
        }

        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
        window.addEventListener("pointercancel", onUp);
      },
      true
    );
  }

  function init() {
    document.querySelectorAll(".timeline-panel").forEach(ensureSpareTracks);
    initTrimResize();
    initDrag();
    initTrackReorder();
  }

  document.addEventListener("DOMContentLoaded", init);

  window.VegasTimelineMove = { ensureSpareTracks, placeTrackAt, renumberTracks };
})();
