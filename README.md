# Kingdom Come: Deliverance Head Tracking

![Kingdom Come: Deliverance running with this mod](https://raw.githubusercontent.com/itsloopyo/kingdom-come-deliverance-headtracking/main/assets/readme-clip.gif)

*Gameplay footage from Kingdom Come: Deliverance, captured with this mod running. The game, its assets and all footage of it are copyright [Warhorse Studios](https://warhorsestudios.cz/); the clip is reproduced here solely to demonstrate what the mod does. This mod is not affiliated with, endorsed by, or supported by Warhorse Studios.*

An unofficial head tracking mod for Kingdom Come: Deliverance that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

> **Status: pre-release.** This has not been comprehensively tested and may contain
> game breaking bugs

## Features

- **Decoupled look and aim** - your head moves the picture; aim, raycasts and combat stay on your mouse
- **6DOF positional tracking** - lean and peek with head position, limited so you never clip through Henry
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Kingdom Come: Deliverance on Steam](https://store.steampowered.com/app/379430/), or the Xbox Game Pass version. Each store ships its own build of the game and the mod carries a profile for each; on a build it does not recognize it stays dormant rather than misbehave.
- A head tracker: a webcam through [OpenTrack](https://github.com/opentrack/opentrack)'s `neuralnet` tracker, TrackIR, Tobii, a VR headset, or a phone app that speaks the OpenTrack UDP protocol.
- 64-bit Windows 10 or 11.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Kingdom Come: Deliverance**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the [Releases page](https://github.com/itsloopyo/kingdom-come-deliverance-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds the game, drops the ASI loader and the mod in beside `KingdomCome.exe`, and records what it installed so `uninstall.cmd` can put the game back exactly as it found it.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242` (see below).
5. Launch the game.

The mod disables the game's built-in Tobii integration while head tracking is
enabled, preventing it from adding a second camera movement. Toggling the mod
off restores the previous Tobii settings. Tobii through OpenTrack keeps working.

[Lopari](https://lopari.app) installs and launches it for you in one click.

If the installer cannot find your game, point it at the folder yourself. Either
pass the path as an argument:

```powershell
install.cmd "D:\Games\KingdomComeDeliverance"
```

or set the environment variable the installer reads:

```powershell
$env:KINGDOM_COME_DELIVERANCE_PATH = "D:\Games\KingdomComeDeliverance"
```

Give it the game's own top folder, not the folder the executable is in. On Steam
and GOG that is the folder containing `Bin\Win64\KingdomCome.exe`; on Xbox Game Pass
it is the `Content` folder, which holds `KingdomCome.exe` directly.

### Manual Installation

Copy two files in beside `KingdomCome.exe`:

```
dinput8.dll                                 (from vendor\ultimate-asi-loader\)
KingdomComeDeliveranceHeadTracking.asi      (from plugins\)
```

The two stores put `KingdomCome.exe` in different places, and the loader only
looks in the folder the executable is in, so this is the one detail worth
checking before you copy:

| Store | Where the two files go |
|-------|------------------------|
| Steam, GOG | `<game>\Bin\Win64\` |
| Xbox Game Pass | `<XboxGames>\Kingdom Come- Deliverance\Content\` |

The installer ZIP mirrors the Steam layout, so its files sit under `Bin\Win64\`
inside the archive. For an Xbox Game Pass install, take them out of that folder and
drop them straight into `Content`.

`dinput8.dll` is Ultimate ASI Loader. `WHGame.dll` imports DirectInput 8
directly and the game folder is searched before System32, so the loader picks
itself up with no launch options. If you already run another ASI loader there,
keep yours and copy only the `.asi`.

The Nexus ZIP ships the `.asi` on its own for exactly this case: extract it over
the game folder and supply your own ASI loader.

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action                        | Nav-cluster | Chord          |
|-------------------------------|-------------|----------------|
| Toggle tracking               | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode           | `Page Up`   | `Ctrl+Shift+G` |
| Toggle world/camera-local yaw | `Page Down` | `Ctrl+Shift+H` |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

## Configuration

`HeadTracking.ini` is written next to `KingdomCome.exe` on first launch, in the
same folder you copied the `.asi` into.

```ini
[HeadTracking]
UdpPort=4242
; Start with head tracking already on.
EnableOnStartup=true
; Yaw about the world up-axis so the horizon stays level. Off yaws about
; the camera's own up-axis, which leans the view on pitched turns.
WorldSpaceYaw=true
; Smoothing for a tracker running on this machine (loopback). 0 = none.
LocalSmoothing=0.0
; Smoothing for a tracker reaching this machine over the network. A tracker
; sending to this PC's LAN address instead of 127.0.0.1 counts as remote -
; the classifier sees a transport, not a machine.
RemoteSmoothing=0.15
MaxExtrapolationFraction=0.5

[Camera]
; Vertical field of view in degrees - the same number the game's own
; Vertical FOV setting carries, but not limited to its 60-75 range. 0 leaves
; whatever the game is set to. A wider view means less head turning to see
; the same thing; 65 is the game's default and 90 is a common choice.
; Saving the game's graphics settings puts its own value back until the next
; launch.
FieldOfView=0

[Position]
; 6DOF lean. Limits are meters.
Enabled=true
LimitX=0.30
LimitY=0.20
LimitYDown=0.20
LimitZ=0.40
LimitZBack=0.10

[Hotkeys]
; Windows virtual-key codes. Ctrl+Shift+Y / G / H work as alternatives.
ToggleKey=0x23
PositionKey=0x21
YawModeKey=0x22
```

There is deliberately no sensitivity or axis-inversion setting. Shape the pose in
your tracker app instead, so one profile behaves the same in every game.

`FieldOfView` accepts 40 to 120 degrees. Anything outside that is refused rather
than clamped, and the log names the value it rejected.

## Troubleshooting

Everything the mod does is written to `HeadTracking.log` next to
`KingdomCome.exe`. The previous session is kept as `HeadTracking.prev.log`.

**Mod not loading**

- No log file at all means the ASI loader is not loading. Confirm `dinput8.dll`
  and the `.asi` are both in the folder `KingdomCome.exe` is in - `Bin\Win64` on
  Steam and GOG, `Content` on Xbox Game Pass. Neither belongs in the game's top
  folder.
- A log line saying *staying dormant* means the mod did not recognize your
  `WHGame.dll`. The line says whether the game is newer or older than the builds
  it knows about; open an issue with it. The line above it names the profile
  that did match on a working install, `steam-win64-...` or `gdk-win64-...`
  according to where you bought the game.

**No tracking response**

- `udpData=NO` in the heartbeat line means no tracker data is arriving. If the
  same line says `udp=listening`, the socket is open and the tracker is what to
  check: confirm OpenTrack is started and its output is UDP `127.0.0.1:4242`.
- `udp=WAITING (port held by another app)` means something else already holds UDP
  `4242`, usually another of these mods left running or an OpenTrack instance set
  to receive. Close it and leave the game running; the mod retries twice a second.
- `gameplay gate CLOSED` means the mod thinks you are in a menu. The line reports
  the three signals it decided from (window focus, cursor visibility, cursor
  clipping); include it in an issue.

**Jittery or unstable tracking**

- Over WiFi or from a phone, raise `RemoteSmoothing` above its `0.15` default.
- On this machine, send to `127.0.0.1` rather than the LAN address so the mod
  applies `LocalSmoothing` instead.
- With a webcam, add even front-facing light; the `neuralnet` tracker gets noisy
  in the dark and that noise reaches the camera as jitter.

**Wrong rotation axis**

- If yaw feels wrong when looking steeply up or down, toggle between world-locked
  and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked is the
  default and keeps the horizon level.
- If the view drifts or sits off-center, center in your tracker app; the mod has
  no center of its own.
- If an axis moves the wrong way, fix it in your tracker's profile. The mod
  exposes no inversion settings on purpose.

**Game-specific quirks**

- Something stops being usable when you look away from it: the game picks what
  you can interact with using the camera it draws, so an object stops responding
  once your head is roughly 25 degrees off it. Look back toward it. Your aim
  itself is unaffected.
- The crosshair sits at screen center while the view moves: the log line
  `HUD cursor hooked at RVA ...` says the hook installed. If it reports a
  failure, open an issue with that line. The crosshair is also left alone
  whenever tracking is not being applied, which includes every menu.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The ASI loader is only removed if
the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Needs CMake and Visual Studio. The build has no dependency on a game install; it
produces the installer ZIP on a clean checkout of a machine that does not own the
game.

```powershell
git clone --recursive https://github.com/itsloopyo/kingdom-come-deliverance-headtracking
cd kingdom-come-deliverance-headtracking
pixi run package
```

Tasks: `pixi run build | test | package | install | update-deps | check-fingerprint | release`.

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. The bundled and statically
linked third-party components keep their own licenses, all reproduced in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- **Warhorse Studios** for Kingdom Come: Deliverance, and for shipping a camera
  whose view parameters and render matrix are cleanly separated. That separation
  is the whole reason look and aim can be decoupled here. The clip at the top of
  this page is their game, reproduced only to show the mod in use.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (MIT)
  for loading the mod into the game.
- [OpenTrack](https://github.com/opentrack/opentrack) (ISC) for the tracking
  protocol.
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause), statically
  linked, for the function hooks.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Warhorse Studios.
Use at your own risk.
