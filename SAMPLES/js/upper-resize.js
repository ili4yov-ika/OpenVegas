/**
 * Upper workspace layout:
 *  - vertical splitter: media bin ↔ preview (width)
 *  - horizontal splitter: upper (tabs + preview) ↔ timeline (height)
 */
(function () {
  const MIN_MEDIA = 220;
  const MIN_PREVIEW = 280;
  const MIN_UPPER = 180;
  const MIN_TIMELINE = 160;
  const STORAGE_MEDIA = "openvegas-media-w";
  const STORAGE_UPPER = "openvegas-upper-h";
  const DEFAULT_MEDIA_W = 380;
  const DEFAULT_UPPER_PCT = 55;

  function applyMediaWidth(px) {
    const w = Math.round(px);
    document.documentElement.style.setProperty("--media-w", w + "px");
    try {
      localStorage.setItem(STORAGE_MEDIA, String(w));
    } catch (_) {}
    return w;
  }

  function currentMediaWidth() {
    const raw = getComputedStyle(document.documentElement).getPropertyValue("--media-w").trim();
    const n = parseFloat(raw);
    return Number.isFinite(n) ? n : DEFAULT_MEDIA_W;
  }

  function currentMasterWidth() {
    const raw = getComputedStyle(document.documentElement).getPropertyValue("--master-w").trim();
    const n = parseFloat(raw);
    return Number.isFinite(n) ? n : 112;
  }

  function clampMedia(px, upper) {
    const maxMedia = Math.max(MIN_MEDIA, upper.clientWidth - MIN_PREVIEW - currentMasterWidth());
    return Math.max(MIN_MEDIA, Math.min(maxMedia, Math.round(px)));
  }

  function applyUpperHeight(px) {
    const h = Math.round(px);
    document.documentElement.style.setProperty("--upper-h", h + "px");
    try {
      localStorage.setItem(STORAGE_UPPER, String(h));
    } catch (_) {}
    return h;
  }

  function clampUpper(px, workspace) {
    const splitH = workspace.querySelector(":scope > .workspace-splitter")?.offsetHeight || 4;
    const maxUpper = Math.max(MIN_UPPER, workspace.clientHeight - MIN_TIMELINE - splitH);
    return Math.max(MIN_UPPER, Math.min(maxUpper, Math.round(px)));
  }

  function defaultUpperPx(workspace) {
    const pctAttr = parseFloat(workspace.dataset.upperPct || "");
    const pct = Number.isFinite(pctAttr) && pctAttr > 0 ? pctAttr : DEFAULT_UPPER_PCT;
    return Math.round(workspace.clientHeight * (pct / 100));
  }

  function ensureVerticalSplitter(upper) {
    let split = upper.querySelector(":scope > .upper-splitter");
    if (!split) {
      split = document.createElement("div");
      split.className = "upper-splitter";
      split.title = "Drag to resize Preview";
      split.setAttribute("role", "separator");
      split.setAttribute("aria-orientation", "vertical");
      const preview = upper.querySelector(".preview-panel");
      if (preview) upper.insertBefore(split, preview);
      else upper.appendChild(split);
    }
    return split;
  }

  function ensureWorkspaceSplitter(workspace) {
    let split = workspace.querySelector(":scope > .workspace-splitter");
    if (!split) {
      split = document.createElement("div");
      split.className = "workspace-splitter";
      split.title = "Drag to resize Timeline";
      split.setAttribute("role", "separator");
      split.setAttribute("aria-orientation", "horizontal");
      const upper = workspace.querySelector(".upper-workspace");
      if (upper && upper.nextSibling) workspace.insertBefore(split, upper.nextSibling);
      else if (upper) upper.after(split);
      else workspace.appendChild(split);
    }
    return split;
  }

  function wireVerticalSplitter(split, upper) {
    if (!split || split.dataset.wired) return;
    split.dataset.wired = "1";

    let dragging = false;
    let startX = 0;
    let startW = 0;

    function onMove(e) {
      if (!dragging) return;
      applyMediaWidth(clampMedia(startW + (e.clientX - startX), upper));
    }

    function onUp() {
      if (!dragging) return;
      dragging = false;
      document.body.classList.remove("is-resizing-upper");
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerup", onUp);
    }

    split.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      dragging = true;
      startX = e.clientX;
      startW = currentMediaWidth();
      document.body.classList.add("is-resizing-upper");
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });

    split.addEventListener("dblclick", () => {
      applyMediaWidth(clampMedia(DEFAULT_MEDIA_W, upper));
    });
  }

  function wireWorkspaceSplitter(split, workspace) {
    if (!split || split.dataset.wired) return;
    split.dataset.wired = "1";

    let dragging = false;
    let startY = 0;
    let startH = 0;

    function onMove(e) {
      if (!dragging) return;
      applyUpperHeight(clampUpper(startH + (e.clientY - startY), workspace));
    }

    function onUp() {
      if (!dragging) return;
      dragging = false;
      document.body.classList.remove("is-resizing-workspace");
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerup", onUp);
    }

    split.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      dragging = true;
      startY = e.clientY;
      const upper = workspace.querySelector(".upper-workspace");
      startH = upper ? upper.getBoundingClientRect().height : defaultUpperPx(workspace);
      document.body.classList.add("is-resizing-workspace");
      window.addEventListener("pointermove", onMove);
      window.addEventListener("pointerup", onUp);
    });

    split.addEventListener("dblclick", () => {
      applyUpperHeight(clampUpper(defaultUpperPx(workspace), workspace));
    });
  }

  function init() {
    try {
      const savedMedia = localStorage.getItem(STORAGE_MEDIA);
      if (savedMedia) applyMediaWidth(parseFloat(savedMedia));
    } catch (_) {}

    document.querySelectorAll(".upper-workspace").forEach((upper) => {
      const split = ensureVerticalSplitter(upper);
      wireVerticalSplitter(split, upper);
      applyMediaWidth(clampMedia(currentMediaWidth(), upper));
    });

    document.querySelectorAll(".workspace").forEach((workspace) => {
      if (!workspace.querySelector(".upper-workspace") || !workspace.querySelector(".timeline-panel")) {
        return;
      }
      const split = ensureWorkspaceSplitter(workspace);
      wireWorkspaceSplitter(split, workspace);

      let initial = defaultUpperPx(workspace);
      try {
        const saved = parseFloat(localStorage.getItem(STORAGE_UPPER));
        if (Number.isFinite(saved) && saved > 0) initial = saved;
      } catch (_) {}
      applyUpperHeight(clampUpper(initial, workspace));
    });
  }

  document.addEventListener("DOMContentLoaded", init);
  window.addEventListener("resize", () => {
    document.querySelectorAll(".workspace").forEach((workspace) => {
      const upper = workspace.querySelector(".upper-workspace");
      if (!upper) return;
      applyUpperHeight(clampUpper(upper.getBoundingClientRect().height, workspace));
    });
  });
})();
