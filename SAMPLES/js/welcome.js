/**
 * Welcome modal: New Project / Open Project / Getting Started.
 */
(function () {
  function activatePane(name) {
    document.querySelectorAll(".welcome-nav-btn").forEach((btn) => {
      btn.classList.toggle("is-active", btn.dataset.welcome === name);
    });
    document.querySelectorAll(".welcome-pane").forEach((pane) => {
      pane.classList.toggle("is-active", pane.dataset.welcomePane === name);
    });
  }

  function initAspectCards() {
    document.querySelectorAll(".aspect-card").forEach((card) => {
      card.addEventListener("click", () => {
        document.querySelectorAll(".aspect-card").forEach((c) => c.classList.remove("is-selected"));
        card.classList.add("is-selected");
      });
    });
  }

  function initGettingActions() {
    document.querySelectorAll(".getting-actions .btn").forEach((btn) => {
      btn.addEventListener("click", () => {
        document.querySelectorAll(".getting-actions .btn").forEach((b) => b.classList.remove("is-active"));
        btn.classList.add("is-active");
      });
    });
  }

  document.addEventListener("DOMContentLoaded", () => {
    document.querySelectorAll(".welcome-nav-btn").forEach((btn) => {
      btn.addEventListener("click", () => activatePane(btn.dataset.welcome));
    });

    const closeBtn = document.querySelector(".welcome-close");
    if (closeBtn) {
      closeBtn.addEventListener("click", () => {
        const overlay = document.querySelector(".welcome-overlay");
        if (overlay) overlay.style.display = "none";
      });
    }

    const createBtn = document.getElementById("btn-create-project");
    if (createBtn) {
      createBtn.addEventListener("click", () => {
        window.location.href = "empty-project.html";
      });
    }

    initAspectCards();
    initGettingActions();
  });
})();
