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
  const partById = new Map();
  document.querySelectorAll("#chapter-nav .nav-group-label").forEach((label) => {
    label.nextElementSibling?.querySelectorAll('a[href^="#"]').forEach((link) => {
      partById.set(link.hash.slice(1), label.textContent.trim());
    });
  });

  const sidebar = document.querySelector("#sidebar");
  const chapterNavigation = document.querySelector("#chapter-nav");
  const menuButton = document.querySelector("#menu-button");
  const sidebarScrim = document.querySelector("#sidebar-scrim");
  const completeButton = document.querySelector("#mark-complete");
  const searchInput = document.querySelector("#book-search");
  const searchResults = document.querySelector("#search-results");
  const readingProgress = document.querySelector("#reading-progress-bar");
  const completionBar = document.querySelector("#completion-bar");
  const completedCount = document.querySelector("#completed-count");
  const currentPartLabel = document.querySelector("#current-part-label");
  const currentChapterLabel = document.querySelector("#current-chapter-label");
  const themeButton = document.querySelector("#theme-button");
  const resetButton = document.querySelector("#reset-progress");
  const mobileNavigation = window.matchMedia("(max-width: 1000px)");

  let activeIndex = 0;
  let scrollUpdatePending = false;
  let completed = new Set(
    [...readSet(storageKeys.completed)].filter((id) => chapterById.has(id)),
  );
  let checklist = readSet(storageKeys.checklist);

  function readSet(key) {
    try {
      const value = JSON.parse(localStorage.getItem(key) || "[]");
      return new Set(
        Array.isArray(value)
          ? value.filter((item) => typeof item === "string")
          : [],
      );
    } catch {
      return new Set();
    }
  }

  function writeSet(key, values) {
    try {
      localStorage.setItem(key, JSON.stringify([...values]));
    } catch {
      // The reader remains usable when local storage is disabled.
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

  function chapterNumber(index) {
    return String(index + 1).padStart(2, "0");
  }

  function setActiveChapter(id, { replaceHash = false } = {}) {
    const chapter = chapterById.get(id) || chapters[0];
    const nextIndex = chapters.indexOf(chapter);
    const changed = nextIndex !== activeIndex
      || !linkById.get(chapter.id)?.classList.contains("active");
    activeIndex = nextIndex;

    if (changed) {
      navigationLinks.forEach((link) => {
        const active = link.hash === `#${chapter.id}`;
        link.classList.toggle("active", active);
        if (active) {
          link.setAttribute("aria-current", "location");
        } else {
          link.removeAttribute("aria-current");
        }
      });
      currentPartLabel.textContent =
        partById.get(chapter.id) || "Project book";
      currentChapterLabel.textContent =
        `${chapterNumber(activeIndex)} / ${chapters.length} · ${chapter.dataset.title}`;
      document.title = `${chapter.dataset.title} — Building TradeChart Lab`;
      updateCompletion();

      const activeLink = linkById.get(chapter.id);
      if (activeLink && chapterNavigation && !mobileNavigation.matches) {
        const navBounds = chapterNavigation.getBoundingClientRect();
        const linkBounds = activeLink.getBoundingClientRect();
        const edgePadding = 8;

        if (linkBounds.top < navBounds.top + edgePadding) {
          chapterNavigation.scrollTop -=
            navBounds.top + edgePadding - linkBounds.top;
        } else if (linkBounds.bottom > navBounds.bottom - edgePadding) {
          chapterNavigation.scrollTop +=
            linkBounds.bottom - navBounds.bottom + edgePadding;
        }
      }
    }

    if (replaceHash && location.hash !== `#${chapter.id}`) {
      history.replaceState(null, "", `#${chapter.id}`);
    }
  }

  function chapterAtReadingLine() {
    const headerHeight =
      document.querySelector(".topbar")?.getBoundingClientRect().height || 0;
    const readingLine =
      window.scrollY + headerHeight + Math.min(window.innerHeight * 0.22, 170);
    let selected = chapters[0];
    for (const chapter of chapters) {
      const chapterTop =
        chapter.getBoundingClientRect().top + window.scrollY;
      if (chapterTop <= readingLine) {
        selected = chapter;
      } else {
        break;
      }
    }

    const pageBottom = window.scrollY + window.innerHeight;
    const documentBottom = document.documentElement.scrollHeight - 2;
    return pageBottom >= documentBottom
      ? chapters[chapters.length - 1]
      : selected;
  }

  function updateReadingProgress() {
    const available =
      document.documentElement.scrollHeight - window.innerHeight;
    const percentage = available > 0
      ? Math.min(100, Math.max(0, (window.scrollY / available) * 100))
      : 100;
    readingProgress.style.width = `${percentage}%`;
  }

  function updateFromScroll() {
    scrollUpdatePending = false;
    const chapter = chapterAtReadingLine();
    setActiveChapter(chapter.id, { replaceHash: true });
    updateReadingProgress();
  }

  function scheduleScrollUpdate() {
    if (scrollUpdatePending) {
      return;
    }
    scrollUpdatePending = true;
    window.requestAnimationFrame(updateFromScroll);
  }

  function scrollToChapter(id, { smooth = true, pushHistory = false } = {}) {
    const chapter = chapterById.get(id);
    if (!chapter) {
      return;
    }
    if (pushHistory && location.hash !== `#${id}`) {
      history.pushState(null, "", `#${id}`);
    }
    chapter.scrollIntoView({
      behavior: smooth ? "smooth" : "auto",
      block: "start",
    });
    setActiveChapter(id);
  }

  function navigateTo(index) {
    if (index < 0 || index >= chapters.length) {
      return;
    }
    scrollToChapter(chapters[index].id, {
      smooth: true,
      pushHistory: true,
    });
  }

  function updateCompletion() {
    const completeProgress = (completed.size / chapters.length) * 100;
    completionBar.style.width = `${completeProgress}%`;
    completedCount.textContent =
      `${completed.size} of ${chapters.length} complete`;
    linkById.forEach((link, id) => {
      link.classList.toggle("completed", completed.has(id));
    });

    const currentId = chapters[activeIndex].id;
    const done = completed.has(currentId);
    completeButton.classList.toggle("done", done);
    completeButton.setAttribute("aria-pressed", String(done));
    completeButton.querySelector(".complete-label").textContent =
      done ? "Chapter completed" : "Mark chapter complete";
  }

  function toggleComplete() {
    const id = chapters[activeIndex].id;
    if (completed.has(id)) {
      completed.delete(id);
    } else {
      completed.add(id);
    }
    writeSet(storageKeys.completed, completed);
    updateCompletion();
  }

  function openSidebar() {
    sidebar.removeAttribute("inert");
    document.body.classList.add("sidebar-open");
    menuButton.setAttribute("aria-expanded", "true");
  }

  function closeSidebar({ restoreFocus = false } = {}) {
    document.body.classList.remove("sidebar-open");
    menuButton.setAttribute("aria-expanded", "false");
    if (mobileNavigation.matches) {
      sidebar.setAttribute("inert", "");
      if (restoreFocus) {
        menuButton.focus({ preventScroll: true });
      }
    } else {
      sidebar.removeAttribute("inert");
    }
  }

  function synchronizeNavigationMode() {
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
        const haystack =
          `${chapter.dataset.title} ${text}`.toLocaleLowerCase();
        const index = haystack.indexOf(normalized);
        if (index < 0) {
          return null;
        }
        const textIndex = text.toLocaleLowerCase().indexOf(normalized);
        const start = Math.max(0, textIndex - 42);
        const excerpt = textIndex >= 0
          ? `${start > 0 ? "…" : ""}${text.slice(
            start,
            textIndex + normalized.length + 64,
          )}…`
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
    const value = theme === "dark" ? "dark" : "light";
    document.documentElement.dataset.theme = value;
    themeButton.setAttribute(
      "aria-label",
      value === "dark" ? "Switch to light theme" : "Switch to dark theme",
    );
    document.querySelector('meta[name="theme-color"]')?.setAttribute(
      "content",
      value === "dark" ? "#10151f" : "#f7f9fc",
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
    setTheme(saved || "light");
  }

  function initializeCodeBlocks() {
    document.querySelectorAll("pre").forEach((block) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "copy-button";
      button.textContent = "Copy";
      button.setAttribute(
        "aria-label",
        `Copy ${block.dataset.language || "code"} block`,
      );
      button.addEventListener("click", async () => {
        try {
          await navigator.clipboard.writeText(
            block.querySelector("code")?.textContent || "",
          );
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
      closeSidebar({ restoreFocus: true });
    } else {
      openSidebar();
    }
  });
  sidebarScrim.addEventListener("click", () => {
    closeSidebar({ restoreFocus: true });
  });
  completeButton.addEventListener("click", toggleComplete);
  searchInput.addEventListener("input", () => renderSearch(searchInput.value));
  themeButton.addEventListener("click", () => {
    setTheme(
      document.documentElement.dataset.theme === "dark" ? "light" : "dark",
    );
  });
  resetButton.addEventListener("click", () => {
    completed = new Set();
    checklist = new Set();
    writeSet(storageKeys.completed, completed);
    writeSet(storageKeys.checklist, checklist);
    document.querySelectorAll("[data-check]").forEach((input) => {
      input.checked = false;
    });
    updateCompletion();
  });

  document.addEventListener("click", (event) => {
    const link = event.target.closest('a[href^="#"]');
    if (!link) {
      return;
    }
    const id = link.hash.slice(1);
    if (!chapterById.has(id)) {
      return;
    }
    event.preventDefault();
    const drawerWasOpen =
      mobileNavigation.matches
      && document.body.classList.contains("sidebar-open");
    closeSidebar({ restoreFocus: drawerWasOpen });
    const navigate = () => {
      scrollToChapter(id, {
        smooth: !drawerWasOpen,
        pushHistory: true,
      });
    };
    if (drawerWasOpen) {
      // Let the off-canvas transition finish so its focused link cannot
      // interrupt the document scroll on touch browsers.
      window.setTimeout(navigate, 240);
    } else {
      navigate();
    }
  });

  window.addEventListener("scroll", scheduleScrollUpdate, { passive: true });
  window.addEventListener("resize", scheduleScrollUpdate);
  window.addEventListener("popstate", () => {
    scrollToChapter(requestedChapterId(), { smooth: false });
  });
  mobileNavigation.addEventListener("change", synchronizeNavigationMode);
  document.addEventListener("keydown", (event) => {
    const typing = ["INPUT", "TEXTAREA"].includes(
      document.activeElement?.tagName,
    );
    if (event.key === "/" && !typing) {
      event.preventDefault();
      if (mobileNavigation.matches) {
        openSidebar();
      }
      searchInput.focus();
    } else if (event.key === "Escape") {
      searchInput.value = "";
      renderSearch("");
      closeSidebar({ restoreFocus: mobileNavigation.matches });
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
  setActiveChapter(requestedChapterId());
  updateReadingProgress();

  if (chapterById.has(rawChapterId())) {
    window.requestAnimationFrame(() => {
      scrollToChapter(rawChapterId(), { smooth: false });
    });
  } else {
    history.replaceState(null, "", `#${chapters[0].id}`);
  }
})();
