/**
 * Builds the UI reference catalog: menu bar, context menus, Welcome panes,
 * preview dropdowns — all expanded for static snapshots / Qt port.
 */
(function () {
  function esc(s) {
    return String(s || "")
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;");
  }

  /** Walk menu tree → [{ path, items }] including root and every submenu. */
  function flattenMenus(items, path) {
    const out = [{ path: path.slice(), items: items }];
    (items || []).forEach((it) => {
      if (it.sep || !Array.isArray(it.submenu) || !it.submenu.length) return;
      const label = (it.label || "").replace(/<[^>]+>/g, "");
      out.push(...flattenMenus(it.submenu, path.concat(label)));
    });
    return out;
  }

  function menuCard(title, pathKey, itemsHtml, extraClass) {
    const cls = "ui-catalog-card" + (extraClass ? " " + extraClass : "");
    const meta = pathKey ? '<p class="ui-catalog-card__meta">' + esc(pathKey) + "</p>" : "";
    return (
      '<article class="' +
      cls +
      '"><h3>' +
      esc(title) +
      "</h3>" +
      meta +
      itemsHtml +
      "</article>"
    );
  }

  function renderMenuTree(title, rootKey, items, asContext) {
    const flat = flattenMenus(items, [rootKey || title]);
    const build = window.VegasMenus?.buildMenuItems;
    if (!build) return "";

    return flat
      .map((node, idx) => {
        const pathLabel = node.path.join(" → ");
        const isRoot = idx === 0;
        const wrapClass = asContext
          ? "context-menu is-open" +
            (rootKey === "video-event" || rootKey === "audio-event"
              ? " context-menu--event"
              : rootKey === "timeline-empty"
                ? " context-menu--compact"
                : "")
          : "dropdown-menu is-open" + (rootKey === "File" && isRoot ? " dropdown-menu--file" : "");
        const html =
          '<div class="' + wrapClass + '">' + build(node.items) + "</div>";
        // Mark all submenus open for CSS
        const tmp = document.createElement("div");
        tmp.innerHTML = html;
        tmp.querySelectorAll(".has-submenu").forEach((el) => el.classList.add("is-open"));
        tmp.querySelectorAll(".context-submenu").forEach((el) => el.classList.add("is-visible"));
        return menuCard(isRoot ? title : pathLabel, pathLabel, tmp.innerHTML);
      })
      .join("");
  }

  function section(id, heading, bodyHtml) {
    return (
      '<section id="' +
      id +
      '"><h2>' +
      esc(heading) +
      "</h2><div class=\"ui-catalog-grid\">" +
      bodyHtml +
      "</div></section>"
    );
  }

  function buildWelcomeSection(sourceSel) {
    const source = document.querySelector(sourceSel);
    if (!source) {
      return (
        '<section id="welcome"><h2>Welcome — все вкладки</h2>' +
        '<p class="lead">Источник Welcome не найден.</p></section>'
      );
    }

    const panes = Array.from(source.querySelectorAll(".welcome-pane"));
    const nav = Array.from(source.querySelectorAll(".welcome-nav-btn"));

    return (
      '<section id="welcome"><h2>Welcome — все вкладки</h2>' +
      '<div class="ui-catalog-welcome">' +
      panes
        .map((pane, i) => {
          const name = pane.dataset.welcomePane || "pane-" + i;
          const navBtn = nav.find((b) => b.dataset.welcome === name);
          const title =
            (navBtn?.textContent || name).replace(/\s+/g, " ").trim() || name;
          const clone = pane.cloneNode(true);
          clone.classList.add("is-active");
          return (
            "<div><h3>" +
            esc(title) +
            '</h3><p class="ui-catalog-card__meta">data-welcome-pane="' +
            esc(name) +
            '"</p>' +
            clone.outerHTML +
            "</div>"
          );
        })
        .join("") +
      "</div></section>"
    );
  }

  function buildPreviewSection() {
    // Prefer live chrome after fill; fall back to empty note.
    const dds = Array.from(
      document.querySelectorAll(
        ".preview-panel .preview-dd, [data-chrome='preview-toolbar'] .preview-dd"
      )
    );
    if (!dds.length) {
      return section(
        "preview-dd",
        "Preview toolbar — выпадающие меню",
        menuCard(
          "нет данных",
          "preview-dd",
          '<p class="ui-catalog-card__meta">Toolbar ещё не заполнен chrome.js</p>'
        )
      );
    }

    const cards = dds
      .map((dd) => {
        const key = dd.dataset.dd || "dd";
        const btn = dd.querySelector(".preview-dd__btn");
        const title =
          btn?.getAttribute("title") ||
          btn?.querySelector(".preview-dd__value")?.textContent?.trim() ||
          key;
        const clone = dd.cloneNode(true);
        clone.classList.add("is-open");
        const menu = clone.querySelector(".preview-dd__menu");
        if (menu) {
          menu.hidden = false;
          menu.removeAttribute("hidden");
        }
        clone.querySelectorAll(".has-submenu").forEach((el) => el.classList.add("is-open"));
        clone.querySelectorAll(".preview-dd__flyout").forEach((el) => {
          el.style.display = "block";
        });
        return menuCard("Preview → " + title, "preview-dd:" + key, clone.outerHTML);
      })
      .join("");

    return section("preview-dd", "Preview toolbar — выпадающие меню", cards);
  }

  function toc(links) {
    return (
      '<nav class="ui-catalog-toc" aria-label="Разделы">' +
      links
        .map((l) => '<a href="#' + l.id + '">' + esc(l.label) + "</a>")
        .join("") +
      "</nav>"
    );
  }

  function build() {
    const root = document.getElementById("ui-catalog-root");
    if (!root || !window.VegasMenus) return;

    const { MENU_DATA, CONTEXT_MENUS } = window.VegasMenus;
    const menuOrder = ["File", "Edit", "View", "Insert", "Tools", "Options", "Help"];

    let menuHtml = "";
    menuOrder.forEach((name) => {
      if (!MENU_DATA[name]) return;
      menuHtml += renderMenuTree("Menu · " + name, name, MENU_DATA[name], false);
    });

    const ctxKeys = Object.keys(CONTEXT_MENUS).sort();
    let ctxHtml = "";
    ctxKeys.forEach((key) => {
      ctxHtml += renderMenuTree("Context · " + key, key, CONTEXT_MENUS[key], true);
    });

    const links = [
      { id: "menubar", label: "Menu bar" },
      { id: "context", label: "Context menus" },
      { id: "welcome", label: "Welcome" },
      { id: "preview-dd", label: "Preview dropdowns" },
    ];

    root.innerHTML =
      toc(links) +
      section(
        "menubar",
        "Menu bar — File / Edit / View / Insert / Tools / Options / Help",
        menuHtml
      ) +
      section("context", "Context menus (все типы data-context)", ctxHtml) +
      buildWelcomeSection(".welcome-overlay") +
      buildPreviewSection();

    root.querySelectorAll(".has-submenu").forEach((el) => el.classList.add("is-open"));
    root.querySelectorAll(".context-submenu").forEach((el) => el.classList.add("is-visible"));
  }

  document.addEventListener("DOMContentLoaded", () => {
    // chrome.js fills preview toolbar on DOMContentLoaded — run after a tick
    requestAnimationFrame(() => {
      setTimeout(build, 50);
    });
  });

  window.VegasUiCatalog = { build };
})();
