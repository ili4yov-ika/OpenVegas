/**
 * Timeline resize:
 *  - vertical splitter: track-header column width
 *  - per-track height: drag bottom edge of track header (Vegas-style)
 */
(function () {
  const MIN_W = 140;
  const MAX_W = 560;
  const MIN_H = 36;
  const MAX_H = 420;
  const STORAGE_KEY = "openvegas-track-header-w";

  function applyWidth(px) {
    const w = Math.max(MIN_W, Math.min(MAX_W, Math.round(px)));
    document.documentElement.style.setProperty("--track-header-w", w + "px");
    try {
      localStorage.setItem(STORAGE_KEY, String(w));
    } catch (_) {}
    return w;
  }

  function currentWidth() {
    const raw = getComputedStyle(document.documentElement).getPropertyValue("--track-header-w").trim();
    const n = parseFloat(raw);
    return Number.isFinite(n) ? n : 210;
  }

  function defaultTrackH(kind) {
    const key = kind === "audio" ? "--track-h-audio" : "--track-h-video";
    const fallback = kind === "audio" ? 100 : 96;
    const n = parseFloat(getComputedStyle(document.documentElement).getPropertyValue(key));
    return Number.isFinite(n) && n > 0 ? n : fallback;
  }

  function zoomVOf(panel) {
    const n = parseFloat(panel?.dataset?.tlZoomV);
    return Number.isFinite(n) && n > 0 ? n : 1;
  }

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
    if (header.classList.contains("track-header--audio")) return "audio";
    if (header.classList.contains("track-header--video")) return "video";
    return null;
  }

  function pairForHeader(panel, header) {
    const headers = getHeaders(panel);
    const lanes = getLanes(panel);
    const idx = headers.indexOf(header);
    if (idx < 0) return null;
    return { header, lane: lanes[idx] || null, idx };
  }

  function lockH(el, px) {
    if (!el) return;
    const h = Math.round(px) + "px";
    el.style.height = h;
    el.style.minHeight = h;
    el.style.maxHeight = h;
    if (el.classList.contains("track-header")) {
      el.style.flex = "0 0 " + h;
    }
  }

  function ensureBaseH(header) {
    if (!header) return defaultTrackH("video");
    const kind = headerKind(header) || "video";
    let base = parseFloat(header.dataset.trackHBase);
    if (Number.isFinite(base) && base > 0) return base;
    const panel = getPanel(header);
    const zoomV = zoomVOf(panel);
    const current = header.offsetHeight || defaultTrackH(kind);
    base = current / zoomV;
    header.dataset.trackHBase = String(base);
    return base;
  }

  function setTrackHeight(header, displayPx, opts) {
    const panel = getPanel(header);
    if (!panel || !header) return;
    const pair = pairForHeader(panel, header);
    const kind = headerKind(header) || "video";
    const zoomV = zoomVOf(panel);
    const display = Math.max(MIN_H, Math.min(MAX_H, Math.round(displayPx)));
    const base = display / zoomV;
    header.dataset.trackHBase = String(base);
    lockH(header, display);
    if (pair?.lane) lockH(pair.lane, display);
    if (!opts || opts.thumbs !== false) {
      panel.querySelector(".tracks-area")?.dispatchEvent(new Event("scroll"));
    }
    return display;
  }

  function resetTrackHeight(header) {
    const kind = headerKind(header) || "video";
    const panel = getPanel(header);
    const zoomV = zoomVOf(panel);
    delete header.dataset.trackHBase;
    return setTrackHeight(header, defaultTrackH(kind) * zoomV);
  }

  function applyTrackHeights(panel, zoomV) {
    if (!panel) return;
    const z = Number.isFinite(zoomV) && zoomV > 0 ? zoomV : zoomVOf(panel);
    panel.dataset.tlZoomV = String(z);
    getHeaders(panel).forEach((header) => {
      const kind = headerKind(header) || "video";
      const base = parseFloat(header.dataset.trackHBase);
      const baseH = Number.isFinite(base) && base > 0 ? base : defaultTrackH(kind);
      if (!Number.isFinite(base) || base <= 0) {
        header.dataset.trackHBase = String(baseH);
      }
      const display = Math.max(MIN_H, Math.min(MAX_H, Math.round(baseH * z)));
      const pair = pairForHeader(panel, header);
      lockH(header, display);
      if (pair?.lane) lockH(pair.lane, display);
    });
  }

  function ensureGrip(header) {
    if (!header || !header.classList.contains("track-header")) return null;
    let grip = header.querySelector(":scope > .track-header__h-resize");
    if (!grip) {
      grip = document.createElement("div");
      grip.className = "track-header__h-resize";
      grip.title = "Drag to resize track height\nDouble-click to reset";
      grip.setAttribute("role", "separator");
      grip.setAttribute("aria-orientation", "horizontal");
      header.appendChild(grip);
    }
    ensureBaseH(header);
    return grip;
  }

  function ensureTrackHeightGrips(root) {
    const scope = root || document;
    scope.querySelectorAll?.(".track-header").forEach(ensureGrip);
    if (scope.classList?.contains("track-header")) ensureGrip(scope);
  }

  function ensureSplitter(panel) {
    const main = panel.querySelector(".timeline-main");
    if (!main) return null;
    let split = main.querySelector(".timeline-splitter");
    if (!split) {
      split = document.createElement("div");
      split.className = "timeline-splitter";
      split.title = "Drag to resize track controls";
      split.setAttribute("role", "separator");
      split.setAttribute("aria-orientation", "vertical");
      const headers = main.querySelector(".track-headers");
      if (headers) {
        headers.style.position = "relative";
        headers.appendChild(split);
      }
    }
    return split;
  }

  function wireSplitter(split) {
    if (!split || split.dataset.wired) return;
    split.dataset.wired = "1";

    let dragging = false;
    let startX = 0;
    let startW = 0;

    function onMove(e) {
      if (!dragging) return;
      applyWidth(startW + (e.clientX - startX));
    }

    function onUp() {
      if (!dragging) return;
      dragging = false;
      document.body.classList.remove("is-resizing-timeline");
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerup", onUp);
    }

    split.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      e.stopPropagation();
      dragging = true;
      startX = e.clientX;
      startW = currentWidth();
      document.body.classList.add("is-resizing-timeline");
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });

    split.addEventListener("dblclick", (e) => {
      e.preventDefault();
      e.stopPropagation();
      applyWidth(210);
    });
  }

  function wireTrackHeightResize() {
    if (document.documentElement.dataset.trackHResizeWired) return;
    document.documentElement.dataset.trackHResizeWired = "1";

    document.addEventListener(
      "pointerdown",
      (e) => {
        if (e.button !== 0) return;
        const grip = e.target.closest?.(".track-header__h-resize");
        if (!grip) return;
        const header = grip.closest(".track-header");
        if (!header) return;
        const panel = getPanel(header);
        if (!panel) return;

        e.preventDefault();
        e.stopPropagation();

        const startY = e.clientY;
        const startH = header.offsetHeight || ensureBaseH(header) * zoomVOf(panel);
        header.classList.add("is-resizing-h");
        document.body.classList.add("is-resizing-track-h");

        function onMove(ev) {
          setTrackHeight(header, startH + (ev.clientY - startY), { thumbs: false });
        }

        function onUp() {
          header.classList.remove("is-resizing-h");
          document.body.classList.remove("is-resizing-track-h");
          window.removeEventListener("pointermove", onMove);
          window.removeEventListener("pointerup", onUp);
          panel.querySelector(".tracks-area")?.dispatchEvent(new Event("scroll"));
        }

        window.addEventListener("pointermove", onMove);
        window.addEventListener("pointerup", onUp);
      },
      true
    );

    document.addEventListener(
      "dblclick",
      (e) => {
        const grip = e.target.closest?.(".track-header__h-resize");
        if (!grip) return;
        const header = grip.closest(".track-header");
        if (!header) return;
        e.preventDefault();
        e.stopPropagation();
        resetTrackHeight(header);
      },
      true
    );
  }

  function observeHeaders(panel) {
    const root = panel.querySelector(".track-headers");
    if (!root || root.dataset.hResizeObserved) return;
    root.dataset.hResizeObserved = "1";
    const mo = new MutationObserver(() => {
      ensureTrackHeightGrips(root);
      applyTrackHeights(panel);
    });
    mo.observe(root, { childList: true });
  }

  function init() {
    try {
      const saved = localStorage.getItem(STORAGE_KEY);
      if (saved) applyWidth(parseFloat(saved));
    } catch (_) {}

    wireTrackHeightResize();

    document.querySelectorAll(".timeline-panel").forEach((panel) => {
      const split = ensureSplitter(panel);
      wireSplitter(split);
      ensureTrackHeightGrips(panel);
      applyTrackHeights(panel);
      observeHeaders(panel);
    });
  }

  document.addEventListener("DOMContentLoaded", init);

  window.VegasTimelineResize = {
    ensureTrackHeightGrips,
    setTrackHeight,
    resetTrackHeight,
    applyTrackHeights,
    defaultTrackH,
    MIN_H,
    MAX_H,
  };
})();
