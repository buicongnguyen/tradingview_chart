(() => {
  "use strict";

  const storageKeys = {
    theme: "tradechart-book-theme",
    completed: "tradechart-book-completed",
    checklist: "tradechart-book-checklist",
  };

  const chapters = [...document.querySelectorAll(".chapter")];
  const navigationLinks = [...document.querySelectorAll("#chapter-nav a")];
  const chapterById = new Map(chapters.map((chapter) => [chapter.id, chapter]));
  const linkById = new Map(
    navigationLinks.map((link) => [link.hash.slice(1), link]),
  );
  const sidebar = document.querySelector("#sidebar");
  const menuButton = document.querySelector("#menu-button");
  const sidebarScrim = document.querySelector("#sidebar-scrim");
  const previousButton = document.querySelector("#previous-chapter");
  const nextButton = document.querySelector("#next-chapter");
  const completeButton = document.querySelector("#mark-complete");
  const searchInput = document.querySelector("#book-search");
  const searchResults = document.querySelector("#search-results");
  const readingProgress = document.querySelector("#reading-progress-bar");
  const completionBar = document.querySelector("#completion-bar");
  const completedCount = document.querySelector("#completed-count");
  const themeButton = document.querySelector("#theme-button");
  const resetButton = document.querySelector("#reset-progress");
  const mobileNavigation = window.matchMedia("(max-width: 1000px)");

  let activeIndex = 0;
  let completed = new Set(
    [...readSet(storageKeys.completed)].filter((id) => chapterById.has(id)),
  );
  let checklist = readSet(storageKeys.checklist);

  function readSet(key) {
    try {
      const value = JSON.parse(localStorage.getItem(key) || "[]");
      return new Set(Array.isArray(value) ? value.filter((item) => typeof item === "string") : []);
    } catch {
      return new Set();
    }
  }

  function writeSet(key, values) {
    try {
      localStorage.setItem(key, JSON.stringify([...values]));
    } catch {
      // The guide remains usable when storage is disabled.
    }
  }

  function rawChapterId() {
    try {
      return decodeURIComponent(location.hash.slice(1));
    } catch {
      return "";
    }
  }

  function requestedChapterId() {
    const id = rawChapterId();
    return chapterById.has(id) ? id : chapters[0].id;
  }

  function activateChapter(id, options = {}) {
    const chapter = chapterById.get(id) || chapters[0];
    activeIndex = chapters.indexOf(chapter);

    chapters.forEach((item) => {
      item.classList.toggle("active", item === chapter);
      item.setAttribute("aria-hidden", item === chapter ? "false" : "true");
    });
    navigationLinks.forEach((link) => {
      const active = link.hash === `#${chapter.id}`;
      link.classList.toggle("active", active);
      if (active) {
        link.setAttribute("aria-current", "page");
      } else {
        link.removeAttribute("aria-current");
      }
    });

    document.title = `${chapter.dataset.title} — Building TradeChart Lab`;
    updatePaging();
    updateProgress();
    closeSidebar();

    if (!options.preserveScroll) {
      window.scrollTo({ top: 0, behavior: options.instant ? "auto" : "smooth" });
    }
  }

  function navigateTo(index) {
    if (index < 0 || index >= chapters.length) {
      return;
    }
    location.hash = chapters[index].id;
  }

  function updatePaging() {
    const previous = chapters[activeIndex - 1];
    const next = chapters[activeIndex + 1];
    previousButton.disabled = !previous;
    nextButton.disabled = !next;
    previousButton.querySelector("strong").textContent = previous?.dataset.title || "";
    nextButton.querySelector("strong").textContent = next?.dataset.title || "";

    const currentId = chapters[activeIndex].id;
    const done = completed.has(currentId);
    completeButton.classList.toggle("done", done);
    completeButton.setAttribute("aria-pressed", String(done));
    completeButton.querySelector(".complete-label").textContent =
      done ? "Completed" : "Mark complete";
  }

  function updateProgress() {
    const chapterProgress = ((activeIndex + 1) / chapters.length) * 100;
    readingProgress.style.width = `${chapterProgress}%`;
    const completeProgress = (completed.size / chapters.length) * 100;
    completionBar.style.width = `${completeProgress}%`;
    completedCount.textContent = `${completed.size} of ${chapters.length} complete`;
    linkById.forEach((link, id) => {
      link.classList.toggle("completed", completed.has(id));
    });
  }

  function toggleComplete() {
    const id = chapters[activeIndex].id;
    if (completed.has(id)) {
      completed.delete(id);
    } else {
      completed.add(id);
    }
    writeSet(storageKeys.completed, completed);
    updatePaging();
    updateProgress();
  }

  function openSidebar() {
    sidebar.removeAttribute("inert");
    document.body.classList.add("sidebar-open");
    menuButton.setAttribute("aria-expanded", "true");
    sidebar.querySelector("input")?.focus();
  }

  function closeSidebar() {
    document.body.classList.remove("sidebar-open");
    menuButton.setAttribute("aria-expanded", "false");
    if (mobileNavigation.matches) {
      sidebar.setAttribute("inert", "");
    } else {
      sidebar.removeAttribute("inert");
    }
  }

  function synchronizeNavigationMode() {
    if (!mobileNavigation.matches) {
      document.body.classList.remove("sidebar-open");
    }
    closeSidebar();
  }

  function renderSearch(query) {
    const normalized = query.trim().toLocaleLowerCase();
    searchResults.replaceChildren();
    if (normalized.length < 2) {
      searchResults.classList.remove("visible");
      return;
    }

    const matches = chapters
      .map((chapter) => {
        const text = chapter.textContent.replace(/\s+/g, " ").trim();
        const haystack = `${chapter.dataset.title} ${text}`.toLocaleLowerCase();
        const index = haystack.indexOf(normalized);
        if (index < 0) {
          return null;
        }
        const textIndex = text.toLocaleLowerCase().indexOf(normalized);
        const start = Math.max(0, textIndex - 42);
        const excerpt = textIndex >= 0
          ? `${start > 0 ? "…" : ""}${text.slice(start, textIndex + normalized.length + 64)}…`
          : text.slice(0, 110);
        return { chapter, excerpt, rank: index };
      })
      .filter(Boolean)
      .sort((left, right) => left.rank - right.rank)
      .slice(0, 6);

    if (matches.length === 0) {
      const empty = document.createElement("span");
      empty.textContent = "No chapter matches that search.";
      searchResults.append(empty);
    } else {
      matches.forEach(({ chapter, excerpt }) => {
        const link = document.createElement("a");
        link.href = `#${chapter.id}`;
        const title = document.createElement("strong");
        title.textContent = chapter.dataset.title;
        const description = document.createElement("small");
        description.textContent = excerpt;
        link.append(title, description);
        link.addEventListener("click", () => {
          searchInput.value = "";
          searchResults.classList.remove("visible");
        });
        searchResults.append(link);
      });
    }
    searchResults.classList.add("visible");
  }

  function setTheme(theme) {
    const value = theme === "light" ? "light" : "dark";
    document.documentElement.dataset.theme = value;
    themeButton.setAttribute(
      "aria-label",
      value === "dark" ? "Switch to light theme" : "Switch to dark theme",
    );
    try {
      localStorage.setItem(storageKeys.theme, value);
    } catch {
      // Theme still applies for this page view.
    }
  }

  function initializeTheme() {
    let saved = "";
    try {
      saved = localStorage.getItem(storageKeys.theme) || "";
    } catch {
      saved = "";
    }
    const preferred = window.matchMedia("(prefers-color-scheme: light)").matches
      ? "light"
      : "dark";
    setTheme(saved || preferred);
  }

  function initializeCodeBlocks() {
    document.querySelectorAll("pre").forEach((block) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "copy-button";
      button.textContent = "Copy";
      button.setAttribute("aria-label", `Copy ${block.dataset.language || "code"} block`);
      button.addEventListener("click", async () => {
        try {
          await navigator.clipboard.writeText(block.querySelector("code")?.textContent || "");
          button.textContent = "Copied";
        } catch {
          button.textContent = "Select text";
        }
        window.setTimeout(() => {
          button.textContent = "Copy";
        }, 1400);
      });
      block.append(button);
    });
  }

  function initializeChecklist() {
    document.querySelectorAll("[data-check]").forEach((input) => {
      input.checked = checklist.has(input.dataset.check);
      input.addEventListener("change", () => {
        if (input.checked) {
          checklist.add(input.dataset.check);
        } else {
          checklist.delete(input.dataset.check);
        }
        writeSet(storageKeys.checklist, checklist);
      });
    });
  }

  menuButton.addEventListener("click", () => {
    if (document.body.classList.contains("sidebar-open")) {
      closeSidebar();
    } else {
      openSidebar();
    }
  });
  sidebarScrim.addEventListener("click", closeSidebar);
  previousButton.addEventListener("click", () => navigateTo(activeIndex - 1));
  nextButton.addEventListener("click", () => navigateTo(activeIndex + 1));
  completeButton.addEventListener("click", toggleComplete);
  searchInput.addEventListener("input", () => renderSearch(searchInput.value));
  themeButton.addEventListener("click", () => {
    setTheme(document.documentElement.dataset.theme === "dark" ? "light" : "dark");
  });
  resetButton.addEventListener("click", () => {
    completed = new Set();
    checklist = new Set();
    writeSet(storageKeys.completed, completed);
    writeSet(storageKeys.checklist, checklist);
    document.querySelectorAll("[data-check]").forEach((input) => {
      input.checked = false;
    });
    updatePaging();
    updateProgress();
  });
  window.addEventListener("hashchange", () => activateChapter(requestedChapterId()));
  mobileNavigation.addEventListener("change", synchronizeNavigationMode);
  document.addEventListener("keydown", (event) => {
    const typing = ["INPUT", "TEXTAREA"].includes(document.activeElement?.tagName);
    if (event.key === "/" && !typing) {
      event.preventDefault();
      searchInput.focus();
      if (window.innerWidth <= 1000) {
        openSidebar();
      }
    } else if (event.key === "Escape") {
      searchInput.value = "";
      renderSearch("");
      closeSidebar();
    } else if (event.altKey && event.key === "ArrowLeft" && !typing) {
      event.preventDefault();
      navigateTo(activeIndex - 1);
    } else if (event.altKey && event.key === "ArrowRight" && !typing) {
      event.preventDefault();
      navigateTo(activeIndex + 1);
    }
  });

  initializeTheme();
  initializeCodeBlocks();
  initializeChecklist();
  synchronizeNavigationMode();
  if (!chapterById.has(rawChapterId())) {
    history.replaceState(null, "", `#${chapters[0].id}`);
  }
  activateChapter(requestedChapterId(), { instant: true });

  const settleInitialPosition = () => {
    const reset = () => {
      if (document.scrollingElement) {
        document.scrollingElement.scrollTop = 0;
      }
      window.scrollTo(0, 0);
    };
    reset();
    window.requestAnimationFrame(() => {
      window.requestAnimationFrame(reset);
    });
    window.setTimeout(reset, 100);
  };
  if (document.readyState === "complete") {
    settleInitialPosition();
  } else {
    window.addEventListener("load", settleInitialPosition, { once: true });
  }
})();
