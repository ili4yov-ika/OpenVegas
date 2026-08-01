/**
 * Timeline event selection for Vegas Pro HTML mockups.
 * Ctrl/Meta+click — toggle; Shift+click — range; groups expand together.
 */
(function () {
  let selectionAnchor = null;

  function clearSelection(root) {
    (root || document).querySelectorAll(".event.is-selected").forEach((el) => {
      el.classList.remove("is-selected");
    });
  }

  function pxPerSec(area) {
    const panel = area?.closest?.(".timeline-panel");
    const v = parseFloat(panel?.dataset?.pxPerSec || "");
    return Number.isFinite(v) && v > 0 ? v : 40;
  }

  function eventRange(el) {
    const left = parseFloat(el.style.left) || 0;
    const width = parseFloat(el.style.width) || 0;
    return { left, right: left + width, width };
  }

  function formatDur(px, pps) {
    const sec = Math.max(0, px / pps);
    return sec.toFixed(2).replace(".", ",");
  }

  function syncCrossfadeLabels(area) {
    if (!area) return;
    const pps = pxPerSec(area);
    const selected = Array.from(area.querySelectorAll(".event.is-selected")).map(eventRange);

    area.querySelectorAll(".crossfade-zone").forEach((zone) => {
      const zLeft = parseFloat(zone.style.left) || 0;
      const zWidth = parseFloat(zone.style.width) || 0;
      const zRight = zLeft + zWidth;
      const active = selected.some((r) => r.left < zRight && r.right > zLeft);
      zone.classList.toggle("is-active", active);

      let dur = zone.querySelector(".crossfade-zone__dur");
      if (!active) {
        if (dur) dur.hidden = true;
        return;
      }
      if (!dur) {
        dur = document.createElement("span");
        dur.className = "crossfade-zone__dur";
        zone.appendChild(dur);
      }
      dur.hidden = false;
      dur.textContent = zone.dataset.dur || formatDur(zWidth, pps);
    });

    area.querySelectorAll(".event__fade-dur").forEach((el) => el.remove());
  }

  function expand(events, area) {
    if (window.VegasEventGroup?.expandToGroups) {
      return window.VegasEventGroup.expandToGroups(events, area);
    }
    return (events || []).filter(Boolean);
  }

  function selectEvent(eventEl, opts) {
    const area = eventEl?.closest?.(".tracks-area");
    if (!area || !eventEl) return [];
    const additive = !!(opts && opts.additive);
    if (!additive) clearSelection(area);
    const members = expand([eventEl], area);
    members.forEach((m) => m.classList.add("is-selected"));
    selectionAnchor = eventEl;
    syncCrossfadeLabels(area);
    return members;
  }

  function toggleEvent(eventEl) {
    const area = eventEl?.closest?.(".tracks-area");
    if (!area || !eventEl) return [];
    const members = expand([eventEl], area);
    const allOn = members.every((m) => m.classList.contains("is-selected"));
    if (allOn) {
      members.forEach((m) => m.classList.remove("is-selected"));
      if (selectionAnchor && members.includes(selectionAnchor)) {
        selectionAnchor = area.querySelector(".event.is-selected");
      }
    } else {
      members.forEach((m) => m.classList.add("is-selected"));
      selectionAnchor = eventEl;
    }
    syncCrossfadeLabels(area);
    return members;
  }

  function selectRange(eventEl) {
    const area = eventEl?.closest?.(".tracks-area");
    if (!area || !eventEl) return [];

    if (!selectionAnchor || !selectionAnchor.isConnected || !area.contains(selectionAnchor)) {
      return selectEvent(eventEl);
    }

    const laneA = selectionAnchor.closest(".track-lane");
    const laneB = eventEl.closest(".track-lane");
    const inner = area.querySelector(".tracks-inner") || area;
    const lanes = Array.from(inner.querySelectorAll(":scope > .track-lane"));
    const iA = lanes.indexOf(laneA);
    const iB = lanes.indexOf(laneB);
    if (iA < 0 || iB < 0) return selectEvent(eventEl);

    const loLane = Math.min(iA, iB);
    const hiLane = Math.max(iA, iB);
    const rA = eventRange(selectionAnchor);
    const rB = eventRange(eventEl);
    const lo = Math.min(rA.left, rB.left);
    const hi = Math.max(rA.right, rB.right);

    clearSelection(area);
    const picked = [];
    for (let i = loLane; i <= hiLane; i++) {
      lanes[i].querySelectorAll(":scope > .event").forEach((ev) => {
        const r = eventRange(ev);
        if (r.right > lo + 0.5 && r.left < hi - 0.5) picked.push(ev);
      });
    }
    expand(picked, area).forEach((m) => m.classList.add("is-selected"));
    // Keep original anchor for further Shift+clicks
    syncCrossfadeLabels(area);
    return picked;
  }

  function applySelectionModifiers(eventEl, e) {
    if (!eventEl) return;
    if (e.shiftKey) selectRange(eventEl);
    else if (e.ctrlKey || e.metaKey) toggleEvent(eventEl);
    else if (!eventEl.classList.contains("is-selected")) selectEvent(eventEl);
    else selectionAnchor = eventEl;
  }

  window.VegasTimelineSelect = {
    syncSelectionChrome: () => {
      document.querySelectorAll(".tracks-area").forEach(syncCrossfadeLabels);
    },
    clearSelection,
    selectEvent,
    toggleEvent,
    selectRange,
    applySelectionModifiers,
    getAnchor: () => selectionAnchor,
    setAnchor: (ev) => {
      selectionAnchor = ev || null;
    },
  };

  function initMediaCards() {
    const grid = document.querySelector(".media-grid");
    if (!grid) return;
    grid.addEventListener("click", (e) => {
      const card = e.target.closest(".media-card");
      if (!card) return;
      grid.querySelectorAll(".media-card.is-selected").forEach((c) => c.classList.remove("is-selected"));
      card.classList.add("is-selected");
    });
  }

  function init() {
    const area = document.querySelector(".tracks-area");
    if (area) {
      // Empty click clears; event selection is handled on pointerdown (move / trim).
      area.addEventListener("click", (e) => {
        if (document.body.classList.contains("is-dragging-event")) return;
        if (document.body.classList.contains("is-trimming-event")) return;
        if (e.target.closest(".event, .event-btn, .event__fade, .event__fade-handle, .event__trim, .playhead")) {
          return;
        }
        clearSelection(area);
        selectionAnchor = null;
        syncCrossfadeLabels(area);
      });
      syncCrossfadeLabels(area);
    }
    initMediaCards();
  }

  document.addEventListener("DOMContentLoaded", init);
})();
