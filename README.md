# Discord Game Emulator

A lightweight, standalone Win32 application designed to create background dummy processes for Discord Rich Presence. The program fetches game directories from a maintained JSON within this repo, spawns minimal dummy processes to emulate game activity, and performs automatic cleanup upon completion.

## Features

* **Standalone Executable:** Written entirely in plain C using the Win32 API with zero external dependencies.
* **Multiple Emulation Modes:** Supports Single, Queued, and Simultaneous emulation.
* **Game Database Search:** Built-in search window to find supported games.
* **Minimal Resource Footprint:** Dummy processes idle at under 1MB of RAM usage.
* **Automatic Cleanup:** Processes safely wipe their temporary generated folders upon completion or premature termination.

## Download
Download the latest version of `Discord_Game_Emulator.exe` under [releases](https://github.com/swypieuwuu/Discord-Game-Emulator/releases).
This project supports Windows only. Support for other operating systems is not planned.

## Building
Clone this repo or download it as zip.

### Using GCC / MinGW
```bash
windres resource.rc -O coff -o resource.res
gcc -mwindows -o Discord_Game_Emulator.exe main.c resource.res -lwininet -lshell32 -luser32 -lgdi32 -ldwmapi
```

## Usage

1. **Launch:** Run the executable.
2. **Search:** Click the magnifying glass icon to open the database. Double-click a game to auto-fill its details.
3. **Configure:** Enter a game name, an optional custom executable path, and a time duration in seconds.
4. **Select Mode:** Use the toggle button in the top right to switch between Single Launch, Queue Mode, and Simultaneous Mode.
5. **Execute:** Click the **Emulate** button at the bottom of the window to begin emulation.

## Modes
1. **Single Game Emulation:** Emulate one game only
2. **Queued Emulation:** Emulate games in a queue, one game after the other.
3. **Simultaneous Emulation:** Emulate multiple games at once.

## AI Declaration
This project was written with the assistance of AI.
