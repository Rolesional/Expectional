# Expectional

Expectional is an open source, external, read only, kernel level Counter-Strike 2 gameplay enhancer.

Driver: https://github.com/Rolesional/expectional-driver

That is a lot of descriptive words, so what does each of them mean?

- `Expectional` — the name of this project
- `open source` — the code is public so anyone can read and learn from it
- `external` — no DLL is ever injected into the target process
- `read only` — the CS2 process is never written to, only read; scanning process memory for writes cannot detect it
- `kernel` — process data is obtained through a kernel driver instead of user level WinAPIs
- `overlay` — drawing (ESP, menu) happens in user mode on a separate overlay window

The project covers kernel driver communication, process memory reading through a driver, entity list resolution, world BVH autowall, VPK parsing and an ImGui based overlay in one codebase.

---

# WARNING

Expectional is **not** a plug and play solution.

- **You may get a BSOD.** The kernel driver is not flawless; a wrong offset, a CS2 update or a driver mismatch can end in a blue screen. Use at your own risk.
- **Core Isolation, Memory Integrity and similar settings must be disabled** for the driver to be mapped. Otherwise the driver will fail to load.
- **Anticheat risk.** Use the Aimbot and Triggerbot, which fall under VAC Live, at your own risk. VAC Live is a server side system that scans AI assisted gameplay and it cannot be bypassed.
- The driver is mapped through a third party vulnerable driver (kdmapper flow). Loaders of this kind are always risky.

---

# Features

Because the software is read only (for now), features like a skin changer are impossible. Despite that limitation, Expectional supports:

- **Aimbot**
  - Target selection, FOV, smooth and keybind are configurable
  - Reading side runs in kernel (entity, bone, visibility), application runs in user mode (mouse event)
  - Per weapon settings are stored by `Weapon Based Config`

- **RCS (Recoil Control)**
  - Reads aim punch data through the driver and compensates it with mouse movement in user mode
  - Configurable per weapon

- **Triggerbot**
  - Fires as soon as an enemy enters your crosshair
  - Reaction time, delay and a `Head only` hitbox option
  - With autowall enabled, wall targets are evaluated with the autowall result instead of engine visibility

- **Weapon Based Config**
  - Separate aimbot / RCS / trigger settings for every weapon
  - Settings are written to config files and can be swapped per match

- **ESP**
  - Box (normal and corner), skeleton, armor bar, health (bar and text), snapline
  - Configurable colors to tell teammates and enemies apart
  - Extra info such as name, weapon, money and bomb state

- **Grenade ESP**
  - Grenade trajectory, smoke/molotov/HE radius and timing
  - World mesh (world BVH) based autowall / penetration math, so wall info stays correct

- **Lineup Helper**
  - Uses workshop guide lineup data, can get lineups from ur steam workshop files.
  - Lineup files live in `%LOCALAPPDATA%\Expectional\lineups`, a readme is created in that folder automatically

- **Spectator List**
  - Lists the players currently watching you / the observer target, read from the observer services in kernel

- **Rank Revealer**
  - Premier elo: win, tie and loss estimates
  - FACEIT rank info
  - Competitive rank icons plus win/loss data

- **Bomb Timer**
  - Time until detonation, defuse timer, defuse state and bomb site

- **Hitsound**
  - Plays a sound on hit

- **Hitmarker**
  - Visual marker on hit, alongside the autowall penetration crosshair

- **Bunnyhop**
  - Runs on jump events in user mode, the menu offers a suggested bind

- **Keybind List**
  - Overlay list showing all active keybinds
  - Default menu key is `HOME` and can be rebound inside the menu

- **Window Radar**
  - Map and player radar in a separate window
  - May not work on workshop maps

- **Cloud Radar**
  - Valve maps only
  - Requires an internet connection, publishes map/player data through a session

- **Stream Proof**
  - The overlay is invisible to OBS, Discord and other screen capture tools
  - The overlay window is excluded from capture

---

# Technical Overview

Where each part of the pipeline runs:

| Operation | Layer |
| --- | --- |
| CS2 connection (process attach) | Kernel |
| Memory reading | Kernel |
| `client.dll` / `engine2.dll` address resolution | Kernel |
| `vphysics2.dll` address resolution | Kernel |
| Pattern scan | Kernel |
| Process alive check | Kernel |
| Overlay rendering | User mode |
| Aimbot, Triggerbot, RCS, Bhop application | User mode (reading side is in the driver) |
| VPK parsing | User mode (located by file path and parsed) |

Offsets are read from local files only; there is no version check and no network access.

---

# How to use / Getting started

1. Extract the archive.
2. Inside you will find `Expectional.exe`.
3. Run `Expectional.exe`. The driver is loaded automatically.
   - The driver is embedded inside Expectional as bytes (encrypted header stream). Reviewers can verify this in the source code.
   - No external `.sys` file has to be copied next to the exe.
   - The binary requests administrator rights (`RequireAdministrator`), run it as admin.
4. If the driver cannot load, the console prints an error; disable Core Isolation / Memory Integrity according to the message and retry.
5. Expectional then starts right away. **CS2 must already be running.**
6. Press `HOME` for the menu, close the window to exit.

**Config folder:** `%LOCALAPPDATA%\Expectional\configs`
**Lineup folder:** `%LOCALAPPDATA%\Expectional\lineups`

---

# Building

Requirements:

- Visual Studio 2022 or Build Tools 2022 (MSVC v143 toolset, x64)
- Windows SDK
- Windows WDK (If u want to compile the driver)
- Desktop development with C++ workload

Steps:

1. Extract the repository into a folder.
2. Open `cs2 usermode.sln` in Visual Studio.
3. Select configuration `Release` and platform `x64`.
4. Build Solution (Ctrl+Shift+B).

From the command line:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
msbuild "cs2 usermode.sln" /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143
```

Output: `x64\Release\Expectional.exe`

### Offsets folder (required)

An `offsets` folder must exist next to the executable and contain the two dumped files:

```
x64\Release\
  Expectional.exe
  offsets\
    offsets.hpp
    client_dll.hpp
```

If the offsets are missing, the program exits with:

```
Offsets not found pls put dumped offsets at offsets folder.
```

If the offsets are valid, the console prints a confirmation such as:

```
[offsets] Successfully loaded from local offsets folder: ...\offsets\
[offsets] OK: local offsets folder | dwEntityList=0x... dwLocalPawn=0x... viewMatrix=0x...
```

### Updating offsets

After a CS2 update the offsets become stale. Dump them again with [cs2-dumper](https://github.com/a2x/cs2-dumper) and copy the produced `offsets.hpp` and `client_dll.hpp` into the `offsets` folder. No offsets are hardcoded in the source, the program only reads that folder.

---

# Troubleshooting

- **Driver does not load:** check that Core Isolation / Memory Integrity is disabled and run the program as administrator.
- **`Offsets not found...`:** verify the `offsets` folder sits next to the exe and the files are named `offsets.hpp` and `client_dll.hpp`.
- **Stuck on `waiting for entity list...`:** the offsets do not match your CS2 build, refresh the dumps. Start Expectional while CS2 is in the main menu.
- **`client.dll is missing after driver init.`:** bring CS2 to the main menu and restart Expectional.
- **Overlay missing or broken while minimized:** switch the resolution to windowed, then back to fullscreen windowed.
- If you keep hitting issues, write in the thread on the Discord server.

---

# VAC

The driver and overlay are designed to stay under the radar of VAC by omitting the identifier, XOR encrypting strings and only reading memory. This is not a guarantee.

**VAC Live** is a server side system that scans AI assisted gameplay. There is no bypass for the Aimbot and Triggerbot. Use them at your own risk and keep Overwatch / demo reviews in mind as well.
