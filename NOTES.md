# Notes

## v0.1.6 (2026-10-30): fix "A toy on the portal has a problem" (Trap Team)
### What happened (log from Trap Team on v0.1.4, Bushwhack, Eon's portal intro at the Academy)
- At the portal intro, Eon talks *through the portal's speaker*. The game sends that sound to the portal on
  USB endpoint 0x02.
- Since v0.1.2 the plugin treated any packet on that endpoint starting with a command letter as a command.
  Sound bytes are random, so some packets looked like 'W' (write block), 'R' (reset), 'A', 'C'... The log
  shows exactly that: "cmd 'W' len 32", "cmd 'R' len 32" right after the speaker started.
- Result: junk got written into Bushwhack's figure data and the portal was reset → the game said the toy has
  a problem. **My bug from v0.1.2.**

### Fix
- Endpoint 0x02 is now always treated as speaker sound and ignored. Commands only come by SET_REPORT
  (which is how Trap Team, and shadPS4's portal, send them).
- Bushwhack's saved progress is damaged: press Reset (↻) next to him on the web page. His original .bin
  was never touched.

### Tested (PC)
- New test: a speaker packet that looks exactly like "write block 8" does not change the figure, and one
  starting with 'R' does not reset the portal. **v0.1.5 fails this test, v0.1.6 passes.**
- Whole-plugin simulation 45/45, protocol test 101/101. Compiles clean against OpenOrbis v0.5.2-v0.5.4.

### Not tested
- On the PS4 (Trap Team portal intro, SuperChargers, Imaginators).

## v0.1.5 (2026-10-30): match shadPS4's PS4 portal
### What the v0.1.4 tests showed
- Imaginators sees the portal, but placed figures don't show up in the game (no error). No log yet.
- SuperChargers still never sees the portal.
- The page stopped opening on the laptop at one point ("site can't be reached"). Cause not confirmed.

### Research
- **shadPS4** (the PS4 emulator) has an emulated Skylanders portal that runs these same PS4 games. I compared
  it line by line with this plugin. Differences found, and fixed to match it:
  1. **Where replies are delivered.** shadPS4 completes the game's USB reads *inside the game's own
     sceUsbdHandleEvents call*, on the game's thread, one per call. We completed them from our own thread at
     any moment. SuperChargers calls HandleEvents (seen in its log), so it probably expects shadPS4's way.
     Now: as soon as the game calls HandleEvents on the open portal, replies are delivered there. If the game
     stops calling it for 1 s, our worker thread takes over again (the old way, which Trap Team used).
  2. **Status before activation.** shadPS4 always answers reads with status. v0.1.4's "silent until 'A'"
     change is switched off again.
  3. **Kernel driver.** shadPS4 reports the kernel driver as active until the game detaches it. Same now.
  4. **'R' (reset) reply:** now 52 02 1B, like shadPS4 and Dolphin (was 52 02 18, from RPCS3).
  5. CheckConnected answers "connected".
- Descriptors (device, config, endpoints 0x81/0x02, HID report) already matched shadPS4.

### More logging
- Every status change sent to the game ("status to game: ..."), the first 40 replies, and the full bytes of
  the first commands of each kind. This shows whether the game hears that a figure was placed and whether it
  reads it.

### Tested (PC)
- New tests: status before activation, kernel-driver active then detached, reads completed inside
  HandleEvents (and the worker leaves them alone), worker takes over when HandleEvents stops.
- Whole-plugin simulation 42/42, protocol test 101/101.
- Compiles with no errors/warnings against OpenOrbis v0.5.2 / v0.5.3 / v0.5.4 headers.

### Not tested
- On the PS4: Trap Team (re-test, start-up changed), SuperChargers, Imaginators.
- Two files changed this time: `source/main.c` and `source/portal.c`.

## v0.1.4 (2026-10-30): SuperChargers fix attempt + phone diagnostics
### What the v0.1.3 tests showed
- Trap Team still works on v0.1.3, and figure progress stayed saved. Saving works on a real PS4.
- SuperChargers log: the game finds the portal, opens it, claims it, then keeps reading it (2000+ reads, and
  every reply reached the game). But it **never sends a single command**: no reset, no activate.
- The S26 Ultra still can't open the page.

### SuperChargers change
- Before, the virtual portal sent status reports from the very first read. A real portal stays **silent**
  until the game turns it on, so the game's reads time out first. SuperChargers seems to wait for that
  silence before it starts talking. Now the portal is silent until the game sends 'A' (activate), and reads
  end as "timed out" when their time is up, like real USB.
- Safety net: if a game sends no command for 5 s, the portal goes back to always sending status (the old
  behaviour that Trap Team worked with). The log says when that happens.
- The log now shows each read's timeout value.

### Phone diagnostics
- The log now shows every device that connects to the web page (by IP) and the first 30 requests. If the
  phone never shows up, it never reached the PS4: that's a network problem, not the plugin.

### Tested (PC)
- New tests: silent before activation, async read times out after its timeout, sync read times out.
- Whole-plugin simulation 39/39, protocol test 101/101.
- Compiles with no errors/warnings against OpenOrbis v0.5.2 / v0.5.3 / v0.5.4 headers.

### Not tested
- On the PS4. **Re-test Trap Team too**: it now sees a silent portal at the start, like real hardware.
- Whether SuperChargers' reads have a timeout. If they don't (timeout 0), the log will show it, and the
  game will hear status after 5 s, the same as before.
- The phone.

## v0.1.3 (2026-10-30): phone fix + SuperChargers diagnostics
Includes everything from v0.1.2 below (which was never put on the PS4).

### Your test results so far (with v0.1.1)
- **Trap Team works!** Gusto loaded from the figures folder and the game took it. First real PS4 success.
- SuperChargers: finds and opens the portal, then never talks to it ("Portal Disconnected"). v0.1.2's changes
  and logging target this.
- Web page works on the laptop, but the S26 Ultra says "too many retries".

### Phone fix
- The web server answered one connection at a time and waited up to 3 s on each. Phone browsers open spare
  connections and leave them idle, so every real request got stuck behind them, and the phone gave up.
- Now every connection gets its own thread (up to 8). Changing figures still happens one at a time.

### Tested
- New PC test: with 4 idle connections open, the page still answers in ~7 ms; 8 requests at the same time
  all succeed. **The old v0.1.1 server fails this same test** (2 s wait, 0/8), which matches the phone problem.
- Whole-plugin simulation 36/36, protocol test 101/101.
- Compiles with no errors/warnings against OpenOrbis v0.5.2 / v0.5.3 / v0.5.4 headers.

### Not tested
- On the S26 Ultra and the PS4: I can't run a real phone here. The idle-connection cause fits the error, but
  it's not confirmed until you try.
- SuperChargers changes (from v0.1.2).

## v0.1.2 (2026-10-30): first PS4 test - diagnostics build
### What your first test showed (SuperChargers, FW 13.52)
- The plugin loads, the web page starts, libSceUsbd is found (28/28 functions) and 20 USB pointers are swapped.
- The game asks for the device list, sees the virtual portal, and **opens it**.
- But the game never sent a single portal command after opening it, so it still said "Portal Disconnected".

### Changes
- Callbacks: before, the plugin only called the game back if that code was in the game's main file. If
  SuperChargers keeps its USB code in a separate library, every reply was silently dropped. Now any loaded
  code is accepted, and a rejected callback is written to the log.
- Commands sent on the interrupt OUT endpoint (instead of SET_REPORT) are now understood too.
- Much more logging: the name of every USB function the game uses, the first calls it makes on the portal,
  the first 40 transfers (type, endpoint, bytes), and other control requests.
- Moved more buffers out of .bss (1.1 KB left).

### Tested
- Compiles with no errors/warnings against OpenOrbis v0.5.2 / v0.5.3 / v0.5.4 headers.
- Protocol test 101/101, whole-plugin PC simulation 34/34.

### Not tested
- On the PS4. This build may or may not connect; either way its log will show exactly where the game stops.

## v0.1.1 (2026-10-30): build fix
- Fixed the GitHub build error "no member named 'prot' in 'OrbisKernelVirtualQueryInfo'". The OpenOrbis
  release GitHub downloads names those fields differently from the newer headers. The plugin now uses its
  own copy of that struct, so the field names don't matter.
- Tested: compiles with no errors or warnings against the OpenOrbis v0.5.2, v0.5.3 and v0.5.4 headers;
  protocol test (101 checks) and whole-plugin PC simulation (34 checks) still pass.
- Not tested: the GitHub build itself (your next run), and still nothing on a real PS4.

## v0.1.0 (2026-10-30): first test build

### What's in it
- GoldHEN plugin `skylanders_portal.prx` (source in `source/`)
- Virtual wired portal: USB 1430:0150, HID, 32-byte reports, 8 figure spots
- Portal commands: A (activate), C (colour), J, L, M (no speaker), Q (read), R (reset), S, V, W (write)
- Status reports every ~8 ms, with ADDED → READY and REMOVING → gone, like a real portal
- Works through both ways a game can talk USB: async transfers (submit + callback) and the simple
  blocking calls (ControlTransfer / InterruptTransfer). Also handles open-by-VID/PID and descriptor requests.
- Phone page: portal spots, search, folder filter, place/take off/take all off, reset progress,
  upload .bin, reload library, log viewer. Shows the portal light colour the game sets.
- Progress saved to `saves/` about 1 second after the game stops writing, and again when a figure is taken off.
  Original `.bin` files are never touched.
- Log file + `/api/log`, made so you can send me what happened on your PS4.

### Tested (on a PC, not a PS4)
- Protocol test: 101 checks pass, using made-up data **and every one of your 691 figure dumps** (main folder).
- Whole-plugin simulation on Linux (`test/run_host_sim.sh`): the real `main.c` runs with a fake game and a
  fake phone. 34 checks pass. They cover: device found, descriptors, open, R/A start-up, place, ADDED/READY,
  block reads match the .bin, writes saved to `saves/` with the original untouched, sync calls, take off and
  REMOVING, progress restored, reset, cancel, upload (and blocking bad names and sizes), page, log,
  8-figure limit, state.txt.
- Compiles for the PS4 target with no warnings (`-Wall -Wextra`) against the OpenOrbis headers.
- Phone page checked in a phone-size browser.

### NOT tested yet
- **Running on a real PS4 / any Skylanders game.** That's the next step, your test.
- The `.prx` itself is not built yet: the OpenOrbis toolchain download was blocked in my workspace. Build it
  with the GitHub workflow (also untested) or a local toolchain.
- Whether the Skylanders PS4 games use libSceUsbd the same way Disney Infinity does (the plugin is built on
  that assumption; the log will tell us).
- FW 13.52 specifically (the Infinity plugin was tested on 11.52).
- Traps in Trap Team, vehicles in SuperChargers, crystals in Imaginators, swappers in Swap Force.
- The 'R' reply value (02 18, from RPCS3). If Trap Team won't accept traps, this is the first thing to change.

### Known limits
- 8 figures on the portal at once (same as a wired portal).
- No portal speaker sound (Trap Team villain voices play from the TV instead).
- No pictures yet: letter badges only. Pictures come before the public release.
- 56 files in your .rar failed to extract (the archive looks damaged). Most were in the extra "v2" and
  "Converted Dumps" folders, but some were in the main folder too, e.g. a few Trap Team villains and Imaginators crystals.
