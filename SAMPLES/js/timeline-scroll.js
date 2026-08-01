/**
 * Vegas-style timeline scrollbars (H/V) with zoom +/- ,
 * Ctrl/Meta + mouse wheel horizontal zoom, and header/ruler sync.
 */
(function () {
  const ZOOM_MIN = 0.35;
  const ZOOM_MAX = 3.5;
  const ZOOM_WHEEL_STEP = 1.12;
  const ZOOM_BTN_STEP = 1.18;
  const BASE_PPS = 40;
  const STORAGE_H = "openvegas-tl-zoom-h";
  const STORAGE_V = "openvegas-tl-zoom-v";

  function clamp(n, a, b) {
    return Math.max(a, Math.min(b, n));
  }

  function getZoom(key, fallback) {
    try {
      const v = parseFloat(localStorage.getItem(key));
      if (Number.isFinite(v)) return clamp(v, ZOOM_MIN, ZOOM_MAX);
    } catch (_) {}
    return fallback;
  }

  function setZoom(key, v) {
    try {
      localStorage.setItem(key, String(v));
    } catch (_) {}
  }

  function scalePxStyle(el, prop, ratio) {
    if (!el?.style) return;
    const raw = el.style[prop];
    if (raw == null || raw === "") return;
    const v = parseFloat(raw);
    if (!Number.isFinite(v)) return;
    el.style[prop] = Math.round(v * ratio) + "px";
  }

  function scaleTimelineHorizontal(panel, ratio) {
    if (!Number.isFinite(ratio) || Math.abs(ratio - 1) < 1e-6) return;

    panel.querySelectorAll(".event").forEach((ev) => {
      scalePxStyle(ev, "left", ratio);
      scalePxStyle(ev, "width", ratio);
      ["fadeIn", "fadeOut"].forEach((key) => {
        const raw = parseFloat(ev.dataset[key]);
        if (Number.isFinite(raw) && raw > 0) {
          ev.dataset[key] = String(Math.max(0, Math.round(raw * ratio)));
        }
      });
      ev.querySelectorAll(".event__fade").forEach((fade) => {
        scalePxStyle(fade, "width", ratio);
      });
    });

    panel
      .querySelectorAll(
        ".crossfade-zone, .playhead, .ruler-playhead-mark, .ruler-marker, .marker-guide, .ruler-loop, .timeline-selband, .timeline-loopband, .ruler-selection"
      )
      .forEach((el) => {
        scalePxStyle(el, "left", ratio);
        scalePxStyle(el, "width", ratio);
      });

    if (Number.isFinite(parseFloat(panel.dataset.initialPlayhead))) {
      panel.dataset.initialPlayhead = String(
        Math.round(parseFloat(panel.dataset.initialPlayhead) * ratio)
      );
    }
  }

  function applyZoom(panel, zoomH, zoomV) {
    const inner = panel.querySelector(".tracks-inner");
    if (!inner) return;
    const baseW = Number(inner.dataset.baseMinW) || 2000;

    const pps = BASE_PPS * zoomH;
    document.documentElement.style.setProperty("--px-per-sec", String(pps));
    panel.dataset.pxPerSec = String(pps);
    panel.dataset.tlZoomV = String(zoomV);

    inner.style.minWidth = Math.round(baseW * zoomH) + "px";

    if (window.VegasTimelineResize?.applyTrackHeights) {
      window.VegasTimelineResize.ensureTrackHeightGrips?.(panel);
      window.VegasTimelineResize.applyTrackHeights(panel, zoomV);
    } else {
      const baseTrackV =
        parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--track-h-video")) || 96;
      const baseTrackA =
        parseFloat(getComputedStyle(document.documentElement).getPropertyValue("--track-h-audio")) || 100;
      function lockH(el, px) {
        const h = px + "px";
        el.style.height = h;
        el.style.minHeight = h;
        el.style.maxHeight = h;
        if (el.classList.contains("track-header")) el.style.flex = "0 0 " + h;
      }
      panel.querySelectorAll(".track-header--video, .track-lane--video").forEach((el) => {
        lockH(el, Math.round(baseTrackV * zoomV));
      });
      panel.querySelectorAll(".track-header--audio, .track-lane--audio").forEach((el) => {
        lockH(el, Math.round(baseTrackA * zoomV));
      });
    }
  }

  function updateThumbs(panel) {
    const area = panel.querySelector(".tracks-area");
    const hThumb = panel.querySelector(".tl-scroll__h-thumb");
    const vThumb = panel.querySelector(".tl-scroll__v-thumb");
    if (!area) return;

    if (hThumb) {
      const max = area.scrollWidth - area.clientWidth;
      const track = hThumb.parentElement;
      const tw = Math.max(1, track.clientWidth);
      const ratio = area.clientWidth / Math.max(1, area.scrollWidth);
      const thumbW = clamp(Math.round(tw * ratio), 22, tw);
      const left = max > 0 ? (area.scrollLeft / max) * (tw - thumbW) : 0;
      hThumb.style.width = thumbW + "px";
      hThumb.style.transform = "translateX(" + Math.round(left) + "px)";
    }

    if (vThumb) {
      const max = area.scrollHeight - area.clientHeight;
      const track = vThumb.parentElement;
      const th = Math.max(1, track.clientHeight);
      const ratio = area.clientHeight / Math.max(1, area.scrollHeight);
      const thumbH = clamp(Math.round(th * ratio), 22, th);
      const top = max > 0 ? (area.scrollTop / max) * (th - thumbH) : 0;
      vThumb.style.height = thumbH + "px";
      vThumb.style.transform = "translateY(" + Math.round(top) + "px)";
    }
  }

  function ensureRulerScroller(ticks) {
    if (!ticks || ticks.dataset.scrollWired) return ticks.querySelector(".ruler-ticks__scroller");
    ticks.dataset.scrollWired = "1";
    const scroller = document.createElement("div");
    scroller.className = "ruler-ticks__scroller";
    while (ticks.firstChild) scroller.appendChild(ticks.firstChild);
    ticks.appendChild(scroller);
    return scroller;
  }

  function ensureLaneScroller(lane) {
    if (!lane) return null;
    let scroller = lane.querySelector(".marker-lane__scroller");
    if (scroller) return scroller;
    scroller = document.createElement("div");
    scroller.className = "marker-lane__scroller";
    while (lane.firstChild) scroller.appendChild(lane.firstChild);
    lane.appendChild(scroller);
    return scroller;
  }

  function syncFromArea(panel) {
    const area = panel.querySelector(".tracks-area");
    const headers = panel.querySelector(".track-headers");
    const ticks = panel.querySelector(".ruler-ticks");
    const markerLane = panel.querySelector(".marker-lane");
    const inner = panel.querySelector(".tracks-inner");
    if (!area) return;
    if (headers) headers.scrollTop = area.scrollTop;
    const minW = inner ? inner.style.minWidth || getComputedStyle(inner).minWidth : "";
    const tx = "translateX(" + -area.scrollLeft + "px)";
    if (ticks) {
      const scroller = ensureRulerScroller(ticks);
      if (scroller) {
        if (minW) scroller.style.minWidth = minW;
        scroller.style.transform = tx;
      }
    }
    if (markerLane) {
      const scroller = ensureLaneScroller(markerLane);
      if (scroller) {
        if (minW) scroller.style.minWidth = minW;
        scroller.style.transform = tx;
      }
    }
    updateThumbs(panel);
  }

  function wireThumbDrag(thumb, axis, panel) {
    if (!thumb || thumb.dataset.wired) return;
    thumb.dataset.wired = "1";
    const area = panel.querySelector(".tracks-area");

    thumb.addEventListener("pointerdown", (e) => {
      if (e.target.closest(".tl-scroll__h-grip, .tl-scroll__v-grip")) return;
      e.preventDefault();
      e.stopPropagation();
      const track = thumb.parentElement;
      const start = axis === "x" ? e.clientX : e.clientY;
      const startScroll = axis === "x" ? area.scrollLeft : area.scrollTop;
      const maxScroll =
        axis === "x" ? area.scrollWidth - area.clientWidth : area.scrollHeight - area.clientHeight;
      const travel =
        axis === "x" ? track.clientWidth - thumb.offsetWidth : track.clientHeight - thumb.offsetHeight;

      function onMove(ev) {
        const delta = (axis === "x" ? ev.clientX : ev.clientY) - start;
        const next = startScroll + (travel > 0 ? (delta / travel) * maxScroll : 0);
        if (axis === "x") area.scrollLeft = next;
        else area.scrollTop = next;
      }
      function onUp() {
        window.removeEventListener("pointermove", onMove);
        window.removeEventListener("pointerup", onUp);
      }
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });
  }

  /** Drag left/right notches on H thumb → zoom time (Vegas-style). */
  function wireHThumbZoomGrips(thumb, setZoomH) {
    if (!thumb || thumb.dataset.gripsWired) return;
    thumb.dataset.gripsWired = "1";
    const area = thumb.closest(".timeline-panel")?.querySelector(".tracks-area");

    thumb.querySelectorAll(".tl-scroll__h-grip").forEach((grip) => {
      grip.addEventListener("pointerdown", (e) => {
        e.preventDefault();
        e.stopPropagation();
        const edge = grip.getAttribute("data-zoom-edge") === "l" ? "l" : "r";
        const startX = e.clientX;
        const startZoom = parseFloat(thumb.closest(".timeline-panel")?.dataset?.tlZoomH || "1") || 1;
        const viewRect = area?.getBoundingClientRect();
        // Keep the opposite viewport edge anchored while resizing the visible window
        const anchorClientX =
          edge === "l"
            ? (viewRect ? viewRect.right - 4 : undefined)
            : (viewRect ? viewRect.left + 4 : undefined);

        document.body.classList.add("is-tl-zoom-drag");

        function onMove(ev) {
          const dx = ev.clientX - startX;
          // Outward drag (wider thumb) → zoom out; inward → zoom in
          const outward = edge === "l" ? -dx : dx;
          const factor = Math.exp(-outward / 140);
          setZoomH(startZoom * factor, anchorClientX);
        }
        function onUp() {
          document.body.classList.remove("is-tl-zoom-drag");
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
        }
        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      });
    });
  }

  function ensureChrome(panel) {
    if (panel.querySelector(".tl-scroll")) return panel.querySelector(".tl-scroll");
    if (!panel.querySelector(".timeline-main")) return null;

    const chrome = document.createElement("div");
    chrome.className = "tl-scroll";
    chrome.innerHTML =
      '<div class="tl-scroll__v">' +
      '<div class="tl-scroll__v-track"><div class="tl-scroll__v-thumb" role="scrollbar" aria-orientation="vertical"></div></div>' +
      '<div class="tl-scroll__zoom-v">' +
      '<button type="button" class="tl-scroll__btn" data-zoom="v-in" title="Zoom Track Height In">+</button>' +
      '<button type="button" class="tl-scroll__btn" data-zoom="v-out" title="Zoom Track Height Out">−</button>' +
      "</div></div>" +
      '<div class="tl-scroll__h-row">' +
      '<div class="tl-scroll__corner" aria-hidden="true"></div>' +
      '<div class="tl-scroll__h">' +
      '<div class="tl-scroll__h-track">' +
      '<div class="tl-scroll__h-thumb" role="scrollbar" aria-orientation="horizontal" title="Drag to scroll · edges to zoom">' +
      '<span class="tl-scroll__h-grip tl-scroll__h-grip--l" data-zoom-edge="l" title="Zoom"></span>' +
      '<span class="tl-scroll__h-grip tl-scroll__h-grip--r" data-zoom-edge="r" title="Zoom"></span>' +
      "</div></div>" +
      '<div class="tl-scroll__zoom-h">' +
      '<button type="button" class="tl-scroll__btn" data-zoom="h-out" title="Zoom Out Time (Ctrl+Wheel)">−</button>' +
      '<button type="button" class="tl-scroll__btn" data-zoom="h-in" title="Zoom In Time (Ctrl+Wheel)">+</button>' +
      "</div></div></div>";

    const tools = panel.querySelector(".timeline-tools");
    if (tools) panel.insertBefore(chrome, tools);
    else panel.appendChild(chrome);
    return chrome;
  }

  function afterZoom(panel) {
    window.VegasTimelineChrome?.rebuildTimeTicks?.(panel);
    window.VegasTimelineChrome?.syncEventToolsForCrossfades?.(panel);
    window.VegasTimelineSelect?.syncSelectionChrome?.();
    requestAnimationFrame(() => {
      syncFromArea(panel);
      updateThumbs(panel);
    });
  }

  function initPanel(panel) {
    if (panel.dataset.tlScroll) return;
    panel.dataset.tlScroll = "1";
    panel.classList.add("timeline-panel--scroll");

    const area = panel.querySelector(".tracks-area");
    const inner = panel.querySelector(".tracks-inner");
    if (!area || !inner) return;

    if (!inner.dataset.baseMinW) {
      const mw = parseInt(getComputedStyle(inner).minWidth, 10);
      inner.dataset.baseMinW = String(Number.isFinite(mw) && mw > 0 ? mw : 2000);
    }

    if (!inner.querySelector(".tracks-filler")) {
      const filler = document.createElement("div");
      filler.className = "tracks-filler";
      filler.setAttribute("aria-hidden", "true");
      inner.appendChild(filler);
    }

    const chrome = ensureChrome(panel);
    if (!chrome) return;

    let zoomH = getZoom(STORAGE_H, 1);
    let zoomV = getZoom(STORAGE_V, 1);
    panel.dataset.tlZoomH = String(zoomH);

    // HTML is authored at zoom 1; re-apply stored horizontal zoom to content.
    if (Math.abs(zoomH - 1) > 1e-6) {
      scaleTimelineHorizontal(panel, zoomH);
    }
    applyZoom(panel, zoomH, zoomV);

    function setZoomH(nextZoomH, anchorClientX) {
      const prev = zoomH;
      zoomH = clamp(nextZoomH, ZOOM_MIN, ZOOM_MAX);
      panel.dataset.tlZoomH = String(zoomH);
      if (Math.abs(zoomH - prev) < 1e-6) return;

      const ratio = zoomH / prev;
      const rect = area.getBoundingClientRect();
      const viewX = Number.isFinite(anchorClientX)
        ? clamp(anchorClientX - rect.left, 0, area.clientWidth)
        : area.clientWidth / 2;
      const contentX = area.scrollLeft + viewX;

      scaleTimelineHorizontal(panel, ratio);
      setZoom(STORAGE_H, zoomH);
      applyZoom(panel, zoomH, zoomV);
      area.scrollLeft = contentX * ratio - viewX;
      afterZoom(panel);
    }

    function setZoomV(nextZoomV) {
      zoomV = clamp(nextZoomV, ZOOM_MIN, ZOOM_MAX);
      setZoom(STORAGE_V, zoomV);
      applyZoom(panel, zoomH, zoomV);
      requestAnimationFrame(() => {
        syncFromArea(panel);
        updateThumbs(panel);
      });
    }

    area.addEventListener("scroll", () => syncFromArea(panel), { passive: true });

    const headers = panel.querySelector(".track-headers");
    if (headers) {
      headers.addEventListener(
        "scroll",
        () => {
          area.scrollTop = headers.scrollTop;
          updateThumbs(panel);
        },
        { passive: true }
      );
    }

    const hThumb = chrome.querySelector(".tl-scroll__h-thumb");
    wireThumbDrag(hThumb, "x", panel);
    wireHThumbZoomGrips(hThumb, setZoomH);
    wireThumbDrag(chrome.querySelector(".tl-scroll__v-thumb"), "y", panel);

    chrome.addEventListener("click", (e) => {
      const btn = e.target.closest("[data-zoom]");
      if (!btn) return;
      const act = btn.getAttribute("data-zoom");
      if (act === "h-in") setZoomH(zoomH * ZOOM_BTN_STEP);
      else if (act === "h-out") setZoomH(zoomH / ZOOM_BTN_STEP);
      else if (act === "v-in") setZoomV(zoomV * ZOOM_BTN_STEP);
      else if (act === "v-out") setZoomV(zoomV / ZOOM_BTN_STEP);
    });

    // Ctrl/Meta + wheel → horizontal time zoom (anchor under cursor)
    panel.addEventListener(
      "wheel",
      (e) => {
        if (!e.ctrlKey && !e.metaKey) return;
        e.preventDefault();
        const factor = e.deltaY < 0 ? ZOOM_WHEEL_STEP : 1 / ZOOM_WHEEL_STEP;
        setZoomH(zoomH * factor, e.clientX);
      },
      { passive: false }
    );

    chrome.querySelector(".tl-scroll__h-track")?.addEventListener("pointerdown", (e) => {
      if (e.target.closest(".tl-scroll__h-thumb")) return;
      const track = e.currentTarget;
      const rect = track.getBoundingClientRect();
      const ratio = (e.clientX - rect.left) / Math.max(1, rect.width);
      area.scrollLeft = ratio * (area.scrollWidth - area.clientWidth);
    });
    chrome.querySelector(".tl-scroll__v-track")?.addEventListener("pointerdown", (e) => {
      if (e.target.closest(".tl-scroll__v-thumb")) return;
      const track = e.currentTarget;
      const rect = track.getBoundingClientRect();
      const ratio = (e.clientY - rect.top) / Math.max(1, rect.height);
      area.scrollTop = ratio * (area.scrollHeight - area.clientHeight);
    });

    const ro = new ResizeObserver(() => updateThumbs(panel));
    ro.observe(area);
    ro.observe(inner);

    syncFromArea(panel);
    requestAnimationFrame(() => {
      window.VegasTimelineChrome?.rebuildTimeTicks?.(panel);
      updateThumbs(panel);
    });

    panel._vegasZoom = {
      zoomIn: () => setZoomH(zoomH * ZOOM_BTN_STEP),
      zoomOut: () => setZoomH(zoomH / ZOOM_BTN_STEP),
      setZoomH,
      setZoomV,
    };
  }

  function zoomTime(panelOrNull, dir) {
    const panel = panelOrNull || document.querySelector(".timeline-panel");
    if (!panel?._vegasZoom) return;
    if (dir > 0) panel._vegasZoom.zoomIn();
    else panel._vegasZoom.zoomOut();
  }

  document.addEventListener("DOMContentLoaded", () => {
    document.querySelectorAll(".timeline-panel").forEach(initPanel);
  });

  window.VegasTimelineScroll = { zoomTime };
})();
