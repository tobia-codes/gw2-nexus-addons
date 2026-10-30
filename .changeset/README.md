# Changesets

One file per player-visible change (`python3 tools/changeset.py add <major|minor|patch> <core|memo> "<text>"`
or `/changeset`), Changesets format:

```
---
"core": minor
---

Text for players (English) — becomes the release note.
```

Merging `develop` into `main` releases every addon with pending changesets (version = last
release + highest bump); the workflow then writes the CHANGELOG and removes the files on
`develop`. Details: `plans/RELEASE.md`.
