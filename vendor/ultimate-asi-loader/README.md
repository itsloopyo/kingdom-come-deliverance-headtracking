# Ultimate ASI Loader (vendored)

Bundled copy of Ultimate ASI Loader, the install-time source of truth.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Tag: `v9.7.2`
- Commit: `ab722befd52581a34449b603926cfab476e66b05`
- Asset: `Ultimate-ASI-Loader_x64.zip`
- Upstream URL: https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.7.2/Ultimate-ASI-Loader_x64.zip
- dinput8.dll SHA-256: `22fda9c71eaae02460f311bf3441638340ab591586d78f1de213c4819dcb883c`
- Fetched at: 2026-08-23T17:38:23.4477937+01:00

`dinput8.dll` is extracted from the upstream asset untouched. It is deployed to
`<game>/Bin/Win64/dinput8.dll`, beside KingdomCome.exe. WHGame.dll - the module
that carries the whole engine and the camera code - imports DINPUT8.dll directly,
and the application directory is searched before System32, so the proxy loads with
no launch-option changes and forwards every DirectInput export on to the real DLL.
