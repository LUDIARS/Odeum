# Spectator data schema

Spectator is a local-first desktop application. Local SQLite and capture files are authoritative until the user explicitly enables Volputas synchronization or exports a JSON file.

| Data | Type | Authority | Storage | Protection | Protection and lifecycle |
|---|---|---|---|---|---|
| Game target registrations | user | Local Spectator | SQLite `game_targets` | Required | Device-local; contains executable/window metadata; deleted by local user action. |
| Play sessions and timing | user | Local Spectator | SQLite `local_sessions` | Required | Device-local behavioral data; no account identifier is required in standalone mode. |
| Capture drafts and comments | user | Local Spectator | SQLite `drafts` | Required | Device-local personal expression; synchronized only in explicit `volputas` mode. |
| Screenshots and replay videos | user | Local Spectator | `%LOCALAPPDATA%/Spectator/captures` | Required | May contain private gameplay content; deleted with the owning local draft. |
| Imported review videos | user | Local Spectator | Managed copy in `%LOCALAPPDATA%/Spectator/captures` | Required | Source file is copied, never moved. The managed copy is authoritative for annotation playback and is deleted with its local draft. |
| Self-reported reactions | user | Local Spectator | SQLite `reaction_annotations` | Required | Verbatim personal expression. Stored with video offset and UTC time; cascade-deleted with the draft; never written to logs. |
| Reaction raw data export | user | Exported file chosen by user | UTF-8 JSON | Required | Contains self-reported text and video hash but no account ID or authentication token. The user controls destination, sharing, and deletion after export. |
| Volputas/OBS credentials | user/secret | Credential issuer | Windows Credential Manager | Required | Never stored in SQLite, JSON export, settings, or logs. Not required in standalone mode. |

## `reaction_annotations` v3 table

| Column | Type | Constraint | Meaning |
|---|---|---|---|
| `id` | TEXT UUID | primary key | Stable local annotation identifier. |
| `draft_id` | TEXT UUID | foreign key, cascade delete | Owning capture draft. |
| `video_offset_ms` | INTEGER | `>= 0` | Offset from the beginning of the stored replay. Export also verifies it does not exceed video duration. |
| `content` | TEXT | 1–2000 characters | Verbatim self-report or the explicit default stamp label. |
| `recorded_at` | TEXT | UTC ISO 8601 | Time the player entered the reaction. |
| `kind` | TEXT | `Comment` / `Positive` / `Negative` | Explicit user-selected annotation type; added in schema v3. |

## Export boundary

The versioned contract is [`reaction-raw-data.schema.json`](reaction-raw-data.schema.json). `utterances` is intentionally compatible with the Volputas `ExternalUtterance` input shape. The raw document is source evidence; sentiment vectors, bins, and emotion curves are derived data and are not written back into the raw document.
