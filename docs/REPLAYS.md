# Replays

Use this for bug reports in internal builds.

## Quick State

- `F5`: save `debug/states/quick.ctrstates`
- `F8`: load `debug/states/quick.ctrstates`

## Record

```sh
build/ctr_native --record
```

Windows: use `build\ctr_native.exe` instead.

Normal saves live in `memcards/slot0`.

When recording starts, the CTR save files from `memcards/slot0` and `slot1` are copied to `memcard.seed`. The game records with a writable copy named `memcard.recording`, so saves and ghosts made while recording stay in the report.

To choose when recording starts:

```sh
build/ctr_native --record --toggle
```

- Press `F9` to start.
- Press `F10` to stop.

For more detailed reports:

```sh
build/ctr_native --record --detailed
```

You can combine both:

```sh
build/ctr_native --record --toggle --detailed
```

## Play Back

Use the command written in that folder's `metadata.txt`.

It looks like:

```sh
build/ctr_native --replay "debug/reports/20260605/ctr-123456/input.ctrreplay"
```

Playback creates a fresh writable `memcard.playback` from `memcard.seed` every run and does not touch your real saves.

If a developer asks you to bypass header identity checks:

```sh
build/ctr_native --replay "debug/reports/20260605/ctr-123456/input.ctrreplay" --replay-bypass-header
```

## Regression Check

Every replay you keep is a regression test. Playback compares the game's state each frame, so a change that alters
gameplay shows as `divergence at replay frame N`:

```sh
python3 tools/replay_check.py --replays debug/reports
```

It plays each `*.ctrreplay` with `--replay-bypass-header` (the replay names the build that recorded it) and
`--replay-fast`: VBlanks are emitted as fast as the host runs, while game time stays the replay's, so a replay takes
seconds instead of minutes. The last lines of a playback give the throughput:

```text
[CTR Replay] replay finished after 5400 frames
[CTR Replay] 12.40 s wall, 435.5 game frames/s (unthrottled)
```

Run it with a sanitizer build (`tools/check.py --sanitize --replays debug/reports`) to catch memory errors and leaks
as well. Replays come from your own copy of the game: keep them out of the repository.
