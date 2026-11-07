# QuestPhone - iPhone as a SteamVR headset + controllers (USB)

## 1. Build (GitHub, no Mac needed)
1. Create a GitHub repo, upload this whole folder (keep `.github/`), push.
2. Actions tab -> run "build". Download artifacts: `QuestPhone-ipa` and `driver_questphone-dll`.

## 2. iPhone
Install `QuestPhone.ipa` with Sideloadly (free Apple ID, plug in over USB once). Trust it in Settings > General > VPN & Device Management. Needs iTunes from apple.com installed on the PC.

## 3. PC driver
1. Put `driver_questphone.dll` in `questphone/bin/win64/` (next to `driver.vrdrivermanifest`).
2. Run in cmd: `"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" adddriver "<full path>\questphone"`
3. Edit `C:\Program Files (x86)\Steam\config\steamvr.vrsettings`, inside `"steamvr": { ... }` add:
   `"requireHmd": false, "forcedDriver": "questphone", "activateMultipleDrivers": true`

## 4. Run
1. `pip install -r bridge/requirements.txt`, then `python bridge/bridge.py`
2. Plug in iPhone, open QuestPhone, pick Quest 2 / 3 / 3S.
3. Start SteamVR (a 1920x888 window appears top-left of your main monitor - leave it uncovered). Launch a SteamVR game.
4. Hold the phone landscape, top edge to your left. "Center" recenters.
