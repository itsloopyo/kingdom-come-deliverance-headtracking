# Changelog

## [Unreleased]

### Changed

- Settings move to `CameraUnlock.ini` beside `KingdomCome.exe` (`Bin\Win64\CameraUnlock.ini` on Steam and GOG, `CameraUnlock.ini` in the game folder on Xbox Game Pass). Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor is this, where your old file had it:
  - A hotkey set to Ctrl, Shift or Alt on its own. That key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its Ctrl+Shift chord.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] PositionKey` is now `CycleTrackingModeKey`, and every key, the chords included, can be rebound.
- The tracking mode that `Page Up` / `Ctrl+Shift+G` picks and the yaw mode that `Page Down` / `Ctrl+Shift+H` picks are saved to `CameraUnlock.ini` and hold the next time you start the game. `End` / `Ctrl+Shift+Y` still changes nothing in the file: the mod starts with head tracking on or off as `EnableOnStartup` says.
- `[Position] Enabled` is now `PositionEnabled` under `[Position]`, with `RotationEnabled` under `[General]`; together they are the tracking mode at startup, so it can also start position only.

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

## [0.0.0] - 2026-08-24

### Added

- Added head tracking for Kingdom Come: Deliverance as an Ultimate ASI Loader
  plugin. The head pose is applied to the camera the renderer draws with, never
  to the camera the game reads for aim, raycasts, weapon direction and player
  facing, so looking around never moves where you are aiming.
- Added OpenTrack UDP intake on port 4242 with sample-rate estimation,
  frame-rate interpolation and per-connection smoothing (`LocalSmoothing` 0.0
  for a tracker on this machine, `RemoteSmoothing` 0.15 for one over the
  network).
- Added positional lean with asymmetric limits, applied through the camera's
  original basis so leaning follows where the body faces rather than where the
  head is looking.
- Added horizon-locked (world-space) yaw by default, switchable to camera-local
  yaw.
- Added reticle compensation. The game's own combat and interaction cursor is
  moved to where the clean aim direction lands in the head-tracked view, so
  shots and interactions land under the crosshair.
- Added gameplay gating. Tracking is suppressed in the main menu, the pause
  menu, the inventory and the map, and while the game is not the foreground
  window.
- Added hotkeys on the nav cluster (`End` toggles tracking, `Page Up` cycles
  6DOF / rotation only / lean only, `Page Down` switches yaw mode) with
  `Ctrl+Shift+Y/G/H` chord alternatives.
- Added a `FieldOfView` option. It writes the same engine variable the game's
  own Vertical FOV setting writes, so it can go past that setting's 75 degree
  ceiling and everything downstream of the camera - culling, the HUD, this
  mod's crosshair compensation - follows it. `0` leaves the game's own value
  alone.
- Added a per-build profile registry that fingerprints `WHGame.dll`, leaving
  the mod fully dormant on an unrecognised build so a game patch can never
  leave a player with a crashing game. `pixi run check-fingerprint` reports
  which profile an install matches.
- Added automatic reclaim of UDP port 4242. If another app is holding the port
  when the game starts, the mod retries the bind twice a second and starts
  listening within a few tens of milliseconds of the port freeing up, so
  closing the other app is the whole fix. Startup and every heartbeat report
  which of the two states it is in (`udp=listening` / `udp=WAITING (port held
  by another app)`).
- Added `HeadTracking.ini` beside `KingdomCome.exe` on first run, plus
  `HeadTracking.log` and `HeadTracking.prev.log` for diagnostics.
