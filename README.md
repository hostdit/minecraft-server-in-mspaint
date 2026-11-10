# Minecraft server inside Microsoft Paint

A working Minecraft 1.8.9 server that runs inside Microsoft Paint. The process listening on port 25565 is mspaint.exe. There's no helper process and nothing else running.

The canvas is the world, seen from above. Draw in Paint and the pixels turn in to blocks. Place a block in Minecraft and Paint paints it. Undo works on it and Save keeps it.

## Why

Excel, Outlook, OBS, Obsidian, Blender, PowerPoint, VLC, Word, The Sims 4, Terraria, FL Studio, VS Code, DaVinci Resolve, Unity, Firefox, Garry's Mod, the whole planet, an iPhone, Valheim, Minecraft itself, File Explorer, and now Paint.

## What's different about this one

Paint has no extension point, no plugin API and no scripting engine. The server gets in by injection, because Paint gives you no other way.

## What it does

- Server list ping with a paint palette favicon and an MOTD of `Microsoft Paint | pid 1234`
- Offline mode login, no encryption, no compression
- A 7x7 chunk creative flat world of bedrock, dirt and grass
- Players see each other walk, look and hold items
- Chat, with the last 100 lines kept
- Keep alives every 15 seconds
- Autosave about a second after the last change

In Paint:

| You do | Minecraft does |
| --- | --- |
| draw a red line | a line of TNT appears, the nearest block to Paint's red |
| draw green | grass |
| rub something out | blank white is plain grass, so the lawn comes back |
| open a picture before injecting | the world becomes the picture |

| Minecraft does | Paint does |
| --- | --- |
| place a block anywhere in a column | paints the top block of that column |
| place stone on grass | paints grey |
| dig it up | the pixel goes back to white |

## What it doesn't do

The world is 112x112 blocks and maps to the top-left 112x112 pixels. Make the canvas 112x112 and maximise Paint. The bridge needs at least 300% zoom and sets it for you. A canvas that doesn't fit the view pauses the bridge, and anything placed in the meantime paints when it comes back.

**Placed blocks use the 20 swatches.** A block lands on the nearest one. Iron comes out lavender.

**It takes your Pencil.** A placement leaves Paint on the Pencil at its smallest size, on whatever colour it last painted. Pick your brush again afterwards.

**Paint has to be on screen.** The pen lands wherever it's pointed, so placements wait if something's covering the canvas.

**English (UK) only.** Swatch and tool names are read in the language this was built on. Another language won't find "Colours".

It's a server in the sense that a client connects to it and receives a world. Set your expectations accordingly.

## Requirements

Windows 11, ARM64 or x64. Current Paint from the Store.

Minecraft Java 1.8.9, protocol 47.

To build, Visual Studio 2026 or its Build Tools with the C++ workload and a Windows SDK, plus CMake 3.25 or newer.

## Install

**1.** Build. On ARM:

```
cmake --preset windows-arm64
cmake --build --preset windows-arm64
```

Swap `arm64` for `x64` on Intel or AMD.

**2.** Deploy. This copies the DLL and `mcpaint_loader.exe` to `%LOCALAPPDATA%\McPaint\bin`.

```
powershell -ExecutionPolicy Bypass -File scripts\deploy.ps1
```

**3.** To let other machines join, from an admin PowerShell:

```
powershell -ExecutionPolicy Bypass -File scripts\open-firewall.ps1
```

## Use

Start Paint, resize the canvas to 112x112 with Image > Resize and maximise the window. Then:

```
cd %LOCALAPPDATA%\McPaint\bin
mcpaint_loader.exe
```

Paint jumps to 300% and the server's listening. Connect Minecraft 1.8.9 to `localhost:25565`, or the PC's IP from another machine.

### Scripts

| Script | What it does |
| --- | --- |
| `deploy.ps1` | copies the latest build under a timestamped name. The loader picks the newest |
| `restart-paint.ps1` | restarts Paint |
| `open-firewall.ps1` | lets Paint through the firewall |

A running Paint never lets go of a DLL it has loaded, so every deploy uses a new file name.

The log is `%LOCALAPPDATA%\McPaint\log.txt`, and also goes to `OutputDebugString`.

`tools/standalone` builds `mcx_standalone.exe`, the same server without Paint, for testing.

### From a Mac

Windows in a VM. On the Mac:

```
socat TCP-LISTEN:25565,fork,reuseaddr TCP:<vm-ip>:25565
```

Then connect to `localhost:25565`.

## Proving it's Paint

```
netstat -ano | findstr :25565
tasklist /fi "pid eq 1234"
```

The pid is mspaint.exe, the same pid as in the MOTD.

## How it works

**Injection.** The loader opens mspaint.exe, writes the DLL's path in to it and starts a remote thread on `LoadLibraryW`. `DllMain` starts a worker thread, which runs once the loader lock is released.

**MSIX.** Store Paint is full trust with file system virtualisation off, so injection, the socket and `%LOCALAPPDATA%` all behave normally.

**Reading the canvas.** Modern Paint draws through composition, so `GetDC` and `BitBlt` read solid black. `PrintWindow` with `PW_RENDERFULLCONTENT` sees it. The canvas position, zoom and size come from Paint's accessibility tree. Ten times a second the DLL samples the middle of each document pixel. A pixel has to hold still for two frames before it counts, so a passing brush outline doesn't build anything.

**Painting back.** A placed block queues its pixel. The DLL picks Pencil and the nearest swatch through accessibility, then taps the pixel with a synthetic pen from `InjectSyntheticPointerInput`. Paint can't tell it from a real pen, which is why undo and save work, and it doesn't move your mouse. The pixel's marked as an echo so the next capture doesn't turn it back in to a block.

**Mapping.** Pixel `(px, py)` is block `(px - 48, 3, py - 48)`, by colour distance over the 1.8.9 palette.

## Layout

```
core/                portable C++20 server, shared with the File Explorer build
paint/               the DLL: capture, diffing, UI and pen driver, palette
loader/              the injector
tools/standalone/    the server without Paint
scripts/             deploy, restart Paint, firewall
```

## Licence

MIT. Do what you like with it.

Minecraft belongs to Mojang. Paint belongs to Microsoft. Nothing from either ships here.
