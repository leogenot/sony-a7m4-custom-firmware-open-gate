# Open gate in movie mode: research (2026-11-25, offline)

> Research notes for later work, not a feature of this build. Nothing here has been tried on a camera.

Question: can open gate be chosen and applied with the Still/Movie/S&Q switch on **movie**, so stills and video keep
their own setups?

**Answer: probably, but only a camera test can say how.** Our "stills only" rule is inherited from the kit, and the
kit's stated reason doesn't apply to this build. Nobody has written the open-gate sites while the movie pipeline runs.
In movie mode, writes happen only in two cases: a Mode change from the menu, and the first start-up after memory was
reset. A probe build with a crash guard can tell which design works.

Sources: the kit is [ArcaneLantern/a7iv-open-gate-test](https://github.com/ArcaneLantern/a7iv-open-gate-test) at
`2f2a021`; `apply.py` below is its `windows/tools/camera_tests/CT6_memory_read_check/open_gate_apply.py`, and
designs are in `windows/tools/modding/designs`. Logs 1 and 3 are Léo's camera traces of 2026-11-24 from the diagnostic
builds (`og-diag 1`), quoted by their line tags.

## Why the rule exists

| Reason | Source | Applies to this build? |
| --- | --- | --- |
| Step 1 (taller raw write buffer, `dmmwb4688_v1`) "only works straight after a cold boot with the dial on stills, before movie mode has been used" | `apply.py:381` | **No.** The build writes step 1 into `DmmCfg.bin`, so it is in place from boot ([README](README.en.md)) |
| The kit writes over USB: "Turn it on, dial on stills, and connect the USB cable. USB Connection Mode: Mass Storage" | `apply.py:176`, `apply.py:334`, kit `README.md:54`, `:66` | **No.** The kit always writes in USB service mode, with live view stopped. It never wrote with movie live view running |
| Our module copies the kit | `og.c:4-5` header, `og.c:83` (`og_power_on` returns on movie), `og.c:92` (`og_select` returns `OG_WRONG_DIAL`), `og_menu.c:58` (Mode disabled on movie) | It is the rule under question |

No source reports a failure of a movie-mode write. It was never tried.

## What the sites change, and when they are probably read

The read times are **inferences**: no source states them.

| Design | What it changes | Probably read |
| --- | --- | --- |
| `ogsq_v3` / `ogsq880_v1`, `og57n_v1` / `og57n880_v1` | 13 sensor/front-end sites in coprocessor RAM (6,024 bytes: first stage, scaler steps), plus 26 bytes of the stored drive-parameter record of `0x8c0` / `0x880` | When the movie drive mode starts |
| `outsz2560_v1`, `og57cp_v3` | Output size in the movie size table. For 5.7K, "output units in the allocator" (memory id `0x43f`, three units to a 106.9 MB block) | When the movie pipeline starts and allocates its buffers |
| `ogfile2560_hs_s_v2`, `ogfile57r300m1_v4` | Encoder descriptors and profiles (`av-cam.bin`); MP4 writer rows and size instructions (`libmpr.so`) | When recording starts |
| `lcdfill_v2` | Rear-screen fit (3:2 fill) and frame rectangle | When the display pipeline is set up |

Evidence for "at movie start": after a stills apply, the kit says "Turn the dial to movie. The rear screen shows the
full-height picture at 3:2, filling the screen" (`apply.py:399`). Log 3 shows the same order on this build: applied on
stills (lines `1b`-`2d`), switched to movie (`3e`), and the recordings had the right size.

## When our hook runs on movie

`og_power_on` runs on every shooting-screen creation (`layout.json` hooks `0x1ddab68`, `0x1e56b2c`). In the traces it
fires with the switch on movie (`power_on 3`):

- **At start-up on movie:** log 1, lines `0`-`2`.
- **On entering movie:** log 1, line `13`, after stills at `a`-`12`; log 3, line `3e`, after the last stills checks at
  `2e`-`3d`.
- **After each menu exit:** log 1, lines `8`-`9` and `1b`, after menu commits; log 3, lines `40`, `41` after the menu at
  `3f`.

A reconcile writes nothing when memory already holds the mode. Log 3 line `2c` (`reconcile.classify 0 2`) is followed
by `power_on.reconcile 2 0`, with no writes. So, with movie allowed, only two moments would write in movie mode:

1. **A Mode change from the movie menu.**
2. **The first `og_power_on` on movie after memory was reset** (battery pull, or a long power-off).

Every other movie-mode call would be a read-only check.

## The risk

Both moments write sensor records and allocator unit sizes the movie pipeline may already be using. Three outcomes are
possible:

- The pipeline ignores the change until its next restart: safe, but needs a restart trigger.
- The pipeline picks it up mid-stream: a glitch, or a broken first file.
- The coprocessor freezes until a battery pull.

A freeze is recoverable the same way as with the kit: the patches live in RAM only, so a battery pull returns the
camera to stock, and `DmmCfg.bin`'s step 1 is static. The crash guard below makes sure a saved mode can't freeze it
again at start-up.

## Candidate designs

| | Design | Unknown it rests on |
| --- | --- | --- |
| A | Apply at the movie-menu commit. Rely on the shooting screen's re-creation at menu exit to restart the pipeline with the new settings | Does menu exit restart the movie pipeline, or does it keep running behind the menu? |
| B | Save the choice on movie; apply it at the next known safe restart | Which camera action is a safe restart (switch position, frame-rate change, S&Q)? |
| C | Apply, then call appFw's own movie-pipeline restart, the path a Rec Frame Rate change takes | Reverse engineering: where that path is and whether it can be called safely |

For the start-up case: apply at `og_power_on` on movie, behind the crash guard, if the probe shows the pipeline takes
it.

## Proposed probe build (not built)

- **Mode on movie:** enabled. `og_select` applies on movie with the step trace on (`/setting/og_diag.txt`, as in the
  diagnostic builds).
- **Start-up on movie:** probe 1 keeps `og_power_on` read-only on movie. Probe 2 lets it apply behind the guard.
- **Crash guard:** `/setting/open_gate.try` is written and synced before any movie-mode apply, and removed once the
  apply returns. If it exists at the next start, the previous movie apply never finished: the module logs it, saves
  Off and removes the file, so the camera starts at stock.

Camera protocol, with the trace read back after each step:

| Test | Steps | Look at |
| --- | --- | --- |
| T1 | On movie: Mode = 4K 3:2, exit the menu | Live view runs; rear screen fills 3:2; record 10 s: file 3840x2560, clean picture |
| T2 | On movie: Mode = 5.7K 3:2, exit the menu | As T1, file 5760x3840 |
| T3 | On movie: 4K 3:2 -> 5.7K 3:2 directly | As T2 |
| T4 | On movie: Mode = Off | Live view and a 10 s file at stock 16:9 |
| T5 | If T1-T3 didn't take: change Rec Frame Rate there and back, or switch to S&Q and back, then record | Whether that restart picks it up |
| T6 | Probe 2: Mode on, battery pull, start on movie | Applied at start-up; file size; no freeze |

| Outcome | Next |
| --- | --- |
| T1-T4 pass after menu exit | Design A: ship movie-mode selection, then probe 2 for start-up |
| Taken only after a restart (T5) | Design B, or C if the restart can be triggered from the module |
| Freeze, broken picture or a broken first file | Keep selection on stills; look at an earlier start-up hook for the battery-pull case only |

## Open questions (camera only)

- Does the movie pipeline keep running while the menu is open?
- Does the shooting-screen re-creation at menu exit restart the movie pipeline and its buffer allocation?
- Is a mid-stream write to the sensor records harmless, ignored, or fatal?
- At start-up on movie, does the first `og_power_on` come before or after the pipeline allocates its buffers?
