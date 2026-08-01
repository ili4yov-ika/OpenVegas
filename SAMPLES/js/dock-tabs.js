/**
 * Fill dock tab panels: Explorer, Video FX, Media Generators, Transitions, Project Notes.
 */
(function () {
  function star() {
    return '<span class="plug-star" title="Favorite">☆</span>';
  }

  function listItem(label, selected) {
    return (
      '<div class="plug-item' +
      (selected ? " is-selected" : "") +
      '">' +
      star() +
      "<span>" +
      label +
      "</span></div>"
    );
  }

  function presetCard(label, bg) {
    return (
      '<div class="preset-card">' +
      '<div class="preset-card__thumb" style="background:' +
      bg +
      '"></div>' +
      '<div class="preset-card__name">' +
      label +
      "</div></div>"
    );
  }

  const EXPLORER = `
<div class="explorer-pane">
  <div class="explorer-toolbar">
    <button type="button" class="icon-btn icon-btn--sm" title="Back">◀</button>
    <button type="button" class="icon-btn icon-btn--sm" title="Forward">▶</button>
    <button type="button" class="icon-btn icon-btn--sm" title="Up">⬆</button>
    <button type="button" class="icon-btn icon-btn--sm" title="Refresh">⟳</button>
    <span class="toolbar-sep"></span>
    <div class="explorer-path">
      <span>Computer</span><span class="sep">›</span>
      <span>Data (D:)</span><span class="sep">›</span>
      <span>Files</span><span class="sep">›</span>
      <span class="is-current">Видео и фильмы</span>
    </div>
  </div>
  <div class="explorer-body">
    <div class="explorer-tree">
      <div class="tree-item"><span class="arrow">▸</span><span class="tree-ico tree-ico--fav"></span> Favorites</div>
      <div class="tree-item"><span class="arrow">▸</span><span class="tree-ico"></span> Recent Places</div>
      <div class="tree-item"><span class="arrow">▸</span><span class="tree-ico"></span> Desktop</div>
      <div class="tree-item"><span class="arrow">▸</span><span class="tree-ico"></span> Libraries</div>
      <div class="tree-item"><span class="arrow">▾</span><span class="tree-ico tree-ico--pc"></span> Computer</div>
      <div class="tree-item tree-item--nested"><span class="arrow">▸</span> Local Disk (C:)</div>
      <div class="tree-item tree-item--nested"><span class="arrow">▾</span> Data (D:)</div>
      <div class="tree-item" style="padding-left:40px"><span class="arrow">▾</span><span class="tree-ico"></span> Files</div>
      <div class="tree-item is-selected" style="padding-left:52px"><span class="tree-ico"></span> Видео и фильмы</div>
      <div class="tree-item" style="padding-left:52px"><span class="tree-ico"></span> Documents</div>
      <div class="tree-item" style="padding-left:52px"><span class="tree-ico"></span> Downloads</div>
    </div>
    <div class="explorer-grid">
      <div class="explorer-item explorer-item--folder"><div class="explorer-item__ico"></div><span>Youtube</span></div>
      <div class="explorer-item explorer-item--folder"><div class="explorer-item__ico"></div><span>АРХИВ</span></div>
      <div class="explorer-item explorer-item--folder"><div class="explorer-item__ico"></div><span>Renders</span></div>
      <div class="explorer-item explorer-item--video">
        <div class="explorer-item__thumb" style="background:linear-gradient(135deg,#334,#556)"></div>
        <span>sample_for_project_video.mp4</span>
      </div>
      <div class="explorer-item explorer-item--video">
        <div class="explorer-item__thumb" style="background:linear-gradient(135deg,#453,#675)"></div>
        <span>Donnie Darko 2001.mp4</span>
      </div>
      <div class="explorer-item explorer-item--video">
        <div class="explorer-item__thumb" style="background:linear-gradient(135deg,#345,#467)"></div>
        <span>clip_street.mp4</span>
      </div>
    </div>
  </div>
</div>`;

  const VIDEO_FX = `
<div class="fx-pane">
  <div class="fx-toolbar">
    <div class="fx-search"><span class="fx-search__ico">⌕</span><input type="search" placeholder="Search..." /></div>
  </div>
  <div class="fx-chips">
    <button type="button" class="fx-chip is-active">All Plug-ins</button>
    <button type="button" class="fx-chip">AI/ML</button>
    <button type="button" class="fx-chip">Creative</button>
    <button type="button" class="fx-chip">Color</button>
    <button type="button" class="fx-chip">Utility</button>
    <button type="button" class="fx-chip">Blur</button>
    <button type="button" class="fx-chip">360°</button>
    <button type="button" class="fx-chip">Third Party</button>
    <button type="button" class="fx-chip fx-chip--star">★</button>
  </div>
  <div class="fx-body">
    <div class="plug-list">
      ${listItem("360 Stabilization")}
      ${listItem("Add Noise")}
      ${listItem("AI Auto Reframe")}
      ${listItem("AI Colorization")}
      ${listItem("AI Dehaze")}
      ${listItem("AI Sharpen")}
      ${listItem("AI Smart Mask 2.0")}
      ${listItem("AI Smoothen")}
      ${listItem("AI Style Transfer", true)}
      ${listItem("AI Upscale")}
      ${listItem("AI Z-Depth")}
      ${listItem("AutoLooks")}
      ${listItem("Bézier Masking")}
      ${listItem("Black and White")}
      ${listItem("Black Bar Fill")}
      ${listItem("Brightness and Contrast")}
      ${listItem("Channel Blend")}
      ${listItem("Color Corrector")}
      ${listItem("Gaussian Blur")}
    </div>
    <div class="fx-main">
      <div class="preset-grid">
        ${presetCard("(Default)", "linear-gradient(135deg,#2a2030,#5a4060)")}
        ${presetCard("Self-Portrait (Picasso)", "linear-gradient(135deg,#6a4020,#c08040)")}
        ${presetCard("Night Alley Walk", "linear-gradient(135deg,#102040,#3060a0)")}
        ${presetCard("The Great Wave", "linear-gradient(135deg,#1a4060,#80b0d0)")}
        ${presetCard("Fruit (Hunt)", "linear-gradient(135deg,#603010,#c06030)")}
        ${presetCard("Light (Kheron)", "linear-gradient(135deg,#504020,#e0c080)")}
        ${presetCard("Floral pattern", "linear-gradient(135deg,#402050,#a06090)")}
        ${presetCard("Sunrise Flowers", "linear-gradient(135deg,#804020,#e0a050)")}
        ${presetCard("Abstract Painting", "linear-gradient(135deg,#203060,#8060a0)")}
        ${presetCard("Black and White", "linear-gradient(135deg,#222,#888)")}
        ${presetCard("The Starry Night", "linear-gradient(135deg,#102050,#4060c0)")}
        ${presetCard("The Weeping Woman", "linear-gradient(135deg,#603020,#c07050)")}
        ${presetCard("Bark pattern", "linear-gradient(135deg,#3a2818,#8a6040)")}
        ${presetCard("Leaf pattern", "linear-gradient(135deg,#1a4020,#50a040)")}
        ${presetCard("Rick and Morty", "linear-gradient(135deg,#206040,#60c080)")}
        ${presetCard("Candy", "linear-gradient(135deg,#802060,#e080c0)")}
        ${presetCard("Mosaic", "linear-gradient(135deg,#404060,#a0a0c0)")}
        ${presetCard("Pointillism", "radial-gradient(circle,#c0a060,#403020)")}
        ${presetCard("Rain Princess", "linear-gradient(135deg,#204060,#80a0c0)")}
        ${presetCard("Udnie (Picasso)", "linear-gradient(135deg,#602040,#c06080)")}
        ${presetCard("Scream (Munch)", "linear-gradient(135deg,#806040,#e0c080)")}
        ${presetCard("Simpsons", "linear-gradient(135deg,#c0a020,#4060c0)")}
      </div>
      <div class="fx-meta">
        <div>OpenVegas AI Style Transfer: OFX, 32-bit floating point, Grouping: OpenVegas/AI, Version 1.0</div>
        <div>Description: Transforming the appearance of famous paintings to user-supplied clips.</div>
      </div>
    </div>
  </div>
</div>`;

  const MEDIA_GEN = `
<div class="fx-pane">
  <div class="fx-toolbar">
    <div class="fx-search"><span class="fx-search__ico">⌕</span><input type="search" placeholder="Search..." /></div>
  </div>
  <div class="fx-chips">
    <button type="button" class="fx-chip is-active">All Plug-ins</button>
    <button type="button" class="fx-chip">Creative</button>
    <button type="button" class="fx-chip">Titles and Text</button>
    <button type="button" class="fx-chip">Utility</button>
    <button type="button" class="fx-chip fx-chip--star">★</button>
  </div>
  <div class="fx-body">
    <div class="plug-list">
      ${listItem("Checkerboard", true)}
      ${listItem("Color Gradient")}
      ${listItem("Credit Roll")}
      ${listItem("Noise Texture")}
      ${listItem("Solid Color")}
      ${listItem("Test Pattern")}
      ${listItem("Titles & Text")}
    </div>
    <div class="fx-main">
      <div class="preset-grid">
        ${presetCard("(Default)", "repeating-conic-gradient(#111 0% 25%, #eee 0% 50%) 0 0/24px 24px")}
        ${presetCard("Large Tiles", "repeating-conic-gradient(#111 0% 25%, #eee 0% 50%) 0 0/40px 40px")}
        ${presetCard("Small Tiles", "repeating-conic-gradient(#111 0% 25%, #eee 0% 50%) 0 0/12px 12px")}
        ${presetCard("Horizontal Blinds", "repeating-linear-gradient(#111 0 6px,#eee 6px 12px)")}
        ${presetCard("Vertical Blinds", "repeating-linear-gradient(90deg,#111 0 6px,#eee 6px 12px)")}
        ${presetCard("Grille", "repeating-linear-gradient(90deg,#222 0 2px,#ccc 2px 8px)")}
        ${presetCard("Fence", "repeating-linear-gradient(#333 0 3px,#aaa 3px 10px)")}
        ${presetCard("Ridges", "repeating-linear-gradient(90deg,#1a1a1a,#888 50%,#1a1a1a)")}
        ${presetCard("Bumps", "radial-gradient(circle at 30% 30%,#ccc,#222 60%)")}
        ${presetCard("Plaid", "repeating-linear-gradient(90deg,#204060 0 8px,#802040 8px 16px)")}
        ${presetCard("Letterbox", "linear-gradient(#000 18%,#ccc 18% 82%,#000 82%)")}
        ${presetCard("Split Screen", "linear-gradient(90deg,#2060a0 50%,#a04020 50%)")}
        ${presetCard("Horizon", "linear-gradient(#4060a0 45%,#c08040 55%)")}
      </div>
      <div class="fx-meta">
        <div>OpenVegas Checkerboard: OFX, 32-bit floating point, GPU Accelerated, Grouping: OpenVegas, Version 1.0</div>
        <div>Description: From OpenVegas sample UI.</div>
      </div>
    </div>
  </div>
</div>`;

  const TRANSITIONS = `
<div class="fx-pane">
  <div class="fx-toolbar">
    <div class="fx-search"><span class="fx-search__ico">⌕</span><input type="search" placeholder="Search..." /></div>
  </div>
  <div class="fx-chips">
    <button type="button" class="fx-chip is-active">All Plug-ins</button>
    <button type="button" class="fx-chip">3D Effects</button>
    <button type="button" class="fx-chip">Barn Door</button>
    <button type="button" class="fx-chip">Wipes</button>
    <button type="button" class="fx-chip">Fades</button>
    <button type="button" class="fx-chip">Loops and Peels</button>
    <button type="button" class="fx-chip fx-chip--star">★</button>
  </div>
  <div class="fx-body">
    <div class="plug-list">
      ${listItem("3D Blinds", true)}
      ${listItem("3D Cascade")}
      ${listItem("3D Fly In/Out")}
      ${listItem("3D Shuffle")}
      ${listItem("Barn Door")}
      ${listItem("Clock Wipe")}
      ${listItem("Cross Effect")}
      ${listItem("Dissolve")}
      ${listItem("Flash")}
      ${listItem("GL Transition")}
      ${listItem("Gradient Wipe")}
      ${listItem("Iris")}
      ${listItem("Linear Wipe")}
      ${listItem("Page Loop")}
      ${listItem("Page Peel")}
      ${listItem("Page Roll")}
      ${listItem("Push")}
      ${listItem("Slide")}
      ${listItem("Spiral")}
      ${listItem("Split")}
      ${listItem("Squeeze")}
      ${listItem("Star Wipe")}
      ${listItem("Swap")}
      ${listItem("Venetian Blinds")}
      ${listItem("Warp Flow")}
      ${listItem("Zoom")}
    </div>
    <div class="fx-main">
      <div class="preset-grid preset-grid--wide">
        ${presetCard("Simple", "linear-gradient(135deg,#1a4a8a 40%,#c0d0e0 40% 55%,#1a4a8a 55%)")}
        ${presetCard("Left to Right", "linear-gradient(90deg,#1a4a8a 45%,#e8eef5 45% 55%,#1a4a8a 55%)")}
        ${presetCard("Slot Machine", "repeating-linear-gradient(90deg,#1a4a8a 0 18px,#d0d8e0 18px 28px)")}
        ${presetCard("Spin", "conic-gradient(from 30deg,#1a4a8a 0 40%,#d8e0ea 40% 55%,#1a4a8a 55%)")}
      </div>
      <div class="fx-meta">
        <div>OpenVegas 3D Blinds: OFX, 32-bit floating point</div>
      </div>
    </div>
  </div>
</div>`;

  const NOTES = `
<div class="notes-pane">
  <div class="notes-header">
    <label class="notes-check"><input type="checkbox" /> Hide resolved notes</label>
    <div class="notes-header__actions">
      <button type="button" class="icon-btn icon-btn--sm" title="Search">⌕</button>
      <button type="button" class="icon-btn icon-btn--sm" title="Pin">📌</button>
      <button type="button" class="icon-btn icon-btn--sm" title="Refresh">⟳</button>
      <span class="notes-tc">0;0.000</span>
      <button type="button" class="icon-btn icon-btn--sm notes-warn" title="Warning">⚠</button>
      <button type="button" class="icon-btn icon-btn--sm notes-del" title="Delete">✕</button>
      <button type="button" class="icon-btn icon-btn--sm" title="More">⋮</button>
    </div>
  </div>
  <div class="notes-list">
    <article class="note-card">
      <header class="note-card__head note-card__head--purple">
        <div>
          <div class="note-card__title">Название заметки</div>
          <div class="note-card__date">Monday, July 27, 2026 7:06:16 AM</div>
        </div>
        <div class="note-card__actions">
          <span class="notes-tc">0;0.000</span>
          <button type="button" class="icon-btn icon-btn--sm notes-warn">⚠</button>
          <button type="button" class="icon-btn icon-btn--sm notes-del">✕</button>
          <button type="button" class="icon-btn icon-btn--sm">⋮</button>
        </div>
      </header>
      <div class="note-card__body">Текст заметки длинный</div>
    </article>
    <article class="note-card">
      <header class="note-card__head note-card__head--blue">
        <div>
          <div class="note-card__title">Ещё одна</div>
          <div class="note-card__date">Monday, July 27, 2026 7:06:56 AM</div>
        </div>
        <div class="note-card__actions">
          <span class="notes-tc">0;0.000</span>
          <button type="button" class="icon-btn icon-btn--sm notes-warn">⚠</button>
          <button type="button" class="icon-btn icon-btn--sm notes-del">✕</button>
          <button type="button" class="icon-btn icon-btn--sm">⋮</button>
        </div>
      </header>
      <div class="note-card__body note-card__body--edit" contenteditable="true">ещё текст</div>
    </article>
  </div>
  <div class="notes-footer">
    <input type="text" class="notes-label" placeholder="Enter default label" />
    <button type="button" class="btn btn--primary notes-add">+ Add new note</button>
  </div>
</div>`;

  const MAP = {
    explorer: EXPLORER,
    "video-fx": VIDEO_FX,
    "media-generators": MEDIA_GEN,
    transitions: TRANSITIONS,
    "project-notes": NOTES,
  };

  function fillPanels() {
    Object.keys(MAP).forEach((key) => {
      document.querySelectorAll('.tab-panel[data-panel="' + key + '"]').forEach((panel) => {
        if (panel.dataset.dockFilled) return;
        panel.dataset.dockFilled = "1";
        panel.innerHTML = MAP[key];
      });
    });
  }

  function syncMediaToolbar(activeTab) {
    document.querySelectorAll('[data-chrome="media-toolbar"]').forEach((tb) => {
      tb.style.display = activeTab === "project-media" ? "" : "none";
    });
  }

  function enhanceTabSwitch() {
    document.querySelectorAll("[data-tabs]").forEach((root) => {
      const tabs = root.querySelectorAll(".panel-tab");
      tabs.forEach((tab) => {
        tab.addEventListener("click", () => {
          syncMediaToolbar(tab.dataset.tab);
        });
      });
      const active = root.querySelector(".panel-tab.is-active");
      syncMediaToolbar(active ? active.dataset.tab : "project-media");
    });
  }

  function initFxInteraction() {
    document.addEventListener("click", (e) => {
      const item = e.target.closest(".plug-item");
      if (item) {
        const list = item.parentElement;
        list.querySelectorAll(".plug-item.is-selected").forEach((el) => el.classList.remove("is-selected"));
        item.classList.add("is-selected");
        return;
      }
      const chip = e.target.closest(".fx-chip");
      if (chip && chip.parentElement.classList.contains("fx-chips")) {
        chip.parentElement.querySelectorAll(".fx-chip").forEach((c) => c.classList.remove("is-active"));
        chip.classList.add("is-active");
      }
    });
  }

  document.addEventListener("DOMContentLoaded", () => {
    fillPanels();
    enhanceTabSwitch();
    initFxInteraction();
  });
})();
