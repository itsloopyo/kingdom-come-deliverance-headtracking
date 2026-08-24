# Kingdom Come: Deliverance Head Tracking

![Head tracking running in Kingdom Come: Deliverance](https://raw.githubusercontent.com/itsloopyo/kingdom-come-deliverance-headtracking/main/assets/readme-clip.gif)

*Gameplay footage from Kingdom Come: Deliverance, captured with this mod running. The game, its assets and all footage of it are copyright [Warhorse Studios](https://warhorsestudios.cz/); the clip is reproduced here solely to demonstrate what the mod does. This mod is not affiliated with, endorsed by, or supported by Warhorse Studios.*

Move the in-game view with your real head while the mouse keeps aiming, with no VR headset needed.

> **Status: pre-release.** This has not been comprehensively tested and may contain
> game breaking bugs

## Features

- **Decoupled look and aim** - your head moves the picture; aim, raycasts and combat stay on your mouse
- **6DOF positional tracking** - lean and peek with head position, limited so you never clip through Henry

## Requirements

- [Kingdom Come: Deliverance on Steam](https://store.steampowered.com/app/379430/). Other stores are untested; the mod stays dormant rather than misbehave if the build is not recognized.
- A head tracker: a webcam through [OpenTrack](https://github.com/opentrack/opentrack)'s `neuralnet` tracker, TrackIR, Tobii, a VR headset, or a phone app that speaks the OpenTrack UDP protocol.
- 64-bit Windows 10 or 11.

## Installation

1. Download the installer ZIP from the [Releases page](https://github.com/itsloopyo/kingdom-come-deliverance-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`. It finds the game, drops the ASI loader and the mod into `Bin\Win64`, and records what it installed so `uninstall.cmd` can put the game back exactly as it found it.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242` (see below).
5. Launch the game.

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

Give it the folder that contains `Bin\Win64\KingdomCome.exe`, not `Bin\Win64`
itself.

### Manual Installation

Copy two files into `<game>\Bin\Win64` (the folder holding `KingdomCome.exe`):

```
Bin\Win64\dinput8.dll                                 (from vendor\ultimate-asi-loader\)
Bin\Win64\KingdomComeDeliveranceHeadTracking.asi      (from plugins\)
```

`dinput8.dll` is Ultimate ASI Loader. `WHGame.dll` imports DirectInput 8
directly and the game folder is searched before System32, so the loader picks
itself up with no launch options. If you already run another ASI loader there,
keep yours and copy only the `.asi`.

The Nexus ZIP ships the `.asi` on its own for exactly this case: extract it over
the game folder and supply your own ASI loader.

## Setting Up OpenTrack

- **Input:** your tracker. `neuralnet` for a plain webcam, or TrackIR / Tobii.
- **Output:** *UDP over network*, IP `127.0.0.1`, port `4242`.
- Press **Start**, then center with OpenTrack's Center bind while sitting how you
  normally play.

The mod keeps no center of its own, deliberately. Your tracker app already has
one, and a second center in series with it drifts apart from the first. Center in
OpenTrack, or with your phone app's CENTER button, exactly like native TrackIR
support in a flight sim.

### VR Headset Setup

Connect the headset to the PC over Air Link or Virtual Desktop, start SteamVR,
then in OpenTrack pick the **SteamVR** input and the same UDP output above. The
headset is used purely as a head tracker; the game still renders to your monitor.

### Webcam Setup

Choose the `neuralnet` tracker in OpenTrack, pick your webcam, and set the same
UDP output. It needs no markers and no IR hardware. Even, front-facing light and
a camera near the top of the monitor give the steadiest pose.

### Phone App Setup

Any phone tracker that sends the OpenTrack UDP protocol works. If the app already
smooths its own output, point it straight at this PC's IP on port `4242` and
press its **CENTER** button once you are sitting comfortably, my app [Headcam](https://headcam.app) does this for free. If you want
OpenTrack's curve mapping, have the app send to a local OpenTrack instance
instead and let OpenTrack forward to `127.0.0.1:4242`.

A tracker sending to this PC's LAN address rather than `127.0.0.1` counts as a
**remote** connection even when it is running on this same machine, because the
mod classifies a transport and not a machine, so it gets `RemoteSmoothing`.

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

`HeadTracking.ini` is written next to `KingdomCome.exe` (in `<game>\Bin\Win64`)
on first launch.

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
  and the `.asi` are both in `Bin\Win64`.
- A log line saying *staying dormant* means the mod did not recognize your
  `WHGame.dll`. The line says whether the game is newer or older than the builds
  it knows about; open an issue with it.

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

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

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
