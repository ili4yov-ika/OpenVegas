/**
 * Global editor hotkeys (layout-independent via KeyboardEvent.code).
 * Wires shortcuts shown in menus / context menus to existing module APIs.
 */
(function () {
  const ZOOM_STEP = 1.25;

  function isTypingTarget(t) {
    return !!t?.closest?.("input, textarea, select, [contenteditable=true]");
  }

  function modalBlocksHotkeys() {
    if (document.querySelector(".ep-backdrop.is-open")) return true;
    if (document.querySelector(".pp-backdrop.is-open")) return true;
    if (document.querySelector(".ra-backdrop.is-open")) return true;
    if (document.querySelector(".pref-backdrop.is-open")) return true;
    if (window.VegasTrimmer?.isOpen?.()) return true;
    if (document.querySelector(".fx-window.is-open, .pc-window.is-open, .afx-window.is-open")) return true;
    return false;
  }

  function panel() {
    return document.querySelector(".timeline-panel");
  }

  function area() {
    return document.querySelector(".tracks-area");
  }

  function selectedEvents() {
    const a = area();
    if (!a) return [];
    let list = Array.from(a.querySelectorAll(".event.is-selected"));
    if (window.VegasEventGroup?.expandToGroups) {
      list = window.VegasEventGroup.expandToGroups(list, a);
      list.forEach((ev) => ev.classList.add("is-selected"));
    }
    return list;
  }

  function toast(msg) {
    window.VegasEventActions?.toast?.(msg);
  }

  function runEventAction(action) {
    const list = selectedEvents();
    const seed = list[0] || area()?.querySelector(".event");
    if (!seed && action !== "event-paste") {
      toast?.("Nothing selected");
      return false;
    }
    if (window.VegasEventActions?.handleAction) {
      return window.VegasEventActions.handleAction(action, "", seed || area());
    }
    return false;
  }

  function insertTrack(kind) {
    const p = panel();
    if (!p || !window.VegasTimelineTracks?.insertTrack) {
      toast?.("Tracks API unavailable");
      return;
    }
    const headers = p.querySelectorAll(".track-headers > .track-header");
    const last = headers[headers.length - 1] || null;
    window.VegasTimelineTracks.insertTrack(p, kind, last, { rename: false });
    toast?.(kind === "video" ? "Video track inserted" : "Audio track inserted");
  }

  function zoomTime(dir) {
    const p = panel();
    if (!p) return;
    if (window.VegasTimelineScroll?.zoomTime) {
      window.VegasTimelineScroll.zoomTime(p, dir);
      return;
    }
    // Fallback: click chrome buttons
    const btn = p.querySelector(dir > 0 ? '[data-zoom="h-in"]' : '[data-zoom="h-out"]');
    btn?.click();
  }

  function onKeyDown(e) {
    if (e.repeat) return;
    if (isTypingTarget(e.target)) return;
    if (modalBlocksHotkeys()) return;

    // Trimmer owns its own keys while open
    if (window.VegasTrimmer?.isOpen?.()) return;

    const ctrl = e.ctrlKey || e.metaKey;
    const shift = e.shiftKey;
    const alt = e.altKey;
    const code = e.code;
    const a = area();
    const p = panel();

    // —— Global (selection optional) ——
    if (ctrl && shift && code === "KeyU") {
      e.preventDefault();
      const on = window.VegasEventGroup?.toggleIgnoreGrouping?.(a);
      toast?.(on ? "Ignore Event Grouping" : "Event Grouping enabled");
      return;
    }

    if (ctrl && shift && code === "KeyM") {
      e.preventDefault();
      window.VegasRenderAs?.open?.();
      return;
    }

    if (alt && code === "Enter") {
      e.preventDefault();
      window.VegasProjectProperties?.open?.();
      return;
    }

    if (ctrl && !shift && !alt && code === "KeyT") {
      e.preventDefault();
      insertTrack("audio");
      return;
    }
    if (ctrl && shift && !alt && code === "KeyT") {
      e.preventDefault();
      insertTrack("video");
      return;
    }

    if (ctrl && !alt && code === "KeyV") {
      e.preventDefault();
      if (shift) {
        const sel = selectedEvents();
        if (sel.length) runEventAction("event-paste-attrs");
        else window.VegasEventActions?.pasteEventsAtPlayhead?.(p, a);
      } else {
        window.VegasEventActions?.pasteEventsAtPlayhead?.(p, a);
      }
      return;
    }

    if (ctrl && !shift && !alt && code === "KeyA") {
      e.preventDefault();
      if (!a) return;
      a.querySelectorAll(".event").forEach((ev) => ev.classList.add("is-selected"));
      window.VegasTimelineSelect?.syncSelectionChrome?.();
      return;
    }

    // View → Zoom In/Out Time
    if (!ctrl && !alt && !shift && code === "ArrowUp") {
      e.preventDefault();
      zoomTime(1);
      return;
    }
    if (!ctrl && !alt && !shift && code === "ArrowDown") {
      e.preventDefault();
      zoomTime(-1);
      return;
    }

    // —— Need selection ——
    const selected = selectedEvents();

    if (ctrl && !shift && !alt && code === "KeyX") {
      if (!selected.length) return;
      e.preventDefault();
      window.VegasEventActions?.cutEvents?.(selected) || runEventAction("event-cut");
      return;
    }
    if (ctrl && !shift && !alt && code === "KeyC") {
      if (!selected.length) return;
      e.preventDefault();
      window.VegasEventActions?.copyEvents?.(selected) || runEventAction("event-copy");
      return;
    }

    if (!ctrl && !alt && (code === "Delete" || code === "Backspace")) {
      if (!selected.length) return;
      e.preventDefault();
      window.VegasEventActions?.deleteEvents?.(selected) || runEventAction("event-delete");
      return;
    }

    // Split — KeyS (works on RU layout; was broken with e.key === "s")
    if (!ctrl && !alt && !shift && code === "KeyS") {
      if (!selected.length) {
        toast?.("Select an event to split");
        return;
      }
      e.preventDefault();
      runEventAction("event-split");
      return;
    }

    if (!ctrl && !alt && !shift && code === "KeyG") {
      if (selected.length < 2) return;
      e.preventDefault();
      runEventAction("event-group");
      return;
    }

    if (!ctrl && !alt && !shift && code === "KeyU") {
      if (!selected.length) return;
      e.preventDefault();
      runEventAction("event-ungroup");
      return;
    }

    // Trim Start / End — Alt+[ / Alt+]
    if (alt && !ctrl && (code === "BracketLeft" || code === "Digit9")) {
      if (!selected.length) return;
      e.preventDefault();
      runEventAction("event-trim-start");
      return;
    }
    if (alt && !ctrl && (code === "BracketRight" || code === "Digit0")) {
      if (!selected.length) return;
      e.preventDefault();
      runEventAction("event-trim-end");
      return;
    }

    // Context shortcuts with implementations
    if (alt && !ctrl && !shift && code === "KeyC") {
      if (!selected.length) return;
      e.preventDefault();
      runEventAction("event-create-nested");
      return;
    }
    if (alt && !ctrl && !shift && code === "KeyN") {
      if (!selected.length) return;
      e.preventDefault();
      runEventAction("event-open-nested");
      return;
    }
    if (alt && !ctrl && !shift && code === "KeyM") {
      if (!selected.length) return;
      e.preventDefault();
      runEventAction("event-motion-tracking");
      return;
    }
  }

  document.addEventListener("keydown", onKeyDown, true);

  // Menu bar labels → same actions
  document.addEventListener("vegas:menu-action", (e) => {
    const label = (e.detail?.label || "").replace(/\s+/g, " ").trim();
    const action = e.detail?.action || "";
    if (/^Audio Track$/i.test(label) || action === "insert-audio-track") {
      insertTrack("audio");
      return;
    }
    if (/^Video Track$/i.test(label) || action === "insert-video-track") {
      insertTrack("video");
      return;
    }
    if (/^Zoom In Time$/i.test(label)) {
      zoomTime(1);
      return;
    }
    if (/^Zoom Out Time$/i.test(label)) {
      zoomTime(-1);
    }
  });

  window.VegasHotkeys = { zoomTime, insertTrack };
})();
