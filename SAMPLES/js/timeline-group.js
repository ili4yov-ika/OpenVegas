/**
 * Vegas-style event grouping (AV pairs move/select together).
 */
(function () {
  let groupSeq = 1;

  function areaRoot(el) {
    return el?.closest?.(".tracks-area") || document.querySelector(".tracks-area");
  }

  function isIgnoreGrouping(area) {
    const a = area || document.querySelector(".tracks-area");
    return a?.dataset?.ignoreGrouping === "1";
  }

  function setIgnoreGrouping(on, area) {
    const a = area || document.querySelector(".tracks-area");
    if (!a) return;
    a.dataset.ignoreGrouping = on ? "1" : "0";
    a.classList.toggle("is-ignore-grouping", !!on);
    document.body.classList.toggle("is-ignore-event-grouping", !!on);
  }

  function toggleIgnoreGrouping(area) {
    const next = !isIgnoreGrouping(area);
    setIgnoreGrouping(next, area);
    return next;
  }

  function eventKind(ev) {
    if (ev.classList.contains("event--video")) return "video";
    if (ev.classList.contains("event--audio")) return "audio";
    return null;
  }

  function eventLeft(ev) {
    return parseFloat(ev.style.left) || 0;
  }

  function eventWidth(ev) {
    return parseFloat(ev.style.width) || 0;
  }

  function markGrouped(ev, id) {
    if (!ev) return;
    ev.dataset.groupId = id;
    ev.classList.add("is-grouped");
  }

  function clearGrouped(ev) {
    if (!ev) return;
    delete ev.dataset.groupId;
    ev.classList.remove("is-grouped");
  }

  function groupMembers(evOrId, area) {
    const id = typeof evOrId === "string" ? evOrId : evOrId?.dataset?.groupId;
    if (!id) return typeof evOrId === "string" ? [] : evOrId ? [evOrId] : [];
    const root = area || areaRoot(typeof evOrId === "string" ? null : evOrId) || document;
    return Array.from(root.querySelectorAll('.event[data-group-id="' + id + '"]'));
  }

  function nextGroupId() {
    return "g" + groupSeq++;
  }

  /** Seed seq above any existing data-group-id numbers. */
  function syncSeqFromDom(root) {
    (root || document).querySelectorAll(".event[data-group-id]").forEach((ev) => {
      const m = /^g(\d+)$/.exec(ev.dataset.groupId || "");
      if (m) groupSeq = Math.max(groupSeq, parseInt(m[1], 10) + 1);
    });
  }

  /**
   * Select all members of groups that any of `events` belong to.
   * Returns the expanded list.
   */
  function expandToGroups(events, area) {
    const list = Array.isArray(events) ? events.filter(Boolean) : [];
    if (!list.length) return [];
    const root = area || areaRoot(list[0]);
    if (!root || isIgnoreGrouping(root)) return list.slice();

    const out = new Set(list);
    list.forEach((ev) => {
      groupMembers(ev, root).forEach((m) => out.add(m));
    });
    return Array.from(out);
  }

  function applySelection(events, area) {
    const root = area || areaRoot(events[0]);
    if (!root) return;
    root.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
    events.forEach((e) => e.classList.add("is-selected"));
    window.VegasTimelineSelect?.syncSelectionChrome?.();
  }

  function selectWithGroup(ev, area, opts) {
    const root = area || areaRoot(ev);
    if (!ev || !root) return [];
    const additive = !!(opts && opts.additive);
    if (!additive) {
      root.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
    }
    const members = expandToGroups([ev], root);
    members.forEach((m) => m.classList.add("is-selected"));
    window.VegasTimelineSelect?.syncSelectionChrome?.();
    return members;
  }

  /**
   * Group selected events. Merges any existing group ids into one.
   */
  function groupEvents(events) {
    const list = Array.from(new Set((events || []).filter(Boolean)));
    if (list.length < 2) return null;

    // Prefer an existing id if present, else create.
    let id = null;
    list.forEach((ev) => {
      if (ev.dataset.groupId) {
        if (!id) id = ev.dataset.groupId;
        else if (ev.dataset.groupId !== id) {
          // merge later
        }
      }
    });
    if (!id) id = nextGroupId();

    // Collect everyone already in any of these groups + the list
    const area = areaRoot(list[0]);
    const all = new Set(list);
    list.forEach((ev) => {
      if (ev.dataset.groupId) groupMembers(ev, area).forEach((m) => all.add(m));
    });
    all.forEach((ev) => markGrouped(ev, id));
    return id;
  }

  /**
   * Vegas Ungroup: remove grouping from the selected events' groups entirely
   * (all members of those groups become ungrouped).
   */
  function ungroupEvents(events) {
    const list = Array.from(new Set((events || []).filter(Boolean)));
    if (!list.length) return 0;
    const area = areaRoot(list[0]);
    const ids = new Set(list.map((e) => e.dataset.groupId).filter(Boolean));
    if (!ids.size) {
      // nothing grouped — clear classes just in case
      list.forEach(clearGrouped);
      return 0;
    }
    let n = 0;
    ids.forEach((id) => {
      groupMembers(id, area).forEach((ev) => {
        clearGrouped(ev);
        n++;
      });
    });
    return n;
  }

  function clearGroup(ev) {
    if (!ev?.dataset?.groupId) return 0;
    return ungroupEvents([ev]);
  }

  function roughlyEqual(a, b, tol) {
    return Math.abs(a - b) <= (tol == null ? 2 : tol);
  }

  /**
   * Auto-group video+audio pairs that share start (and roughly length),
   * preferring same track-index (V1↔A1, V2↔A2, …) like Vegas AV sync groups.
   */
  function autoGroupAVPairs(root) {
    const scope = root || document;
    syncSeqFromDom(scope);

    scope.querySelectorAll(".timeline-panel, .tracks-area").forEach((panelOrArea) => {
      const area =
        panelOrArea.classList.contains("tracks-area")
          ? panelOrArea
          : panelOrArea.querySelector(".tracks-area");
      if (!area) return;

      const videoLanes = Array.from(area.querySelectorAll(".track-lane--video"));
      const audioLanes = Array.from(area.querySelectorAll(".track-lane--audio"));
      if (!videoLanes.length || !audioLanes.length) return;

      const usedAudio = new Set();

      videoLanes.forEach((vLane, vIdx) => {
        const preferredAudio = audioLanes[Math.min(vIdx, audioLanes.length - 1)];
        const searchOrder = [preferredAudio]
          .concat(audioLanes.filter((l) => l !== preferredAudio))
          .filter(Boolean);

        Array.from(vLane.querySelectorAll(".event.event--video")).forEach((vEv) => {
          if (vEv.dataset.groupId) return;
          const vLeft = eventLeft(vEv);
          const vWidth = eventWidth(vEv);

          let match = null;
          for (let i = 0; i < searchOrder.length && !match; i++) {
            const aLane = searchOrder[i];
            const candidates = Array.from(aLane.querySelectorAll(".event.event--audio")).filter(
              (aEv) => !aEv.dataset.groupId && !usedAudio.has(aEv)
            );
            // Prefer same left+width, then same left only
            match =
              candidates.find(
                (aEv) => roughlyEqual(eventLeft(aEv), vLeft) && roughlyEqual(eventWidth(aEv), vWidth, 4)
              ) || candidates.find((aEv) => roughlyEqual(eventLeft(aEv), vLeft));
          }

          if (!match) return;
          usedAudio.add(match);
          const id = nextGroupId();
          markGrouped(vEv, id);
          markGrouped(match, id);
        });
      });
    });
  }

  /**
   * After a time move of `primaryKind` events, keep group peers' left in sync.
   * Lane changes are applied only to same-kind members by the caller.
   */
  function collectMoveSet(seedEvents, primaryKind, area) {
    const root = area || areaRoot(seedEvents[0]);
    const seeds = (seedEvents || []).filter(Boolean);
    if (!seeds.length) return { movers: [], timePeers: [] };

    const expanded = expandToGroups(seeds, root);
    const movers = expanded.filter(
      (ev) => eventKind(ev) === primaryKind && !ev.classList.contains("is-event-locked")
    );
    const timePeers = expanded.filter(
      (ev) => eventKind(ev) !== primaryKind && !ev.classList.contains("is-event-locked")
    );
    // Ensure primary seeds are included even if somehow filtered
    seeds.forEach((ev) => {
      if (
        eventKind(ev) === primaryKind &&
        !ev.classList.contains("is-event-locked") &&
        !movers.includes(ev)
      ) {
        movers.push(ev);
      }
    });
    return { movers, timePeers, all: expanded };
  }

  window.VegasEventGroup = {
    isIgnoreGrouping,
    setIgnoreGrouping,
    toggleIgnoreGrouping,
    groupMembers,
    expandToGroups,
    selectWithGroup,
    applySelection,
    groupEvents,
    ungroupEvents,
    clearGroup,
    autoGroupAVPairs,
    collectMoveSet,
    markGrouped,
    nextGroupId,
    syncSeqFromDom,
  };

  document.addEventListener("DOMContentLoaded", () => {
    autoGroupAVPairs(document);
  });

  // Re-pair after dynamic upgrades / new events
  document.addEventListener("vegas:events-changed", () => {
    autoGroupAVPairs(document);
  });
})();
