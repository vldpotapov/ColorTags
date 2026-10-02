# Storage Research — Phase 2

Date: 2026-09-22
Machine: Windows 11 25H2 (build 26200.9457), NTFS system drive, no admin rights.
Scope: decide the primary persistence strategy and the fallback, based on
behavior tested on this machine.

---

## 1. Options compared

| Option | Files | Folders | No admin | Verified | Risks |
|---|---|---|---|---|---|
| A. NTFS ADS (`:ColorTag`) | YES | YES | YES | ✅ this phase | NTFS-only; lost on FAT/exFAT copy, ZIP, some sync; network shares unknown |
| B. SQLite (path→tag) | YES | YES | YES | not run | rename/move tracking, stale entries, sync |
| C. Property System | per-type only | NO | NO (schema) | research (Phase 0) | handler per file type; folders impossible |

Phase 0 already established that only ADS works for **both files and folders
without admin**. This document adds the Phase 2 experiments and the decision.

---

## 2. Tested behavior (this machine, real experiments)

All experiments ran against a temp dir on `C:` (NTFS) unless noted.

| # | Experiment | Result |
|---|---|---|
| 1 | Write `red` to `file:ColorTag`, read back | ✅ exact round trip |
| 2 | Delete stream, read again | ✅ `FileNotFoundError` → treated as untagged |
| 3 | ADS on a **folder** | ✅ works |
| 4 | ADS on a **read-only file** | ❌ write raises `PermissionError`; **read still works** |
| 5 | ADS on a **hidden file** | ✅ works |
| 6 | **Long path** (314 chars, >260) | ✅ works (Python is long-path aware) |
| 7 | `shutil.copy2` (NTFS→NTFS) | ✅ ADS preserved |
| 8 | `os.replace` (rename/move on NTFS) | ✅ ADS preserved |
| 9 | Value encoding | ✅ stored as UTF-8, no BOM, no trailing newline |
| 10 | Value with trailing newline (`red\n`) | read back verbatim → **must strip** when parsing |
| 11 | **Empty stream** (0 bytes) | read back as `''` → treat as untagged |
| 12 | **UTF-16 BOM value** | `UnicodeDecodeError` on UTF-8 read → treat as untagged |
| 13 | **Unknown value** (`magenta`) | read back verbatim → validate against known ids → untagged |
| 14 | Stream visible to `dir /r` | ✅ |
| 15 | OneDrive local folder on NTFS | ✅ local ADS write/read works |
| 16 | **Google Drive virtual drive** (G:/H:/I:, reports FAT32) | ✅ local ADS write/read works (virtual FS) |
| 17 | Network shares | none present on this machine → **untested** |

### 2.1 Volume inventory

| Drive | File system | Notes |
|---|---|---|
| C: | NTFS | system drive, 1 TB |
| G: | FAT32 (virtual) | Google Drive for Desktop, account A |
| H: | FAT32 (virtual) | Google Drive for Desktop, account B |
| I: | FAT32 (virtual) | Google Drive for Desktop, account C |

No real FAT32/exFAT volume exists on this machine, so genuine FAT32 behavior
could not be exercised; it is documented from known platform behavior (ADS is
not supported on FAT/exFAT — the stream open fails).

---

## 3. Failure cases

1. **Read-only file** — `set_tag`/`remove_tag` raise `PermissionError`.
   `get_tag` works. The UI must surface this as "cannot tag: file is
   read-only" instead of failing silently.
2. **FAT/exFAT volume** — stream open fails (`OSError`/`PermissionError`).
   `is_supported` must return `False` before any write is attempted.
3. **Corrupt/foreign stream content** — empty, UTF-16, or unknown id values
   are all treated as *untagged* (never crash, never surface garbage).
4. **Locked file** (open by another process) — write may raise
   `PermissionError`; same handling as read-only.
5. **Cloud sync** — local writes succeed on OneDrive and Google Drive virtual
   drives, but whether the ADS survives actual cloud sync is **unknown** and
   is the main residual risk (see §5).

---

## 4. Compatibility

- **NTFS (local)**: full support — files, folders, rename, move, copy within
  NTFS, long paths, hidden files.
- **FAT32 / exFAT**: unsupported (no ADS). Fallback required.
- **Google Drive virtual drives**: FS reports FAT32, but ADS works locally.
  Sync preservation unverified → treated as unsupported (conservative).
- **OneDrive local folder**: NTFS, works locally; sync preservation unverified.
- **Network shares**: untested on this machine; known to vary by server
  (SMB supports ADS on NTFS-backed shares, but not guaranteed). `is_supported`
  will report based on the reported file system; failures surface as errors.
- **ZIP/archives**: known to drop ADS (documented, not re-tested).

---

## 5. Recommendation

**Primary storage: NTFS ADS** — `:ColorTag` stream, value = stable tag id
(`red|orange|yellow|green|blue|purple|gray`), UTF-8, no BOM, no trailing
newline. Rationale: the only option verified for files **and** folders without
admin; survives rename/move on NTFS; no central database to corrupt or sync.

**Fallback: SQLite** (`SqliteTagStore`) for volumes where `is_supported` is
`False` (FAT32/exFAT/network/cloud-virtual). Identity strategy:

- Primary key: **volume serial + file index** (`os.stat().st_dev` +
  `st_ino` — both available on Windows, verified: `st_ino` and `st_dev` are
  populated). This survives renames and moves on the same volume.
- A `path` column is kept for lookup and display; on a path miss the entry is
  stale and the item is reported untagged (v1 does not run a rename watcher).
- Cross-volume moves change the file index → old entry becomes stale; the
  stale row is cleaned lazily on next access.

**Property System** stays rejected as a primary: per-file-type handlers only,
no folder support, admin schema required (Phase 0).

---

## 6. Fallback strategy (decision)

- `AdsTagStore.is_supported(path)` → `True` only when the volume's file
  system (via `GetVolumeInformationW`) is `NTFS`.
- The service layer picks the store per path: NTFS → ADS, otherwise →
  SQLite. A single `TagService` instance can hold both stores and route by
  `is_supported`.
- If ADS write fails at runtime despite `is_supported` (read-only, locked,
  network quirk), the error is surfaced to the UI, not silently swallowed.
- Cloud-virtual drives (Google Drive) report FAT32 → routed to SQLite
  fallback, even though local ADS happens to work; sync preservation is the
  deciding risk.

---

## 7. Open items

- Verify ADS survival through real OneDrive/Google Drive sync (needs a
  synced test file and time).
- Verify ADS on a real FAT32/exFAT volume and on an SMB share.
- Decide whether a future version should probe cloud-virtual drives for ADS
  support instead of trusting the reported file system name.
