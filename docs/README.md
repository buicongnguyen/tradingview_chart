# TradeChart Lab project book

This directory is a dependency-free static guide deployed to GitHub Pages.

## Local preview

Open `index.html` directly, or serve the repository root with a local static
server:

```powershell
py -m http.server 8080 --directory docs
```

Then open `http://localhost:8080/`.

## Content rules

- Keep commands synchronized with the repository scripts and workflows.
- Preserve the chapter IDs because public deep links and navigation use them.
- Add every chapter to the sidebar, the article collection, and the integrity
  test's expected order.
- Keep every chapter in the continuous document flow and update the scroll-spy
  behavior when navigation structure changes.
- Do not add runtime CDN dependencies, credentials, analytics, or tracking.
- Run `npm run test:web` before committing.

The Pages workflow deploys this directory only after validation on `main`.
