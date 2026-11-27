# Card-loaded modules: research (read-only)

Question: can the A7 IV load features from the SD card at run time, the way
[Himpq/Pilot](https://github.com/Himpq/Pilot) does on the ILCE-6700 2.00, so that a broken feature is undone by
removing the card instead of a Fastboot recovery? This note records what the files show. Nothing here was built or run
on a camera. Reference revisions: Pilot `d2d22de` (2026-11-27); appFw.so of v0.1.3
(`c5f9bc7c…ccf5fc`, the base of every build here) and of 6.02 (`1e1b9202…0018a2`).

## How Pilot does it (A6700 2.00)

- **One permanent change.** At the end of appFw.so's `DT_INIT`, the `ldp x29, x30, [sp], #0x10` becomes a `B` to a
  0x500-byte island. The island `dlopen`s `/usr/lib/pilot.so` (`RTLD_NOW | RTLD_GLOBAL`), records the result, and runs
  the original epilogue. `PT_LOAD[0]` is extended to cover the island.
- **The resident library finds Sony's functions by name.** `pilot.so` uses `dlsym(RTLD_DEFAULT, …)` for `Fsys_Stat`,
  `Fsys_Open`, `Fsys_Read`, `Fsys_Write`, `Fsys_Seek`, `Fsys_Flush`, `Fsys_Close`, `Fsys_Mkdir`, `pf2_errnum`,
  `pf2_inerrnum` and the `osal_*` messaging calls. It holds no appFw.so address.
- **Modules load after the card mounts.**
  - Its constructor starts a thread that polls drives `B X A C D E` for `X:/pilot` or `X:/DCIM`, every 250 ms for up
    to 60 s.
  - It reads `/pilot/modules.json`, copies each listed module to `/tmp_wbshd/pilot/modules` and `dlopen`s it there.
    The system loader relocates each one.
- **Two off switches.**
  - **`/pilot/disable`:** an empty file there skips every module.
  - **A fuse in `/pilot/fuse.log`:**
    - before loading a set of modules, it appends `ARMED` with the set's hash, then `COMPLETE` once they have loaded;
    - if a start finds `ARMED` for the same set, the last load never finished, so it writes `TRIPPED` and skips that
      set from then on.

    A crash loop caused by a module therefore ends by itself after one restart, with the card still in.
- **Installing.** Installs go through the camera's Jiritsu USB service, writing `/usr/lib/appFw.so` and
  `/usr/lib/pilot.so`. The installer clears the 512-byte `WBI2` header in `/dev/nflasha5`, and the guide asks for a
  battery pull after every install. The 6700's menu module, by contrast, is mostly fixed addresses (about 50 of them).

## What the A7 IV's files show

### The hook point is the same

`DT_INIT` is `0x1714d48`, identical in 6.02 and v0.1.3:

```
0x1714d48  fd7bbfa9  stp x29, x30, [sp, #-0x10]!
0x1714d4c  fd030091  mov x29, sp
0x1714d50  ca860494  bl  0x1836878
0x1714d54  fd7bc1a8  ldp x29, x30, [sp], #0x10     <- Pilot's hook instruction, same bytes
0x1714d58  c0035fd6  ret
```

- `libdl.so.2` is `DT_NEEDED`.
- PLT stubs: `dlopen` at `0x1716310` (GOT `0x5a57ac8`) and `dlsym` at `0x17166a0` (GOT `0x5a57c90`). The open-gate
  module already calls `dlsym` through that stub (layout.json `stock_functions`).
- glibc runs `DT_INIT` before the 370 `DT_INIT_ARRAY` constructors, so at the hook point none of Sony's C++ statics
  have run yet. That includes the menu registrars that 0.4 crashed in.

### The named functions exist

appFw.so imports every function Pilot's resident library resolves by name:
- `Fsys_Open`, `Fsys_Read`, `Fsys_Write`, `Fsys_Seek`, `Fsys_Close`, `Fsys_Mkdir`, `Fsys_Remove`, `Fsys_Rename`,
  `Fsys_FindFirst` and `Fsys_FindNext`, `Fsys_GetDriveLetter`, `Fsys_GetDriveInfo`, `Fsys_Mount` and `Fsys_Unmount`;
- `osal_snd_msg`, `osal_valloc_msg_wait`, `osal_free_msg`, `osal_set_msg_src`.

They are undefined in appFw.so and defined in a library it loads, so `dlsym(RTLD_DEFAULT, …)` finds them. The
import list only shows that these names exist. Two things still need checking:
- `Fsys_Stat`, `Fsys_Flush`, `pf2_errnum` and `pf2_inerrnum` are not imported by appFw.so, so they have to be looked
  up in the exporting library;
- every signature has to be checked against how the A7 IV calls it.

`/tmp_wbshd` exists in the A7 IV's root filesystem (`nflasha7`).

### Installing needs no USB service here

Our packages already rebuild `nflasha15`, which is mounted at `/usr`, through `ext_replace`. A resident
`/usr/lib/<loader>.so` would simply be one more file in the package. The `DT_INIT` branch and its island would come from
`compose_ap` like every other site. So no Jiritsu step is needed, and the install is the normal SD or USB firmware
install.

### Our current module is almost out of room

The open-gate module's RX window is `0x9063000` + `0xc000` (49,152 bytes). alpha-dci-0.5's module is 48,348 bytes,
which leaves 804. Every feature built into that fixed-address blob is subject to the 0.4 failure class (no relocations
at run time), now caught at build time by `_stored_addresses`. A module loaded with `dlopen` is relocated by the
loader and has no size cap.

### Menu pages: a working pattern exists, but its pool is small

- Open gate's menu (`native/open-gate/README.md`, "Menu item") takes over Erlla's Custom Shutter Sound page:
  - mid 1592, with its switch at mid 1593, listed under Media;
  - Erlla's firmware already registers mids 1592–1597 and option ids 5607–5630 in every native map, and its filters
    accept them;
  - the page's own controller (`0x2d7`) draws it.

  This works on the camera (check C8).
- An earlier build that added an item of its own to Media showed no title and couldn't be selected
  (`methods/11-open-gate/README.en.md`, "Limits").
- So a card module can already get a page by taking over ids that Erlla registered. That pool is 6 mids and 24 option
  ids, and open gate uses some of them.
- A Pilot-style page that lists modules and grows with them needs either more registered ids or the A7 IV equivalents
  of Pilot's menu table, page table, screen factories and button handlers. That is new reverse engineering, and the
  failed Media item shows it is easy to get wrong.

### The warm-boot snapshot decides what "remove the card" means

- **How the snapshot works:**
  - `nflasha5` holds a RAM image: 210,606,208 bytes, header `WBI2` in 6.02 and `WBI4` in v0.1.3, otherwise identical.
  - The kernel builds it itself: `vmlinux.bin` has `do_make_image`, `warm_boot_pm_begin`/`_prepare`/`_finish`, `%s:
    canceled to create wbi` and `checking opened files`.
  - `im.elf` (the init manager) writes `warm` to `/sys/power/state` on its power-off path, next to `pm_bootmode`.
  - The updater runs `ud_wbi_drop.elf` after an install.
  - Our builds change the marker to avoid restoring a stale image (`a7m4/firmware.py` writes `WBI3`; v0.1.3 ships
    `WBI4`).
- **What follows if a power-on restores that image instead of starting appFw.so:**
  - `DT_INIT` does not run, so the loader does not run either;
  - whatever was loaded when the image was made is already in memory;
  - a module loaded before the image was made comes back with the card removed, until something cold-boots the
    camera.

  This would explain why Pilot asks for a battery pull after installing and after enabling or disabling a module.
- **What the files don't show:**
  - when the A7 IV makes the image: at every power-off, or once and then reused;
  - whether `checking opened files` refuses an image while files are open, and on which paths (Pilot copies modules
    to `/tmp_wbshd` before loading them, which may be why).
- **Evidence from 0.4:** its crash at every power-on means those boots were cold. But that is expected straight after
  an install, which drops the image, and a crashing app never reaches power-off to make a new one.

## Startup cost

- The loader adds one `dlopen` at `DT_INIT` and a thread that waits for the card.
- Modules load after the card mounts, so they don't delay the start of live view. Features become active a moment
  later; how long is to be measured.
- The real cost depends on the snapshot: if the image restores most power-ons, the loader costs nothing at power-on.
  If adding the loader made the kernel stop building the image, every start would be cold. Cold and warm start times
  are to be measured on the camera (stopwatch or video, ten starts each).

## Tests to settle the open questions (camera required, after recovery)

All of these run on a working beta-0.2 camera with the Fastboot cable at hand.

1. **Is the image made at power-off?**
   - In the Fastboot session during recovery: read the first sector of `nflasha5` before writing, read-only. It should
     be `WBI4` as installed, unless the camera rewrote it.
   - After the first normal boot and switch-off of beta-0.2: read it again.
2. **Does `DT_INIT` run on every power-on?** Open gate's `og_power_on` already runs at every shooting-screen creation,
   restored or not. A counter bumped in `DT_INIT` and logged next to it would separate cold starts from restored ones.
   This is a small probe for an alpha build.
3. **Time to live view,** cold and warm, before and after the loader.

## Proposed shape (for the planning round, not decided)

- **Permanent part:**
  - the `DT_INIT` branch plus an island that `dlopen`s `/usr/lib/og-loader.so` and carries on whatever happens;
  - the loader library: card polling, the `disable` file, Pilot's fuse, staging to `/tmp_wbshd`, and the
    `dlopen`/`dlsym` of each module's init function.
- **Every feature as a module on the card,** with its own log, applied only after the card has mounted. Each module
  checks feature combinations itself, since Sony's greying no longer guards them.
- **Menu:**
  - start with one taken-over Erlla page that lists modules and their switches;
  - choose between more Erlla ids and the full page tables once a module needs more than that page offers.
- **Open gate stays in the fixed module** until a card module replaces it, so beta users lose nothing.
