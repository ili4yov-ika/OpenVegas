/**
 * Render As dialog (Vegas Pro 22-style dark mockup).
 * Open: File → Render As…, toolbar, Ctrl+Shift+M, or VegasRenderAs.open().
 */
(function () {
  const WIN_ID = "render-as";

  const FORMATS = [
    "AAC Audio",
    "AC-3",
    "Apple ProRes",
    "AIFF",
    "FLAC Audio",
    "Image Sequence",
    "AV1",
    "AVC/AAC MP4",
    "HEVC/AAC MP4",
    "MainConcept MPEG-1/2",
    "MP3 Audio",
    "OggVorbis",
    "AVC/MVC",
    "MXF",
    "Wave64",
    "XAVC / XAVC S",
    "Video for Windows",
    "Wave (Microsoft)",
    "Windows Media Video XAVC S",
    "Windows Media Video/Audio",
    "XDCAM EX",
    "XDCAM Transfer",
  ];

  const DEFAULT_FORMAT = "AVC/AAC MP4";
  const DEFAULT_TEMPLATE = "Internet HD 1080p 59.94 fps (NVENC)";

  const TEMPLATES = {
    "AVC/AAC MP4": [
      { name: "Internet UHD 2160p 59.94 fps", fav: false },
      { name: "Internet UHD 2160p 50 fps", fav: false },
      { name: "Internet UHD 2160p 29.97 fps", fav: false },
      { name: "Internet UHD 2160p 25 fps", fav: false },
      { name: "Internet UHD 2160p 23.976 fps", fav: false },
      { name: "Internet 4K 9:16 (portrait) 59.94 fps", fav: false },
      { name: "Internet 4K 9:16 (portrait) 50 fps", fav: false },
      { name: "Internet 4K 9:16 (portrait) 29.97 fps", fav: false },
      { name: "Internet 4K 9:16 (portrait) 25 fps", fav: false },
      { name: "Internet 4K 9:16 (portrait) 23.976 fps", fav: false },
      { name: "Internet HD 1080p 59.94 fps (NVENC)", fav: true, mark: "=" },
      { name: "Internet HD 1080p 50 fps (NVENC)", fav: false, mark: "=" },
      { name: "Internet HD 1080p 29.97 fps (NVENC)", fav: false, mark: "=" },
      { name: "Internet HD 1080p 25 fps (NVENC)", fav: false, mark: "=" },
      { name: "Internet HD 1080p 23.976 fps (NVENC)", fav: false, mark: "=" },
      { name: "Internet HD 1080p 59.94 fps (AMD VCE)", fav: false },
      { name: "Internet HD 1080p 50 fps (AMD VCE)", fav: false },
      { name: "Internet HD 1080p 29.97 fps (AMD VCE)", fav: false },
      { name: "Internet HD 1080p 25 fps (AMD VCE)", fav: false },
      { name: "Internet HD 1080p 23.976 fps (AMD VCE)", fav: false },
      { name: "Internet HD 1080p 59.94 fps", fav: false },
      { name: "Internet HD 1080p 50 fps", fav: false },
      { name: "Internet HD 1080p 29.97 fps", fav: false },
      { name: "Internet HD 1080p 25 fps", fav: false },
      { name: "Internet HD 1080p 23.976 fps", fav: false },
      { name: "Internet 1080 9:16 (portrait) 59.94 fps", fav: false },
      { name: "Internet 1080 9:16 (portrait) 29.97 fps", fav: false },
      { name: "Default Template", fav: false },
    ],
    "HEVC/AAC MP4": [
      { name: "Internet UHD 2160p 59.94 fps (NVENC)", fav: false },
      { name: "Internet UHD 2160p 29.97 fps (NVENC)", fav: false },
      { name: "Internet HD 1080p 59.94 fps (NVENC)", fav: true },
      { name: "Internet HD 1080p 29.97 fps (NVENC)", fav: false },
      { name: "Default Template", fav: false },
    ],
    "AV1": [
      { name: "Internet UHD 2160p 29.97 fps", fav: false },
      { name: "Internet HD 1080p 29.97 fps", fav: false },
      { name: "Default Template", fav: false },
    ],
    "AAC Audio": [
      { name: "128 Kbps, CD, Stereo", fav: false },
      { name: "192 Kbps, CD, Stereo", fav: true },
      { name: "320 Kbps, CD, Stereo", fav: false },
    ],
    "MP3 Audio": [
      { name: "128 Kbps, Stereo, 44,100 Hz", fav: false },
      { name: "192 Kbps, Stereo, 44,100 Hz", fav: true },
      { name: "320 Kbps, Stereo, 44,100 Hz", fav: false },
    ],
    "Wave (Microsoft)": [
      { name: "44,100 Hz, 16 Bit, Stereo, PCM", fav: true },
      { name: "48,000 Hz, 16 Bit, Stereo, PCM", fav: false },
      { name: "48,000 Hz, 24 Bit, Stereo, PCM", fav: false },
    ],
    "Apple ProRes": [
      { name: "ProRes 422 HQ 1080p 29.97", fav: false },
      { name: "ProRes 422 1080p 29.97", fav: true },
      { name: "ProRes 422 LT 1080p 29.97", fav: false },
    ],
    "Image Sequence": [
      { name: "PNG Image Sequence", fav: true },
      { name: "TIFF Image Sequence", fav: false },
      { name: "JPEG Image Sequence", fav: false },
    ],
  };

  const EXT = {
    "AAC Audio": ".m4a",
    "AC-3": ".ac3",
    "Apple ProRes": ".mov",
    AIFF: ".aif",
    "FLAC Audio": ".flac",
    "Image Sequence": ".png",
    "AV1": ".mp4",
    "AVC/AAC MP4": ".mp4",
    "HEVC/AAC MP4": ".mp4",
    "MainConcept MPEG-1/2": ".mpg",
    "MP3 Audio": ".mp3",
    OggVorbis: ".ogg",
    "AVC/MVC": ".mp4",
    "MXF": ".mxf",
    "Wave64": ".w64",
    "XAVC / XAVC S": ".mxf",
    "Video for Windows": ".avi",
    "Wave (Microsoft)": ".wav",
    "Windows Media Video XAVC S": ".wmv",
    "Windows Media Video/Audio": ".wmv",
    "XDCAM EX": ".mp4",
    "XDCAM Transfer": ".mxf",
  };

  function templatesFor(format) {
    return TEMPLATES[format] || [{ name: "Default Template", fav: false }];
  }

  function searchIco() {
    return (
      '<svg class="ra-search__ico" viewBox="0 0 16 16" aria-hidden="true">' +
      '<circle cx="7" cy="7" r="4.5" fill="none" stroke="currentColor" stroke-width="1.4"/>' +
      '<path d="M10.5 10.5L14 14" stroke="currentColor" stroke-width="1.4"/>' +
      "</svg>"
    );
  }

  function buildMarkup() {
    return (
      '<div class="ra-dialog" role="dialog" aria-modal="true" aria-labelledby="ra-title">' +
      '<div class="ra-dialog__titlebar" data-ra-drag>' +
      '<span class="ra-dialog__title" id="ra-title">Render As</span>' +
      '<div class="ra-dialog__winbtns">' +
      '<button type="button" class="ra-winbtn ra-winbtn--close" data-ra-close title="Close" aria-label="Close">✕</button>' +
      "</div></div>" +
      '<div class="ra-dialog__body">' +
      '<div class="ra-toolbar">' +
      '<div class="ra-search">' +
      searchIco() +
      '<input type="search" data-ra-search placeholder="Search render templates" aria-label="Search render templates" />' +
      "</div>" +
      '<button type="button" class="ra-filters" data-ra-filters>Filters Off <span class="ra-filters__caret">▾</span></button>' +
      '<button type="button" class="ra-help" title="Help" aria-label="Help">?</button>' +
      "</div>" +
      '<div class="ra-split" data-ra-split>' +
      '<div class="ra-pane" data-ra-formats-pane>' +
      '<div class="ra-pane__head">Formats</div>' +
      '<ul class="ra-pane__list" data-ra-formats role="listbox" aria-label="Formats"></ul>' +
      "</div>" +
      '<div class="ra-splitter" data-ra-splitter title="Resize"></div>' +
      '<div class="ra-pane" data-ra-templates-pane>' +
      '<div class="ra-pane__head">Templates</div>' +
      '<ul class="ra-pane__list" data-ra-templates role="listbox" aria-label="Templates"></ul>' +
      "</div>" +
      "</div>" +
      '<div class="ra-info">' +
      '<div class="ra-info__col"><div class="ra-info__label">Template Info:</div><div class="ra-info__text" data-ra-info-left></div></div>' +
      '<div class="ra-info__col"><div class="ra-info__label">Template Info:</div><div class="ra-info__text" data-ra-info-right></div></div>' +
      "</div>" +
      "</div>" +
      '<div class="ra-dialog__footer">' +
      '<div class="ra-form">' +
      '<div class="ra-form__opts"><button type="button" class="ra-btn ra-btn--menu" data-ra-options>Render Options <span class="ra-filters__caret">▾</span></button></div>' +
      '<label for="ra-folder">Folder:</label>' +
      '<div class="ra-combo"><input id="ra-folder" type="text" value="C:\\Users\\Admin\\Desktop" data-ra-folder /><button type="button" class="ra-combo__caret" tabindex="-1" aria-label="Recent folders">▾</button></div>' +
      '<button type="button" class="ra-btn" data-ra-browse>Browse...</button>' +
      '<label for="ra-name">Name:</label>' +
      '<input id="ra-name" class="ra-field" type="text" data-ra-name value="example_project_with_fades_and_crossfades.mp4" />' +
      "<span></span>" +
      '<p class="ra-free">Free disk space: 640 GB</p>' +
      "</div>" +
      '<div class="ra-side">' +
      '<button type="button" class="ra-btn" data-ra-customize disabled>Customize Template...</button>' +
      '<button type="button" class="ra-btn" data-ra-about>About...</button>' +
      '<button type="button" class="ra-btn" data-ra-project-loc>Project Location</button>' +
      '<div class="ra-side__actions">' +
      '<button type="button" class="ra-btn ra-btn--primary" data-ra-render>Render</button>' +
      '<button type="button" class="ra-btn" data-ra-close>Cancel</button>' +
      "</div></div></div></div>"
    );
  }

  function ensure() {
    let root = document.getElementById(WIN_ID);
    if (root) return root;
    root = document.createElement("div");
    root.id = WIN_ID;
    root.className = "ra-backdrop";
    root.hidden = true;
    root.innerHTML = buildMarkup();
    document.body.appendChild(root);
    wire(root);
    return root;
  }

  function state(root) {
    if (!root._ra) {
      root._ra = {
        format: DEFAULT_FORMAT,
        template: DEFAULT_TEMPLATE,
        query: "",
        favoritesOnly: false,
      };
    }
    return root._ra;
  }

  function renderFormats(root) {
    const list = root.querySelector("[data-ra-formats]");
    const s = state(root);
    const q = s.query.trim().toLowerCase();
    list.innerHTML = FORMATS.map((name) => {
      const templates = templatesFor(name);
      const matchSelf = !q || name.toLowerCase().includes(q);
      const matchTpl = q && templates.some((t) => t.name.toLowerCase().includes(q));
      if (q && !matchSelf && !matchTpl) return "";
      const sel = name === s.format ? " is-selected" : "";
      return (
        '<li class="ra-item' +
        sel +
        '" role="option" aria-selected="' +
        (name === s.format) +
        '" data-format="' +
        escapeAttr(name) +
        '"><span class="ra-item__label">' +
        escapeHtml(name) +
        "</span></li>"
      );
    }).join("");
  }

  function renderTemplates(root) {
    const list = root.querySelector("[data-ra-templates]");
    const s = state(root);
    const q = s.query.trim().toLowerCase();
    const items = templatesFor(s.format);
    list.innerHTML = items
      .map((t) => {
        if (q && !t.name.toLowerCase().includes(q) && !s.format.toLowerCase().includes(q)) {
          return "";
        }
        if (s.favoritesOnly && !t.fav) return "";
        const sel = t.name === s.template ? " is-selected" : "";
        const starOn = t.fav ? " is-on" : "";
        const mark = t.mark ? '<span class="ra-item__mark">' + t.mark + "</span>" : '<span class="ra-item__mark"></span>';
        return (
          '<li class="ra-item' +
          sel +
          '" role="option" aria-selected="' +
          (t.name === s.template) +
          '" data-template="' +
          escapeAttr(t.name) +
          '">' +
          mark +
          '<button type="button" class="ra-star' +
          starOn +
          '" title="Favorite" aria-label="Favorite">' +
          (t.fav ? "★" : "☆") +
          "</button>" +
          '<span class="ra-item__label">' +
          escapeHtml(t.name) +
          "</span></li>"
        );
      })
      .join("");

    updateInfo(root);
    updateNameExt(root);
    const custom = root.querySelector("[data-ra-customize]");
    if (custom) custom.disabled = true;
  }

  function updateInfo(root) {
    const s = state(root);
    const left = root.querySelector("[data-ra-info-left]");
    const right = root.querySelector("[data-ra-info-right]");
    if (left) {
      left.textContent = s.format + (s.template ? " · " + s.template : "");
    }
    if (right) {
      const ext = EXT[s.format] || ".mp4";
      right.textContent = "Output extension: " + ext;
    }
  }

  function updateNameExt(root) {
    const input = root.querySelector("[data-ra-name]");
    const s = state(root);
    if (!input) return;
    const ext = EXT[s.format] || ".mp4";
    const base = input.value.replace(/\.[^.]+$/, "") || "Untitled";
    if (!input.dataset.userEdited) {
      input.value = base + ext;
    }
  }

  function selectFormat(root, name) {
    const s = state(root);
    s.format = name;
    const tpls = templatesFor(name);
    const fav = tpls.find((t) => t.fav);
    s.template = (fav || tpls[0] || { name: "Default Template" }).name;
    renderFormats(root);
    renderTemplates(root);
  }

  function selectTemplate(root, name) {
    state(root).template = name;
    renderTemplates(root);
  }

  function escapeHtml(s) {
    return String(s)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  function escapeAttr(s) {
    return escapeHtml(s).replace(/"/g, "&quot;");
  }

  function wire(root) {
    const dialog = root.querySelector(".ra-dialog");

    root.querySelectorAll("[data-ra-close]").forEach((btn) => {
      btn.addEventListener("click", () => close());
    });

    root.querySelector("[data-ra-render]")?.addEventListener("click", () => close());

    root.addEventListener("mousedown", (e) => {
      if (e.target === root) close();
    });

    root.querySelector("[data-ra-formats]")?.addEventListener("click", (e) => {
      const item = e.target.closest("[data-format]");
      if (!item) return;
      selectFormat(root, item.getAttribute("data-format"));
    });

    root.querySelector("[data-ra-templates]")?.addEventListener("click", (e) => {
      const star = e.target.closest(".ra-star");
      if (star) {
        e.stopPropagation();
        const item = star.closest("[data-template]");
        const name = item?.getAttribute("data-template");
        const list = templatesFor(state(root).format);
        const t = list.find((x) => x.name === name);
        if (t) {
          t.fav = !t.fav;
          renderTemplates(root);
        }
        return;
      }
      const item = e.target.closest("[data-template]");
      if (!item) return;
      selectTemplate(root, item.getAttribute("data-template"));
    });

    const search = root.querySelector("[data-ra-search]");
    search?.addEventListener("input", () => {
      state(root).query = search.value || "";
      renderFormats(root);
      renderTemplates(root);
    });

    root.querySelector("[data-ra-filters]")?.addEventListener("click", (e) => {
      const s = state(root);
      s.favoritesOnly = !s.favoritesOnly;
      e.currentTarget.innerHTML =
        (s.favoritesOnly ? "Favorites" : "Filters Off") +
        ' <span class="ra-filters__caret">▾</span>';
      renderTemplates(root);
    });

    const nameInput = root.querySelector("[data-ra-name]");
    nameInput?.addEventListener("input", () => {
      nameInput.dataset.userEdited = "1";
    });

    root.querySelector("[data-ra-project-loc]")?.addEventListener("click", () => {
      const folder = root.querySelector("[data-ra-folder]");
      if (folder) folder.value = "C:\\Users\\Admin\\Documents";
    });

    // Splitter drag
    const split = root.querySelector("[data-ra-split]");
    const splitter = root.querySelector("[data-ra-splitter]");
    if (split && splitter) {
      let dragging = false;
      splitter.addEventListener("mousedown", (e) => {
        dragging = true;
        splitter.classList.add("is-dragging");
        e.preventDefault();
      });
      window.addEventListener("mousemove", (e) => {
        if (!dragging) return;
        const r = split.getBoundingClientRect();
        const x = e.clientX - r.left;
        const left = Math.min(Math.max(x, 160), r.width - 220);
        split.style.gridTemplateColumns = left + "px 5px 1fr";
      });
      window.addEventListener("mouseup", () => {
        dragging = false;
        splitter.classList.remove("is-dragging");
      });
    }

    // Titlebar drag
    const drag = root.querySelector("[data-ra-drag]");
    if (drag && dialog) {
      let ox = 0;
      let oy = 0;
      let dragging = false;
      drag.addEventListener("mousedown", (e) => {
        if (e.target.closest(".ra-winbtn")) return;
        dragging = true;
        const r = dialog.getBoundingClientRect();
        dialog.style.position = "fixed";
        dialog.style.margin = "0";
        dialog.style.left = r.left + "px";
        dialog.style.top = r.top + "px";
        ox = e.clientX - r.left;
        oy = e.clientY - r.top;
        e.preventDefault();
      });
      window.addEventListener("mousemove", (e) => {
        if (!dragging) return;
        dialog.style.left = Math.max(0, e.clientX - ox) + "px";
        dialog.style.top = Math.max(0, e.clientY - oy) + "px";
      });
      window.addEventListener("mouseup", () => {
        dragging = false;
      });
    }
  }

  function open() {
    const root = ensure();
    const s = state(root);
    s.format = DEFAULT_FORMAT;
    s.template = DEFAULT_TEMPLATE;
    s.query = "";
    s.favoritesOnly = false;
    const search = root.querySelector("[data-ra-search]");
    if (search) search.value = "";
    const filters = root.querySelector("[data-ra-filters]");
    if (filters) filters.innerHTML = 'Filters Off <span class="ra-filters__caret">▾</span>';
    const name = root.querySelector("[data-ra-name]");
    if (name) {
      delete name.dataset.userEdited;
      name.value = "example_project_with_fades_and_crossfades.mp4";
    }
    renderFormats(root);
    renderTemplates(root);
    root.hidden = false;
    root.classList.add("is-open");
    setTimeout(() => {
      name?.focus();
      name?.select();
    }, 0);
  }

  function close() {
    const root = document.getElementById(WIN_ID);
    if (!root) return;
    root.classList.remove("is-open");
    root.hidden = true;
  }

  function isOpen() {
    const root = document.getElementById(WIN_ID);
    return !!(root && root.classList.contains("is-open"));
  }

  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && isOpen()) {
      e.preventDefault();
      close();
      return;
    }
    if (e.ctrlKey && e.shiftKey && (e.key === "M" || e.key === "m")) {
      e.preventDefault();
      open();
    }
  });

  document.addEventListener("click", (e) => {
    const tb = e.target.closest('.icon-btn[title="Render As"]');
    if (tb) {
      e.preventDefault();
      open();
      return;
    }
    const item = e.target.closest(".dropdown-menu__item");
    if (!item || item.classList.contains("is-disabled")) return;
    const label = (item.querySelector(".label")?.textContent || "").trim();
    const action = item.getAttribute("data-action") || "";
    if (action === "render-as" || label === "Render As...") {
      e.stopPropagation();
      open();
    }
  });

  document.addEventListener("vegas:menu-action", (e) => {
    const action = e.detail?.action || "";
    const label = e.detail?.label || "";
    if (action === "render-as" || label === "Render As...") open();
  });

  window.VegasRenderAs = { open, close, isOpen };

  if (document.body?.dataset.raOpen === "1") {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", () => open());
    } else {
      open();
    }
  }
})();
