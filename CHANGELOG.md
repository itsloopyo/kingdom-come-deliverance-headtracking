# Changelog

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
