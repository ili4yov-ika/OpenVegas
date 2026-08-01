/**
 * Context-menu + hotkey actions for timeline video/audio events (Vegas-style mockup).
 */
(function () {
  const clipAttrs = {
    html: null,
    datasets: null,
    kind: null,
    switches: null,
    label: "",
  };

  let groupSeq = 1;
  let toastTimer = null;

  function toast(msg) {
    let el = document.getElementById("vegas-toast");
    if (!el) {
      el = document.createElement("div");
      el.id = "vegas-toast";
      el.className = "vegas-toast";
      document.body.appendChild(el);
    }
    el.textContent = msg;
    el.classList.add("is-visible");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove("is-visible"), 2200);
  }

  function panelOf(el) {
    return el?.closest?.(".timeline-panel") || document.querySelector(".timeline-panel");
  }

  function areaOf(el) {
    return el?.closest?.(".tracks-area") || document.querySelector(".tracks-area");
  }

  function playheadX(panel) {
    const mark = panel?.querySelector?.(".ruler-playhead-mark");
    if (mark?.style?.left) {
      const n = parseFloat(mark.style.left);
      if (Number.isFinite(n)) return n;
    }
    const ph = panel?.querySelector?.(".tracks-inner > .playhead");
    if (ph?.style?.left) {
      const n = parseFloat(ph.style.left);
      if (Number.isFinite(n)) return n;
    }
    return 0;
  }

  function eventRange(ev) {
    const left = parseFloat(ev.style.left) || 0;
    const width = Math.max(1, parseFloat(ev.style.width) || 0);
    return { left, width, right: left + width };
  }

  function eventLabel(ev) {
    return (
      ev.querySelector(".event__titlebar")?.textContent?.trim() ||
      ev.querySelector(".event__label")?.textContent?.trim() ||
      (ev.classList.contains("event--video") ? "sample_for_project_video" : "sample_for_project_audio")
    );
  }

  function selectedEvents(area, fallback) {
    let list = Array.from(area?.querySelectorAll?.(".event.is-selected") || []);
    if (!list.length && fallback?.classList?.contains("event")) list = [fallback];
    if (window.VegasEventGroup?.expandToGroups) {
      list = window.VegasEventGroup.expandToGroups(list, area);
    }
    return list;
  }

  function ensureSelected(ev) {
    const area = areaOf(ev);
    if (!area || !ev) return;
    // Keep multi-selection when acting on an already-selected event.
    if (ev.classList.contains("is-selected")) {
      if (window.VegasEventGroup?.expandToGroups && area.dataset.ignoreGrouping !== "1") {
        window.VegasEventGroup.expandToGroups([ev], area).forEach((m) => m.classList.add("is-selected"));
        window.VegasTimelineSelect?.syncSelectionChrome?.();
      }
      return;
    }
    if (window.VegasEventGroup?.selectWithGroup) {
      window.VegasEventGroup.selectWithGroup(ev, area);
    } else {
      area.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
      ev.classList.add("is-selected");
      window.VegasTimelineSelect?.syncSelectionChrome?.();
    }
  }

  function refreshCf(root) {
    window.VegasTimelineChrome?.syncCrossfadeZones?.(root || document);
    window.VegasTimelineSelect?.syncSelectionChrome?.();
  }

  function snapshotEvent(ev) {
    const area = areaOf(ev);
    const lane = ev.closest(".track-lane");
    const kind = ev.classList.contains("event--video") ? "video" : "audio";
    const kindLanes = area
      ? Array.from(area.querySelectorAll(".track-lane--" + kind))
      : [];
    const clone = ev.cloneNode(true);
    clone.classList.remove("is-selected", "is-dragging", "is-trimming");
    delete clone.dataset.wasLocked;
    return {
      html: clone.outerHTML,
      kind,
      label: eventLabel(ev),
      left: parseFloat(ev.style.left) || 0,
      width: Math.max(1, parseFloat(ev.style.width) || 0),
      kindLaneIndex: Math.max(0, kindLanes.indexOf(lane)),
      switches: {
        mute: ev.classList.contains("is-event-muted"),
        lock: ev.classList.contains("is-event-locked"),
        loop: ev.classList.contains("is-event-loop"),
        normalize: ev.classList.contains("is-normalized"),
        invertPhase: ev.classList.contains("is-invert-phase"),
        maintainAspect: ev.dataset.maintainAspect !== "0",
        reduceInterlace: ev.classList.contains("is-reduce-interlace"),
        resample: ev.classList.contains("is-resample"),
        disableResample: ev.classList.contains("is-disable-resample"),
        reverse: ev.classList.contains("is-reversed"),
      },
      datasets: {
        fadeIn: ev.dataset.fadeIn || "",
        fadeOut: ev.dataset.fadeOut || "",
        fadeCurveIn: ev.dataset.fadeCurveIn || "",
        fadeCurveOut: ev.dataset.fadeCurveOut || "",
        fxChain: ev.dataset.fxChain || "",
        rate: ev.dataset.rate || "",
        channelMode: ev.dataset.channelMode || "",
        stream: ev.dataset.stream || "",
        groupId: ev.dataset.groupId || "",
      },
    };
  }

  function copyEvents(events) {
    if (!events.length) return;
    const list = Array.from(new Set(events.filter(Boolean)));
    const primary = list[0];
    const snap = snapshotEvent(primary);
    clipAttrs.html = snap.html;
    clipAttrs.kind = snap.kind;
    clipAttrs.label = snap.label;
    clipAttrs.switches = snap.switches;
    clipAttrs.datasets = snap.datasets;
    clipAttrs.multi = list
      .slice()
      .sort((a, b) => {
        const la = parseFloat(a.style.left) || 0;
        const lb = parseFloat(b.style.left) || 0;
        if (la !== lb) return la - lb;
        const ka = a.classList.contains("event--video") ? 0 : 1;
        const kb = b.classList.contains("event--video") ? 0 : 1;
        return ka - kb;
      })
      .map(snapshotEvent);
    toast(list.length > 1 ? "Copied " + list.length + " events" : "Copied");
  }

  function deleteEvents(events) {
    if (!events.length) return;
    const area = areaOf(events[0]);
    events.forEach((ev) => ev.remove());
    refreshCf(area || document);
    toast(events.length > 1 ? "Deleted " + events.length + " events" : "Deleted");
  }

  function cutEvents(events) {
    copyEvents(events);
    deleteEvents(events);
  }

  function applyAttributes(target, snap, selective) {
    if (!target || !snap) return;
    const ds = snap.datasets || {};
    const sw = snap.switches || {};
    if (!selective || selective.fades) {
      if (ds.fadeIn) target.dataset.fadeIn = ds.fadeIn;
      if (ds.fadeOut) target.dataset.fadeOut = ds.fadeOut;
      if (ds.fadeCurveIn) target.dataset.fadeCurveIn = ds.fadeCurveIn;
      if (ds.fadeCurveOut) target.dataset.fadeCurveOut = ds.fadeCurveOut;
    }
    if (!selective || selective.fx) {
      if (ds.fxChain != null) target.dataset.fxChain = ds.fxChain;
      window.VegasTimelineChrome?.syncEventFxBadge?.(target);
    }
    if (!selective || selective.switches) {
      target.classList.toggle("is-event-muted", !!sw.mute);
      target.classList.toggle("is-event-locked", !!sw.lock);
      target.classList.toggle("is-event-loop", !!sw.loop);
      target.classList.toggle("is-normalized", !!sw.normalize);
      target.classList.toggle("is-invert-phase", !!sw.invertPhase);
      target.classList.toggle("is-reduce-interlace", !!sw.reduceInterlace);
      target.classList.toggle("is-resample", !!sw.resample);
      target.classList.toggle("is-disable-resample", !!sw.disableResample);
      target.classList.toggle("is-reversed", !!sw.reverse);
      target.dataset.maintainAspect = sw.maintainAspect === false ? "0" : "1";
    }
    if (!selective || selective.channels) {
      if (ds.channelMode) {
        target.dataset.channelMode = ds.channelMode;
        applyChannelVisual(target, ds.channelMode);
      }
    }
    syncEventBadges(target);
  }

  function pasteAttributes(events, selective) {
    if (!clipAttrs.datasets) {
      toast("Nothing to paste");
      return;
    }
    events.forEach((ev) => applyAttributes(ev, clipAttrs, selective));
    refreshCf(areaOf(events[0]));
    toast(selective ? "Selectively pasted attributes" : "Pasted event attributes");
  }

  function pasteEventsAtPlayhead(panel, area) {
    if (!clipAttrs.html && !(clipAttrs.multi && clipAttrs.multi.length)) {
      toast("Clipboard empty");
      return;
    }
    if (!area) {
      toast("No timeline");
      return;
    }
    const x = playheadX(panel);
    const snaps = clipAttrs.multi && clipAttrs.multi.length ? clipAttrs.multi : [clipAttrs];
    const minLeft = Math.min.apply(
      null,
      snaps.map((s) => (Number.isFinite(s.left) ? s.left : 0))
    );
    const groupMap = new Map();
    const created = [];

    snaps.forEach((snap) => {
      const tmp = document.createElement("div");
      tmp.innerHTML = snap.html;
      const ev = tmp.firstElementChild;
      if (!ev) return;
      ev.classList.remove("is-selected", "is-dragging", "is-trimming");
      const kind = snap.kind || (ev.classList.contains("event--video") ? "video" : "audio");
      const kindLanes = Array.from(area.querySelectorAll(".track-lane--" + kind));
      if (!kindLanes.length) return;
      const laneIdx = Math.max(0, Math.min(kindLanes.length - 1, snap.kindLaneIndex | 0));
      const lane = kindLanes[laneIdx] || kindLanes[0];
      const w = Number.isFinite(snap.width) ? snap.width : parseFloat(ev.style.width) || 100;
      const rel = (Number.isFinite(snap.left) ? snap.left : 0) - minLeft;
      ev.style.left = Math.round(x + rel) + "px";
      ev.style.width = Math.round(w) + "px";

      const oldGid = snap.datasets?.groupId || ev.dataset.groupId || "";
      if (oldGid) {
        if (!groupMap.has(oldGid)) {
          groupMap.set(
            oldGid,
            window.VegasEventGroup?.nextGroupId?.() || "pg" + Date.now() + "_" + groupMap.size
          );
        }
        const gid = groupMap.get(oldGid);
        ev.dataset.groupId = gid;
        ev.classList.add("is-grouped");
      } else {
        delete ev.dataset.groupId;
        ev.classList.remove("is-grouped");
      }

      lane.appendChild(ev);
      if (ev.dataset.tlUpgraded) {
        // already full chrome from snapshot
      } else {
        window.VegasTimelineChrome?.upgradeEvent?.(ev);
      }
      window.VegasTimelineChrome?.syncEventFxBadge?.(ev);
      created.push(ev);
    });

    area.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
    created.forEach((e) => e.classList.add("is-selected"));
    if (created[0]) window.VegasTimelineSelect?.setAnchor?.(created[0]);
    refreshCf(area);
    document.dispatchEvent(new CustomEvent("vegas:events-changed"));
    toast(created.length > 1 ? "Pasted " + created.length + " events" : "Pasted");
  }

  function cloneEventShell(ev) {
    const clone = ev.cloneNode(true);
    clone.classList.remove("is-selected", "is-dragging", "is-trimming");
    return clone;
  }

  /**
   * Split one event at timeline x (px).
   * opts.rightGroupId — group id for the right half (Vegas: rights of a split group share a new id;
   * left keeps the original). If omitted and event was grouped, right is ungrouped pending batch assign.
   */
  function splitEventAt(ev, x, opts) {
    const { left, right, width } = eventRange(ev);
    if (x <= left + 4 || x >= right - 4) {
      toast("Cursor is outside event");
      return null;
    }
    const leftW = Math.round(x - left);
    const rightW = Math.round(right - x);
    const rightEv = cloneEventShell(ev);
    ev.style.width = leftW + "px";
    rightEv.style.left = Math.round(x) + "px";
    rightEv.style.width = rightW + "px";

    // Fades: keep in on left, out on right.
    const fadeIn = parseFloat(ev.dataset.fadeIn) || 0;
    const fadeOut = parseFloat(ev.dataset.fadeOut) || 0;
    ev.dataset.fadeOut = "0";
    rightEv.dataset.fadeIn = "0";
    if (fadeIn > leftW) ev.dataset.fadeIn = String(leftW);
    if (fadeOut > rightW) rightEv.dataset.fadeOut = String(rightW);

    // Grouping: never leave both halves on the same groupId (clone would copy it).
    if (opts && opts.rightGroupId) {
      if (window.VegasEventGroup?.markGrouped) {
        window.VegasEventGroup.markGrouped(rightEv, opts.rightGroupId);
      } else {
        rightEv.dataset.groupId = opts.rightGroupId;
        rightEv.classList.add("is-grouped");
      }
    } else {
      delete rightEv.dataset.groupId;
      rightEv.classList.remove("is-grouped");
    }
    // left keeps oldGid unchanged

    ev.after(rightEv);
    syncFadeDom(ev);
    syncFadeDom(rightEv);
    return rightEv;
  }

  /**
   * Split all events at x. Same-group members → left keep old group, rights share one new group
   * (Vegas-style). Singleton right groups are dissolved.
   */
  function splitEventsAt(events, x) {
    const list = Array.from(new Set((events || []).filter(Boolean)));
    const rightGidByOld = new Map();
    const createdRights = [];
    list.forEach((ev) => {
      const oldGid = ev.dataset.groupId || "";
      let rightGid = null;
      if (oldGid) {
        if (!rightGidByOld.has(oldGid)) {
          rightGidByOld.set(
            oldGid,
            window.VegasEventGroup?.nextGroupId?.() || "g" + Date.now() + "_" + rightGidByOld.size
          );
        }
        rightGid = rightGidByOld.get(oldGid);
      }
      const rightEv = splitEventAt(ev, x, { rightGroupId: rightGid });
      if (rightEv) createdRights.push(rightEv);
    });

    // If a new "right" group has < 2 members, ungroup them.
    rightGidByOld.forEach((newId) => {
      const members = window.VegasEventGroup?.groupMembers
        ? window.VegasEventGroup.groupMembers(newId)
        : createdRights.filter((e) => e.dataset.groupId === newId);
      if (members.length < 2) {
        members.forEach((m) => {
          delete m.dataset.groupId;
          m.classList.remove("is-grouped");
        });
      }
    });

    return createdRights;
  }

  function syncFadeDom(ev) {
    const fadeIn = Math.max(0, parseFloat(ev.dataset.fadeIn) || 0);
    const fadeOut = Math.max(0, parseFloat(ev.dataset.fadeOut) || 0);
    const inEl = ev.querySelector('.event__fade[data-fade-side="in"]');
    const outEl = ev.querySelector('.event__fade[data-fade-side="out"]');
    if (inEl) {
      inEl.style.width = fadeIn + "px";
      inEl.classList.toggle("has-fade", fadeIn > 0);
    }
    if (outEl) {
      outEl.style.width = fadeOut + "px";
      outEl.classList.toggle("has-fade", fadeOut > 0);
    }
  }

  function trimStart(ev, x) {
    const { left, right } = eventRange(ev);
    if (x <= left + 2 || x >= right - 8) {
      toast("Cursor is outside event");
      return;
    }
    const newLeft = Math.round(x);
    const newW = Math.round(right - newLeft);
    ev.style.left = newLeft + "px";
    ev.style.width = newW + "px";
    const fadeIn = parseFloat(ev.dataset.fadeIn) || 0;
    if (fadeIn > newW) {
      ev.dataset.fadeIn = String(newW);
      syncFadeDom(ev);
    }
  }

  function trimEnd(ev, x) {
    const { left, right } = eventRange(ev);
    if (x <= left + 8 || x >= right - 2) {
      toast("Cursor is outside event");
      return;
    }
    const newW = Math.round(x - left);
    ev.style.width = newW + "px";
    const fadeOut = parseFloat(ev.dataset.fadeOut) || 0;
    if (fadeOut > newW) {
      ev.dataset.fadeOut = String(newW);
      syncFadeDom(ev);
    }
  }

  function detectScenesAndSplit(ev) {
    const { left, width } = eventRange(ev);
    const parts = Math.min(4, Math.max(2, Math.floor(width / 80)));
    if (parts < 2) {
      toast("Event too short");
      return;
    }
    const slice = width / parts;
    let current = ev;
    for (let i = 1; i < parts; i++) {
      const at = left + slice * i;
      const right = splitEventAt(current, at);
      if (!right) break;
      current = right;
    }
    toast("Detect Scenes: " + parts + " parts");
  }

  function selectEventsToEnd(ev) {
    const lane = ev.closest(".track-lane");
    const area = areaOf(ev);
    if (!lane || !area) return;
    const { left } = eventRange(ev);
    area.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
    Array.from(lane.querySelectorAll(".event")).forEach((e) => {
      if (eventRange(e).left >= left - 0.5) e.classList.add("is-selected");
    });
    refreshCf(area);
  }

  function selectAllAfterCursor(panel, area) {
    const x = playheadX(panel);
    area.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
    area.querySelectorAll(".event").forEach((e) => {
      if (eventRange(e).right > x) e.classList.add("is-selected");
    });
    refreshCf(area);
    toast("Selected events after cursor");
  }

  function openInTrimmer(ev) {
    if (window.VegasTrimmer?.openFromEvent) {
      window.VegasTrimmer.openFromEvent(ev);
      return;
    }
    const name = eventLabel(ev);
    toast("Opened in Trimmer: " + name);
  }

  function openInAudioEditor(ev, copy) {
    toast((copy ? "Opened copy in Audio Editor: " : "Opened in Audio Editor: ") + eventLabel(ev));
  }

  function selectInProjectMedia(ev) {
    const name = eventLabel(ev).toLowerCase();
    const grid = document.querySelector(".media-grid");
    const mediaTab = document.querySelector('.panel-tab[data-tab="project-media"]');
    mediaTab?.click();
    if (!grid) {
      toast("Project Media list not found");
      return;
    }
    let match = null;
    grid.querySelectorAll(".media-card").forEach((card) => {
      card.classList.remove("is-selected");
      const n = (card.querySelector(".media-card__name")?.textContent || "").toLowerCase();
      if (!match && (n.includes(name) || name.includes(n.replace(/\.[^.]+$/, "")))) {
        match = card;
      }
    });
    if (!match) match = grid.querySelector(".media-card");
    if (match) {
      match.classList.add("is-selected");
      match.scrollIntoView({ block: "nearest", behavior: "smooth" });
      toast("Selected in Project Media");
    }
  }

  function createNestedTimeline(ev) {
    ev.dataset.nested = "1";
    ev.classList.add("is-nested");
    const title = ev.querySelector(".event__titlebar");
    if (title && !title.querySelector(".event__nested-badge")) {
      const b = document.createElement("span");
      b.className = "event__nested-badge";
      b.textContent = "Nested";
      title.appendChild(b);
    }
    toast("Created Nested Timeline");
  }

  function openNestedTimeline(ev) {
    if (!ev.dataset.nested) {
      toast("Not a nested timeline");
      return;
    }
    toast("Opened Nested Timeline");
  }

  function toggleReverse(events) {
    events.forEach((ev) => {
      ev.classList.toggle("is-reversed");
      syncEventBadges(ev);
    });
    toast(events[0].classList.contains("is-reversed") ? "Reversed" : "Forward");
  }

  function createSubclip(ev) {
    const grid = document.querySelector(".media-grid");
    const mediaTab = document.querySelector('.panel-tab[data-tab="project-media"]');
    mediaTab?.click();
    const name = eventLabel(ev) + " Subclip";
    if (grid) {
      const card = document.createElement("div");
      card.className = "media-card is-selected";
      const isVideo = ev.classList.contains("event--video");
      card.innerHTML =
        '<div class="media-card__thumb' +
        (isVideo ? '">' : ' media-card__thumb--audio">♪') +
        (isVideo ? '<img src="../assets/video-thumb.jpg" alt="" />' : " SUB") +
        "</div>" +
        '<div class="media-card__name">' +
        name +
        "</div>";
      grid.querySelectorAll(".media-card.is-selected").forEach((c) => c.classList.remove("is-selected"));
      grid.appendChild(card);
      card.scrollIntoView({ block: "nearest" });
    }
    toast("Created Subclip");
  }

  function toggleSwitch(ev, key) {
    const map = {
      mute: "is-event-muted",
      lock: "is-event-locked",
      loop: "is-event-loop",
      normalize: "is-normalized",
      invertPhase: "is-invert-phase",
      reduceInterlace: "is-reduce-interlace",
      resample: "is-resample",
      disableResample: "is-disable-resample",
      maintainAspect: null,
      activeTakeInfo: null,
      ignoreGrouping: null,
    };
    if (key === "maintainAspect") {
      ev.dataset.maintainAspect = ev.dataset.maintainAspect === "0" ? "1" : "0";
      toast(ev.dataset.maintainAspect !== "0" ? "Maintain Aspect Ratio on" : "Maintain Aspect Ratio off");
      return;
    }
    if (key === "activeTakeInfo") {
      ev.dataset.activeTakeInfo = ev.dataset.activeTakeInfo === "0" ? "1" : "0";
      toast(ev.dataset.activeTakeInfo !== "0" ? "Active Take Information on" : "Active Take Information off");
      return;
    }
    if (key === "ignoreGrouping") {
      const on = window.VegasEventGroup
        ? window.VegasEventGroup.toggleIgnoreGrouping(areaOf(ev))
        : (() => {
            const area = areaOf(ev);
            const next = area?.dataset.ignoreGrouping !== "1";
            if (area) area.dataset.ignoreGrouping = next ? "1" : "0";
            return next;
          })();
      toast(on ? "Ignore Event Grouping" : "Event Grouping enabled");
      return;
    }
    const cls = map[key];
    if (!cls) return;
    ev.classList.toggle(cls);
    syncEventBadges(ev);
    const on = ev.classList.contains(cls);
    const labels = {
      mute: "Mute",
      lock: "Lock",
      loop: "Loop",
      normalize: "Normalize",
      invertPhase: "Invert Phase",
      reduceInterlace: "Reduce Interlace Flicker",
      resample: "Resample",
      disableResample: "Disable Resample",
    };
    toast((labels[key] || key) + (on ? " on" : " off"));
  }

  function syncEventBadges(ev) {
    let row = ev.querySelector(".event__switch-badges");
    if (!row) {
      row = document.createElement("div");
      row.className = "event__switch-badges";
      ev.appendChild(row);
    }
    const badges = [];
    if (ev.classList.contains("is-event-muted")) badges.push("M");
    if (ev.classList.contains("is-event-locked")) badges.push("L");
    if (ev.classList.contains("is-event-loop")) badges.push("⟳");
    if (ev.classList.contains("is-reversed")) badges.push("◀");
    if (ev.classList.contains("is-normalized")) badges.push("N");
    if (ev.classList.contains("is-invert-phase")) badges.push("Ø");
    row.textContent = badges.join(" ");
    row.hidden = !badges.length;
  }

  function toggleEnvelope(ev, kind) {
    const key = "env" + kind.charAt(0).toUpperCase() + kind.slice(1);
    const on = ev.dataset[key] === "1";
    ev.dataset[key] = on ? "0" : "1";
    let line = ev.querySelector(".event__envelope--" + kind);
    if (!on) {
      if (!line) {
        line = document.createElement("div");
        line.className = "event__envelope event__envelope--" + kind;
        ev.appendChild(line);
      }
      line.hidden = false;
    } else if (line) {
      line.remove();
    }
    toast((on ? "Removed " : "Inserted ") + kind + " envelope");
  }

  function groupEvents(events) {
    if (window.VegasEventGroup?.groupEvents) {
      const id = window.VegasEventGroup.groupEvents(events);
      if (!id) {
        toast("Select 2+ events to group");
        return;
      }
      toast("Grouped " + window.VegasEventGroup.groupMembers(id).length + " events");
      return;
    }
    if (events.length < 2) {
      toast("Select 2+ events to group");
      return;
    }
    const id = "g" + groupSeq++;
    events.forEach((ev) => {
      ev.dataset.groupId = id;
      ev.classList.add("is-grouped");
    });
    toast("Grouped " + events.length + " events");
  }

  function ungroupEvents(events) {
    if (window.VegasEventGroup?.ungroupEvents) {
      const n = window.VegasEventGroup.ungroupEvents(events);
      toast(n ? "Ungrouped" : "Not in a group");
      return;
    }
    events.forEach((ev) => {
      delete ev.dataset.groupId;
      ev.classList.remove("is-grouped");
    });
    toast("Ungrouped");
  }

  function clearGroup(ev) {
    if (window.VegasEventGroup?.clearGroup) {
      const n = window.VegasEventGroup.clearGroup(ev);
      toast(n ? "Cleared group" : "Not in a group");
      return;
    }
    const id = ev.dataset.groupId;
    if (!id) {
      toast("Not in a group");
      return;
    }
    areaOf(ev)
      ?.querySelectorAll('.event[data-group-id="' + id + '"]')
      .forEach((e) => {
        delete e.dataset.groupId;
        e.classList.remove("is-grouped");
      });
    toast("Cleared group");
  }

  function selectGroup(ev) {
    if (window.VegasEventGroup?.selectWithGroup) {
      window.VegasEventGroup.selectWithGroup(ev, areaOf(ev));
      return;
    }
    const id = ev.dataset.groupId;
    const area = areaOf(ev);
    if (!id || !area) {
      toast("Not in a group");
      return;
    }
    area.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
    area.querySelectorAll('.event[data-group-id="' + id + '"]').forEach((e) => e.classList.add("is-selected"));
    refreshCf(area);
  }

  function setStream(ev, stream) {
    ev.dataset.stream = stream;
    toast("Stream " + stream);
  }

  function applyChannelVisual(ev, mode) {
    ev.classList.remove("is-ch-left", "is-ch-right", "is-ch-mono");
    if (mode === "left") ev.classList.add("is-ch-left");
    else if (mode === "right") ev.classList.add("is-ch-right");
    else if (mode === "mono") ev.classList.add("is-ch-mono");
  }

  function setChannels(ev, mode) {
    ev.dataset.channelMode = mode;
    applyChannelVisual(ev, mode);
    const names = { left: "Left Only", right: "Right Only", stereo: "Both (Stereo)", mono: "Combine to Mono" };
    toast(names[mode] || mode);
  }

  function synchronize(events, mode) {
    if (events.length < 2) {
      toast("Select 2+ events");
      return;
    }
    if (mode === "start") {
      const left = Math.min(...events.map((e) => eventRange(e).left));
      events.forEach((e) => {
        e.style.left = Math.round(left) + "px";
      });
      toast("Aligned start times");
    } else if (mode === "timecode" || mode === "sound") {
      const base = eventRange(events[0]).left;
      events.slice(1).forEach((e, i) => {
        e.style.left = Math.round(base + (i + 1) * 4) + "px";
      });
      toast(mode === "sound" ? "Synchronized by Sound" : "Synchronized by Timecode");
    }
    refreshCf(areaOf(events[0]));
  }

  function createSyncLink(events) {
    if (events.length < 2) {
      toast("Select 2+ events");
      return;
    }
    const id = "sync" + Date.now().toString(36);
    events.forEach((e) => {
      e.dataset.syncLink = id;
      e.classList.add("is-sync-linked");
    });
    toast("Created Sync Link");
  }

  function selectLinked(ev) {
    const id = ev.dataset.syncLink;
    const area = areaOf(ev);
    if (!id || !area) {
      toast("No Sync Link");
      return;
    }
    area.querySelectorAll(".event.is-selected").forEach((e) => e.classList.remove("is-selected"));
    area.querySelectorAll('.event[data-sync-link="' + id + '"]').forEach((e) => e.classList.add("is-selected"));
    refreshCf(area);
  }

  function unlink(ev) {
    const id = ev.dataset.syncLink;
    const area = areaOf(ev);
    if (!id || !area) return;
    area.querySelectorAll('.event[data-sync-link="' + id + '"]').forEach((e) => {
      delete e.dataset.syncLink;
      e.classList.remove("is-sync-linked");
    });
    toast("Unlinked");
  }

  function nextTake(ev, dir) {
    const cur = parseInt(ev.dataset.take || "1", 10) || 1;
    const next = Math.max(1, cur + dir);
    ev.dataset.take = String(next);
    let badge = ev.querySelector(".event__take-badge");
    if (!badge) {
      badge = document.createElement("span");
      badge.className = "event__take-badge";
      ev.querySelector(".event__titlebar")?.appendChild(badge);
    }
    badge.textContent = "Take " + next;
    toast("Take " + next);
  }

  function beatDetection(ev) {
    const { left, width } = eventRange(ev);
    const lane = ev.closest(".track-lane");
    const panel = panelOf(ev);
    if (!lane || !panel) return;
    const n = Math.min(8, Math.max(3, Math.floor(width / 60)));
    for (let i = 1; i < n; i++) {
      const x = left + (width * i) / n;
      // Visual beat marks on the event
      let marks = ev.querySelector(".event__beat-marks");
      if (!marks) {
        marks = document.createElement("div");
        marks.className = "event__beat-marks";
        ev.appendChild(marks);
      }
      const m = document.createElement("span");
      m.className = "event__beat-mark";
      m.style.left = ((x - left) / width) * 100 + "%";
      marks.appendChild(m);
    }
    toast("Beat Detection: " + (n - 1) + " beats");
  }

  function tempoDetection(ev) {
    const bpm = 72 + Math.round((parseFloat(ev.style.width) || 200) % 40);
    ev.dataset.tempo = String(bpm);
    let badge = ev.querySelector(".event__tempo-badge");
    if (!badge) {
      badge = document.createElement("span");
      badge.className = "event__tempo-badge";
      ev.appendChild(badge);
    }
    badge.textContent = bpm + " BPM";
    toast("Tempo Detection: " + bpm + " BPM");
  }

  function applyNonRealtimeFx(ev) {
    const chain = (ev.dataset.fxChain || "").trim();
    const next = chain ? chain + "|Rendered FX" : "Rendered FX";
    ev.dataset.fxChain = next;
    window.VegasTimelineChrome?.syncEventFxBadge?.(ev);
    ev.classList.add("is-fx-rendered");
    toast("Applied Non-Real-Time Event FX");
  }

  function motionTracking(ev) {
    ev.classList.add("is-motion-tracking");
    toast("Motion Tracking started");
    setTimeout(() => {
      ev.classList.remove("is-motion-tracking");
      toast("Motion Tracking complete");
    }, 1200);
  }

  function showProperties(ev) {
    if (window.VegasEventProperties?.open && ev?.classList?.contains("event")) {
      window.VegasEventProperties.open(ev);
      return;
    }
    // Simple fallback for audio / missing module
    let dlg = document.getElementById("event-props-dialog");
    if (!dlg) {
      dlg = document.createElement("div");
      dlg.id = "event-props-dialog";
      dlg.className = "event-props-dialog";
      dlg.innerHTML =
        '<div class="event-props-dialog__panel">' +
        '<div class="event-props-dialog__title">Event Properties</div>' +
        '<div class="event-props-dialog__body"></div>' +
        '<div class="event-props-dialog__foot"><button type="button" class="event-props-dialog__ok">OK</button></div>' +
        "</div>";
      document.body.appendChild(dlg);
      dlg.querySelector(".event-props-dialog__ok").addEventListener("click", () => {
        dlg.classList.remove("is-open");
      });
      dlg.addEventListener("click", (e) => {
        if (e.target === dlg) dlg.classList.remove("is-open");
      });
    }
    const r = eventRange(ev);
    const pps = parseFloat(panelOf(ev)?.dataset?.pxPerSec || "40") || 40;
    const body = dlg.querySelector(".event-props-dialog__body");
    body.innerHTML =
      "<div><b>Name</b>: " +
      eventLabel(ev) +
      "</div>" +
      "<div><b>Type</b>: " +
      (ev.classList.contains("event--video") ? "Video" : "Audio") +
      "</div>" +
      "<div><b>Start</b>: " +
      (r.left / pps).toFixed(3) +
      " s</div>" +
      "<div><b>Length</b>: " +
      (r.width / pps).toFixed(3) +
      " s</div>";
    dlg.classList.add("is-open");
  }

  function channelsDialog(ev) {
    const cur = ev.dataset.channelMode || "stereo";
    const pick = window.prompt(
      "Channels: left | right | stereo | mono\nCurrent: " + cur,
      cur
    );
    if (!pick) return;
    const mode = pick.trim().toLowerCase();
    if (["left", "right", "stereo", "mono"].includes(mode)) setChannels(ev, mode);
  }

  function handleAction(action, label, target) {
    const ev = target?.closest?.(".event") || target;
    if (!ev || !ev.classList.contains("event")) return false;
    const area = areaOf(ev);
    const panel = panelOf(ev);
    ensureSelected(ev);
    const events = selectedEvents(area, ev);
    const primary = events[0] || ev;

    switch (action) {
      case "event-open-trimmer":
        openInTrimmer(primary);
        return true;
      case "event-select-media":
        selectInProjectMedia(primary);
        return true;
      case "event-create-nested":
        createNestedTimeline(primary);
        return true;
      case "event-open-nested":
        openNestedTimeline(primary);
        return true;
      case "event-motion-tracking":
        motionTracking(primary);
        return true;
      case "event-cut":
        cutEvents(events);
        return true;
      case "event-copy":
        copyEvents(events);
        return true;
      case "event-paste-attrs":
        pasteAttributes(events, null);
        return true;
      case "event-paste-attrs-selective":
        pasteAttributes(events, { switches: true, fx: true, fades: false, channels: true });
        return true;
      case "event-delete":
        deleteEvents(events);
        return true;
      case "event-trim-start":
        events.forEach((e) => trimStart(e, playheadX(panel)));
        refreshCf(area);
        toast("Trim Start");
        return true;
      case "event-trim-end":
        events.forEach((e) => trimEnd(e, playheadX(panel)));
        refreshCf(area);
        toast("Trim End");
        return true;
      case "event-detect-scenes":
        events.forEach(detectScenesAndSplit);
        refreshCf(area);
        return true;
      case "event-split":
        splitEventsAt(events, playheadX(panel));
        refreshCf(area);
        toast("Split");
        return true;
      case "event-create-subclip":
        createSubclip(primary);
        return true;
      case "event-reverse":
        toggleReverse(events);
        return true;
      case "event-select-to-end":
        selectEventsToEnd(primary);
        return true;
      case "event-select-after-cursor":
        selectAllAfterCursor(panel, area);
        return true;
      case "event-env-velocity":
        toggleEnvelope(primary, "velocity");
        return true;
      case "event-env-opacity":
        toggleEnvelope(primary, "opacity");
        return true;
      case "event-env-fade-color":
        toggleEnvelope(primary, "fadecolor");
        return true;
      case "event-switch-mute":
        events.forEach((e) => toggleSwitch(e, "mute"));
        return true;
      case "event-switch-lock":
        events.forEach((e) => toggleSwitch(e, "lock"));
        return true;
      case "event-switch-loop":
        events.forEach((e) => toggleSwitch(e, "loop"));
        return true;
      case "event-switch-normalize":
        events.forEach((e) => toggleSwitch(e, "normalize"));
        return true;
      case "event-switch-invert":
        events.forEach((e) => toggleSwitch(e, "invertPhase"));
        return true;
      case "event-switch-aspect":
        events.forEach((e) => toggleSwitch(e, "maintainAspect"));
        return true;
      case "event-switch-interlace":
        events.forEach((e) => toggleSwitch(e, "reduceInterlace"));
        return true;
      case "event-switch-resample":
        events.forEach((e) => toggleSwitch(e, "resample"));
        return true;
      case "event-switch-disable-resample":
        events.forEach((e) => toggleSwitch(e, "disableResample"));
        return true;
      case "event-take-info":
        toggleSwitch(primary, "activeTakeInfo");
        return true;
      case "event-take-next":
        nextTake(primary, 1);
        return true;
      case "event-take-prev":
        nextTake(primary, -1);
        return true;
      case "event-group":
        groupEvents(events);
        return true;
      case "event-ungroup":
        ungroupEvents(events);
        return true;
      case "event-ignore-grouping":
        toggleSwitch(primary, "ignoreGrouping");
        return true;
      case "event-clear-group":
        clearGroup(primary);
        return true;
      case "event-select-group":
        selectGroup(primary);
        return true;
      case "event-stream-0":
        setStream(primary, "0");
        return true;
      case "event-stream-1":
        setStream(primary, "1");
        return true;
      case "event-sync-timecode":
        synchronize(events, "timecode");
        return true;
      case "event-sync-sound":
        synchronize(events, "sound");
        return true;
      case "event-sync-align-start":
        synchronize(events, "start");
        return true;
      case "event-create-sync-link":
        createSyncLink(events);
        return true;
      case "event-select-linked":
        selectLinked(primary);
        return true;
      case "event-unlink":
        unlink(primary);
        return true;
      case "event-properties":
        // Always open for the right-clicked event (not another member of an AV group).
        showProperties(ev);
        return true;
      case "event-open-audio-editor":
        openInAudioEditor(primary, false);
        return true;
      case "event-open-audio-editor-copy":
        openInAudioEditor(primary, true);
        return true;
      case "event-beat-detect":
        beatDetection(primary);
        return true;
      case "event-tempo-detect":
        tempoDetection(primary);
        return true;
      case "event-nrt-fx":
        applyNonRealtimeFx(primary);
        return true;
      case "audio-channels":
        channelsDialog(primary);
        return true;
      case "event-ch-left":
        setChannels(primary, "left");
        return true;
      case "event-ch-right":
        setChannels(primary, "right");
        return true;
      case "event-ch-stereo":
        setChannels(primary, "stereo");
        return true;
      case "event-ch-mono":
        setChannels(primary, "mono");
        return true;
      case "event-paste":
        pasteEventsAtPlayhead(panel, area);
        return true;
      default:
        break;
    }

    // Label fallback for items without action ids (legacy).
    const L = (label || "").replace(/\s+/g, " ").trim();
    if (/^Open in Trimmer$/i.test(L)) return handleAction("event-open-trimmer", L, target);
    if (/^Select in Project Media/i.test(L)) return handleAction("event-select-media", L, target);
    if (/^Create Nested Timeline$/i.test(L)) return handleAction("event-create-nested", L, target);
    if (/^Open Nested Timeline$/i.test(L)) return handleAction("event-open-nested", L, target);
    if (/^Motion Tracking$/i.test(L)) return handleAction("event-motion-tracking", L, target);
    if (/^Cut$/i.test(L)) return handleAction("event-cut", L, target);
    if (/^Copy$/i.test(L)) return handleAction("event-copy", L, target);
    if (/^Paste Event Attributes$/i.test(L)) return handleAction("event-paste-attrs", L, target);
    if (/^Selectively Paste/i.test(L)) return handleAction("event-paste-attrs-selective", L, target);
    if (/^Delete$/i.test(L)) return handleAction("event-delete", L, target);
    if (/^Trim Start$/i.test(L)) return handleAction("event-trim-start", L, target);
    if (/^Trim End$/i.test(L)) return handleAction("event-trim-end", L, target);
    if (/^Detect Scenes/i.test(L)) return handleAction("event-detect-scenes", L, target);
    if (/^Split$/i.test(L)) return handleAction("event-split", L, target);
    if (/^Create Subclip$/i.test(L)) return handleAction("event-create-subclip", L, target);
    if (/^Reverse$/i.test(L)) return handleAction("event-reverse", L, target);
    if (/^Select Events to End$/i.test(L)) return handleAction("event-select-to-end", L, target);
    if (/^Select All After Cursor$/i.test(L)) return handleAction("event-select-after-cursor", L, target);
    if (/^Velocity$/i.test(L)) return handleAction("event-env-velocity", L, target);
    if (/^Opacity$/i.test(L)) return handleAction("event-env-opacity", L, target);
    if (/^Fade to Color$/i.test(L)) return handleAction("event-env-fade-color", L, target);
    if (/^Mute$/i.test(L)) return handleAction("event-switch-mute", L, target);
    if (/^Lock$/i.test(L)) return handleAction("event-switch-lock", L, target);
    if (/^Loop$/i.test(L)) return handleAction("event-switch-loop", L, target);
    if (/^Normalize$/i.test(L)) return handleAction("event-switch-normalize", L, target);
    if (/^Invert Phase$/i.test(L)) return handleAction("event-switch-invert", L, target);
    if (/^Maintain Aspect/i.test(L)) return handleAction("event-switch-aspect", L, target);
    if (/^Reduce Interlace/i.test(L)) return handleAction("event-switch-interlace", L, target);
    if (/^Disable Resample$/i.test(L)) return handleAction("event-switch-disable-resample", L, target);
    if (/^Resample$/i.test(L)) return handleAction("event-switch-resample", L, target);
    if (/^Active Take Information$/i.test(L)) return handleAction("event-take-info", L, target);
    if (/^Next Take$/i.test(L)) return handleAction("event-take-next", L, target);
    if (/^Previous Take$/i.test(L)) return handleAction("event-take-prev", L, target);
    if (/^Group$/i.test(L)) return handleAction("event-group", L, target);
    if (/^Ungroup$/i.test(L)) return handleAction("event-ungroup", L, target);
    if (/^Ignore Event Grouping$/i.test(L)) return handleAction("event-ignore-grouping", L, target);
    if (/^Clear Group$/i.test(L)) return handleAction("event-clear-group", L, target);
    if (/^Select Events in Group$/i.test(L)) return handleAction("event-select-group", L, target);
    if (/^Stream 0$/i.test(L)) return handleAction("event-stream-0", L, target);
    if (/^Stream 1$/i.test(L)) return handleAction("event-stream-1", L, target);
    if (/^By Timecode$/i.test(L)) return handleAction("event-sync-timecode", L, target);
    if (/^By Sound$/i.test(L)) return handleAction("event-sync-sound", L, target);
    if (/^Align Start Times$/i.test(L)) return handleAction("event-sync-align-start", L, target);
    if (/^Create Sync Link/i.test(L)) return handleAction("event-create-sync-link", L, target);
    if (/^Select Linked Events$/i.test(L)) return handleAction("event-select-linked", L, target);
    if (/^Unlink$/i.test(L)) return handleAction("event-unlink", L, target);
    if (/^Properties/i.test(L)) return handleAction("event-properties", L, target);
    if (/^Open in Audio Editor$/i.test(L)) return handleAction("event-open-audio-editor", L, target);
    if (/^Open Copy in Audio Editor$/i.test(L)) return handleAction("event-open-audio-editor-copy", L, target);
    if (/^Beat Detection$/i.test(L)) return handleAction("event-beat-detect", L, target);
    if (/^Tempo Detection$/i.test(L)) return handleAction("event-tempo-detect", L, target);
    if (/^Apply Non-Real-Time/i.test(L)) return handleAction("event-nrt-fx", L, target);
    if (/^Left Only$/i.test(L)) return handleAction("event-ch-left", L, target);
    if (/^Right Only$/i.test(L)) return handleAction("event-ch-right", L, target);
    if (/^Both \(Stereo\)$/i.test(L)) return handleAction("event-ch-stereo", L, target);
    if (/^Combine to Mono$/i.test(L)) return handleAction("event-ch-mono", L, target);
    if (/^Channels/i.test(L)) return handleAction("audio-channels", L, target);

    return false;
  }

  function patchEventMenuItems(items, ev) {
    const hasClip = !!(clipAttrs.datasets || clipAttrs.html);
    const hasGroup = !!ev.dataset.groupId;
    const hasSync = !!ev.dataset.syncLink;
    const isNested = ev.dataset.nested === "1";

    function walk(list) {
      return list.map((it) => {
        if (it.sep) return it;
        const next = Object.assign({}, it);
        if (Array.isArray(it.submenu)) next.submenu = walk(it.submenu);

        if (next.action === "event-paste-attrs" || next.action === "event-paste-attrs-selective") {
          next.disabled = !hasClip;
        }
        if (next.action === "event-open-nested") next.disabled = !isNested;
        if (next.action === "event-clear-group" || next.action === "event-select-group") {
          next.disabled = !hasGroup;
        }
        if (next.action === "event-select-linked" || next.action === "event-unlink") {
          next.disabled = !hasSync;
        }
        if (next.action === "event-create-sync-link") {
          const area = ev.closest(".tracks-area");
          const count = area ? area.querySelectorAll(".event.is-selected").length : 0;
          next.disabled = count < 2;
        }

        if (next.action === "event-switch-mute") next.checked = ev.classList.contains("is-event-muted");
        if (next.action === "event-switch-lock") next.checked = ev.classList.contains("is-event-locked");
        if (next.action === "event-switch-loop") next.checked = ev.classList.contains("is-event-loop");
        if (next.action === "event-switch-normalize") next.checked = ev.classList.contains("is-normalized");
        if (next.action === "event-switch-invert") next.checked = ev.classList.contains("is-invert-phase");
        if (next.action === "event-switch-aspect") next.checked = ev.dataset.maintainAspect !== "0";
        if (next.action === "event-switch-interlace") next.checked = ev.classList.contains("is-reduce-interlace");
        if (next.action === "event-switch-resample") next.checked = ev.classList.contains("is-resample");
        if (next.action === "event-switch-disable-resample") {
          next.checked = ev.classList.contains("is-disable-resample");
        }
        if (next.action === "event-take-info") next.checked = ev.dataset.activeTakeInfo !== "0";
        if (next.action === "event-ignore-grouping") {
          next.checked = window.VegasEventGroup
            ? window.VegasEventGroup.isIgnoreGrouping(areaOf(ev))
            : areaOf(ev)?.dataset.ignoreGrouping === "1";
        }
        if (next.action === "event-env-velocity") next.checked = ev.dataset.envVelocity === "1";
        if (next.action === "event-env-opacity") next.checked = ev.dataset.envOpacity === "1";
        if (next.action === "event-env-fade-color") next.checked = ev.dataset.envFadecolor === "1";
        if (next.action === "event-stream-0") next.checked = (ev.dataset.stream || "0") === "0";
        if (next.action === "event-stream-1") next.checked = ev.dataset.stream === "1";
        if (next.action === "event-ch-left") next.checked = ev.dataset.channelMode === "left";
        if (next.action === "event-ch-right") next.checked = ev.dataset.channelMode === "right";
        if (next.action === "event-ch-stereo") {
          next.checked = !ev.dataset.channelMode || ev.dataset.channelMode === "stereo";
        }
        if (next.action === "event-ch-mono") next.checked = ev.dataset.channelMode === "mono";

        return next;
      });
    }
    return walk(items);
  }

  // Expose patcher for menus.js
  window.VegasEventMenuPatch = {
    patch: patchEventMenuItems,
    hasClipboardAttrs: () => !!(clipAttrs.datasets || clipAttrs.html),
  };

  document.addEventListener("vegas:context-action", (e) => {
    const { type, action, label, target } = e.detail || {};
    if (type === "timeline-empty" || type === "video-track-empty" || type === "audio-track-empty") {
      const L = (label || "").replace(/\s+/g, " ").trim();
      if (/^Paste$/i.test(L) || action === "event-paste" || action === "edit-paste") {
        pasteEventsAtPlayhead(panelOf(target), areaOf(target) || document.querySelector(".tracks-area"));
      }
      return;
    }
    if (type !== "video-event" && type !== "audio-event") return;
    // Let dedicated FX/PanCrop handlers run first when they match.
    if (
      action === "video-pan-crop" ||
      action === "video-event-fx" ||
      action === "audio-event-fx" ||
      action === "track-fx"
    ) {
      return;
    }
    handleAction(action, label, target);
  });

  // Hotkeys moved to hotkeys.js (uses e.code for RU/EN layouts).
  // Keep a no-op flag so older pages don't double-bind if both scripts load.
  document.documentElement.dataset.vegasHotkeysLegacy = "0";

  document.addEventListener("vegas:menu-action", (e) => {
    const action = e.detail?.action || "";
    const label = e.detail?.label || "";
    const area = document.querySelector(".tracks-area");
    const panel = document.querySelector(".timeline-panel");
    if (action === "ignore-event-grouping" || /^Ignore Event Grouping$/i.test(label)) {
      const on = window.VegasEventGroup
        ? window.VegasEventGroup.toggleIgnoreGrouping(area)
        : false;
      toast(on ? "Ignore Event Grouping" : "Event Grouping enabled");
      return;
    }

    let selected = Array.from(area?.querySelectorAll?.(".event.is-selected") || []);
    if (window.VegasEventGroup?.expandToGroups) {
      selected = window.VegasEventGroup.expandToGroups(selected, area);
    }
    const L = (label || "").replace(/\s+/g, " ").trim();

    if (/^Cut$/i.test(L) || action === "edit-cut") {
      if (selected.length) cutEvents(selected);
      return;
    }
    if (/^Copy$/i.test(L) || action === "edit-copy") {
      if (selected.length) copyEvents(selected);
      return;
    }
    if (/^Paste$/i.test(L) || action === "edit-paste" || action === "event-paste") {
      pasteEventsAtPlayhead(panel, area);
      return;
    }
    if (/^Delete$/i.test(L) || action === "edit-delete") {
      if (selected.length) deleteEvents(selected);
      return;
    }
    if (/^Select All$/i.test(L) || action === "edit-select-all") {
      if (!area) return;
      area.querySelectorAll(".event").forEach((ev) => ev.classList.add("is-selected"));
      window.VegasTimelineSelect?.syncSelectionChrome?.();
      return;
    }
    if (/^Group$/i.test(L) && selected.length >= 2) {
      groupEvents(selected);
      return;
    }
    if (/^Ungroup$/i.test(L) && selected.length) {
      ungroupEvents(selected);
    }
  });

  // Block dragging locked events
  document.addEventListener(
    "pointerdown",
    (e) => {
      const ev = e.target.closest?.(".event");
      if (ev?.classList.contains("is-event-locked") && !e.target.closest(".event-btn, .event__fade-handle, .event__trim")) {
        ev.dataset.wasLocked = "1";
      }
    },
    true
  );

  window.VegasEventActions = {
    handleAction,
    toast,
    copyEvents,
    cutEvents,
    deleteEvents,
    pasteEventsAtPlayhead,
    pasteAttributes,
    splitEventAt,
    splitEventsAt,
    playheadX,
  };
})();
