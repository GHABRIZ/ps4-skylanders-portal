# Skylanders Portal for PS4 (GoldHEN plugin)

A GoldHEN plugin that makes a jailbroken PS4 think a real **Skylanders Portal of Power** is plugged in.
No real portal is needed. You choose your figures from your phone's browser, and they appear in the game.

Made for every PS4 Skylanders game: **Swap Force, Trap Team, SuperChargers and Imaginators**.

> Status: **v0.1.0, first test build.** Not tested on a PS4 yet. See `NOTES.md`.

## What it does
- A virtual USB portal (VID 1430 / PID 0150) that speaks the real portal protocol
- A phone page at `http://<ps4-ip>:8080`: 8 spots on the portal, search, folders, place and take off figures
- Saves progress: when the game writes to a figure (level, gold, hats, upgrades), it is saved and comes back
  the next time you place that figure. Your original `.bin` files are never changed.
- "Reset" button per figure to go back to the original file
- Upload `.bin` files straight from your phone, or copy whole folders with FTP
- Remembers what was on the portal for the next launch
- A log you can open from the phone page (for bug reports)

**No figure files are included.** Use dumps of figures you own.

## Install
1. Copy `skylanders_portal.prx` to `/data/GoldHEN/plugins/` (FTP works, for example GoldHEN's FTP server).
2. Find your game's CUSA ID (below) and add it to `/data/GoldHEN/plugins.ini`:
   ```
   [CUSAxxxxx]
   /data/GoldHEN/plugins/skylanders_portal.prx
   ```
3. Copy your figure `.bin` files to `/data/GoldHEN/skylanders/figures/` (sub-folders are fine).
   The folder is created the first time the plugin runs, or you can make it yourself.
4. Start the game. On your phone (same Wi-Fi), open `http://<ps4-ip>:8080`.
   Your PS4's IP is in Settings → Network → View Connection Status.

### Find your CUSA ID
Each game and region has its own ID (for example, a US disc and an EU disc are different).
Easy ways to see it: the GoldHEN notification when the game starts, a homebrew launcher like Itemzflow, or
FTP into `/user/app/` and look for the folder that appears when the game is installed.

## Files on the PS4
All in `/data/GoldHEN/skylanders/`:
| Path | What it is |
|---|---|
| `figures/` | your `.bin` dumps (never modified) |
| `figures/Uploaded/` | files you uploaded from the phone page |
| `saves/` | progress the game wrote to each figure. Back this folder up! |
| `state.txt` | which figures were on the portal |
| `log.txt` | what the plugin saw (send it with bug reports) |

## Tips
- Swap Force swappers: place the top and the bottom as two figures.
- Trap Team: traps are figures too. Place the trap the game asks for.
- Only 1KB (1024 byte) dumps work. NFC (.nfc) files must be converted to .bin first.
- Use a real portal? Remove the plugin line from `plugins.ini` for that game.

## Build it yourself
Needs the [OpenOrbis PS4 toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) and the
[GoldHEN Plugins SDK](https://github.com/GoldHEN/GoldHEN_Plugins_SDK) next to this folder.
```sh
export OO_PS4_TOOLCHAIN=/path/to/PS4Toolchain
make          # makes skylanders_portal.prx
make test     # PC test of the portal protocol, no PS4 needed
test/run_host_sim.sh /path/to/your/bins   # runs the whole plugin on a Linux PC with a fake game
```
Or push to GitHub: `.github/workflows/build.yml` builds the `.prx` for you (Actions tab → artifact).

## Credits
- [infinity-base-ps4](https://github.com/Kwarz-git/infinity-base-ps4) by Kwarz (MIT): the PS4 USB hooking approach and fake-device code this plugin builds on
- [RPCS3](https://rpcs3.net/) and [Dolphin](https://dolphin-emu.org/): Skylanders portal protocol reference
- OpenOrbis toolchain, GoldHEN Plugins SDK

## Disclaimer
Unofficial, for use with games and figures you own. Not affiliated with Activision or Toys for Bob.
Skylanders is a trademark of Activision. Use at your own risk, and back up your saves.
