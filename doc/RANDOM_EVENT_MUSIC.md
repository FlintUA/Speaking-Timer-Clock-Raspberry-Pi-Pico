# Random music for timer and scheduled alarms

Status: installed with separate approval on November 13 as `1.0.0+28.g36286e9`. Startup, catalog and read-only USB/HTTP/settings checks passed; the user subsequently confirmed general operation. Focused per-slot/output/persistence cases remain separate.

## Controls

- Timer: enable Random timer music track under browser Settings. It applies only when extra sound is Music; message ON/OFF remains independent. Disable random to restore the saved specific track. The physical Timer sound editor adds FIXED/RANDOM after MUSIC; FIXED continues to its track number, RANDOM confirms the draft without a number.
- Alarms 1-5: each slot has Signal, Music - specific track, or Music - random track. On the clock, rotate the SOUND value through SIGNAL/MUSIC/RANDOM. Fixed music continues to TRACK; signal/random confirms without a track-number page. Enabled state, weekday schedule and time are unchanged.
- The browser's existing Test button auditions specific tracks only. It is disabled for a random draft, except that Stop remains available for an already active audition. Scheduled/random playback is tested by letting an event occur.
- USB: `timer-random on|off`, `alarm-config N HH MM random`, and the slot-1 alias `alarm-set HH MM random`. Existing fixed/signal syntax remains valid. `random-music-status` reports the cached IDs and timer random flag without reading or modifying cards.

Changing a preference uses the existing deferred settings save and existing idle/editor guards. Wait for Settings saved before power cycling. Ordinary start/cancel speech, independent LOCAL/Bluetooth levels, timer completion message choices, Stop timer sound and alarm stop/snooze rules are unchanged.

## Selection and event lifetime

The timer selects once when its countdown finishes. Each alarm selects when its due occurrence is queued (or a direct alarm test starts), not while editing its configuration. Different slots and the timer remember their own previous random track in RAM. If at least two files exist, the next new occurrence avoids that owner's immediately previous selection. One available file can repeat. This is random selection, not a no-repeat playlist.

An alarm's selected file stays with its RAM-only occurrence through three-minute snooze, deferred waiting and interruption by a higher-priority timer. The next scheduled occurrence selects again. Bluetooth-to-LOCAL fallback copies the already built remaining plan and does not select again. Random completion plays one file, not an ongoing music-player session. Both selected choices and random history reset on reboot; scheduled preferences persist.

## File catalog and cards

The integrated MP3-enabled profiles cache actual short-name files `08/001.MP3` through `08/255.MP3` from the SPI FAT32 card before radio startup. File order and numbering gaps do not matter. Duplicate numeric IDs are deduplicated. Directories, deleted/LFN/volume entries, empty files, invalid clusters and other names/extensions are excluded. The scan is bounded by the existing 64-cluster directory limit and is independent of the diagnostic listing's 64-entry print limit. A failed or incomplete scan discards all partial IDs.

No directory scan, folder-count query or new card I/O runs in an event callback or at the moment of random selection. The cache is read-only for the boot session: reboot after editing/replacing the card. Random mode without an available catalog, including a build without the integrated MP3 boot path, uses the standard signal and logs the reason rather than guessing file numbers. Fixed-track mode retains its existing behavior.

The DFPlayer uses a different card. Its folder-count query cannot prove which numeric files exist, so both cards must contain the same folder-08 files for reliable LOCAL fallback. The SPI catalog establishes file presence, not decoder validity or equality of card contents. Keep the cards mirrored; firmware cannot verify the DFPlayer's contents file by file. `[RANDOM-MUSIC]` logs record owner (0=timer, 1-5=alarm), selected track or signal fallback. Alarm RINGING logs report the actual selected track (0 denotes signal), while saved configuration keeps the fixed track for later reuse.

## Saved format and compatibility

Conditional Flash v16 is 632 bytes and still occupies three pages in the same A/B sectors. It extends the immutable v15 prefix and checks all data with a final CRC; historical checksum fields in the prefix remain erased FF. It stores timer_random and permits the separate RANDOM alarm sound value. The alarm revision token now allocates two sound bits so RANDOM cannot collide with a fixed-track edit. These tokens are live API data, not a Flash-layout change.

Older v1-v15 reads default timer_random OFF and retain fixed/signal alarm settings, credentials, Bluetooth devices, birthdays and all other preferences without writing Flash. If random mode is unused, saves retain the existing v11-v15 conditional formats. A rollback must use a v16-aware build after saving random preferences; an older build may ignore v16 records or refuse saves. Flash sectors and BTstack key storage are unchanged.

## Verification and pending hardware checks

Twenty-one registered checks cover numeric/sparse/duplicate/invalid catalog entries, avoiding the immediately previous track, singleton/empty catalogs, actual FAT32 scanning beyond 64 entries, incomplete/corrupt scans, random alarm parsing and independent revision bits, browser checkbox/track controls, snooze choice copying, and v15/v16 persistence including alarm-only random configuration, CRC-corruption fallback and preservation of other saved data. Both web and ordinary BT/MP3/Wi-Fi profiles build.

A one-off host harness compiles actual main-loop choice/queue/start/stop/snooze/timer-preemption and Bluetooth fallback handlers with stubbed hardware. It verifies no reroll on snooze, delayed dispatch or LOCAL fallback, a new choice for the next occurrence, empty-catalog signal fallback and unchanged fixed tracks. This is transport-independent software evidence, not physical audio acceptance.

After separately approved installation, confirm the boot catalog and existing settings first. With matching cards, test timer random with and without its message, repeated new countdowns, and Stop timer sound. For each desired alarm slot select random, let it ring, snooze three minutes and verify the same music returns; the next scheduled event should draw again. Compare LOCAL/Bluetooth when convenient. Wait for saved settings, power cycle and verify preferences. Four real browser clients, load latency and card equality remain separate checks. Do not hot-swap cards during playback to force a failure.

The user subsequently confirmed that both cards are identical and music numbering has no gaps. The implementation continues to use actual IDs and does not depend on contiguous numbering.
