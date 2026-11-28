# Installing Project+ Training Mode Mod (P+TMM)

P+TMM ships either as a ready-to-run Dolphin / packed `sd.raw` package or as a single plugin file (`TrainingMode.rel`) that drops into an existing **Project+ v3.2.1** build. No other Project+ files are modified, and removing `TrainingMode.rel` uninstalls the mod cleanly.

Before installing:

- **Offline play only:** Do not install P+TMM into a netplay build.
- **ProjectPunch compatibility:** Project+ does not have enough memory to load P+TMM alongside [ProjectPunch](https://github.com/WispSSBM/ProjectPunch). Remove `ProjectPunch.rel` from `Project+/pf/plugins/` before adding `TrainingMode.rel`. P+TMM builds on Wisp's MIT-licensed ProjectPunch work (with Wisp's blessing) and includes its core training tools (frame advance, ledgedash/GALINT HUD, color overlays, invisible techs, and frame advantage).
- **Back up your SD card:** Keep a spare copy of your `sd.raw` (Dolphin) or SD card folder (Wii) before editing.

---

## Option 1: Prebuilt Dolphin / Packed `sd.raw` (Recommended for Dolphin)

1. Download the prebuilt Dolphin package (or pre-packed `sd.raw`) from [GitHub Releases](https://github.com/skierrz/pplus-training-mode/releases) or the [P+TMM Discord](https://discord.gg/WHNtJd2KAJ).
2. Extract the archive to a local folder.
3. Open Dolphin, point it at your Super Smash Bros. Brawl (USA, `RSBE01` rev 2) disc image, and launch Project+.

---

## Option 2: Add `TrainingMode.rel` to an Existing Project+ v3.2.1 Build

Download `TrainingMode.rel` from [GitHub Releases](https://github.com/skierrz/pplus-training-mode/releases).

### On Dolphin (`sd.raw`)

Project+ on Dolphin stores its virtual SD card in `sd.raw`. Use Dolphin's built-in SD Card Sync controls to add the plugin:

1. **Stop the game** if Project+ is running (Dolphin itself can stay open).
2. Open **Options -> Configuration -> Wii**, and locate **SD Card Settings**.
3. Enable both **Insert SD Card** and **Allow Writes to SD Card**. Without writes enabled, P+TMM cannot save settings or `.tmr` recordings and will display `"Lab settings not saved: the SD card is locked"` in matches.
4. Check **SD Sync Folder**. Unpacking replaces everything inside that folder, so choose an empty directory (for example, a new `sd` folder next to `sd.raw`) if it is unset or holds other files.
5. Click **Unpack SD Card Now** and confirm. Your SD Sync Folder now contains the `Project+` directory.
6. Copy `TrainingMode.rel` into `Project+/pf/plugins/` inside that folder, replacing any older copy.
7. Return to **SD Card Settings**, click **Pack SD Card Now**, and confirm.
8. Launch Project+ as usual.

> **Tip:** If **Automatically Sync with Folder** is enabled, Dolphin packs the folder into `sd.raw` on launch and unpacks it back on exit (when **Allow Writes to SD Card** is checked). In that mode you can skip steps 5 and 7 and copy `TrainingMode.rel` straight into your SD Sync Folder's `Project+/pf/plugins/` while the game is stopped.

### On a Wii

Dolphin is the primary test target; Wii compatibility is actively maintained via strict MMU, DSP LLE, and heap checks, though real Wii hardware is not yet thoroughly tested. Console feedback is welcome on Discord or GitHub Issues.

1. Power off the Wii and insert its SD card into your computer.
2. Open `Project+/pf/plugins/` on the SD card.
3. Copy `TrainingMode.rel` into that folder (replacing any older copy and removing `ProjectPunch.rel` if present).
4. Reinsert the SD card into the Wii and launch Project+ as usual.

---

## Verifying the Installation

1. Launch Project+. P+TMM boots directly to Training's character select with the 4-tab **Boot Menu** open (hold `X` or `Y` on a GameCube controller during boot to go to Versus instead).
2. Choose **Training Lab** (or any Scenario or Tech Practice drill), pick fighters and a stage, and start the match.
3. Confirm the **`P+TM`** version watermark appears cleanly in the bottom-right corner.
4. Press **Start** (`+` on Wii controllers) to open or close the in-match **Lab Menu**. Press **Z** (`ZL`/`ZR` on Classic, `-` on Sideways) inside the Lab Menu to open Brawl's Training menu, or set **Lab > Controls > Menu Open** to **Z in Pause** if you prefer keeping `Start` for Brawl's pause menu.
5. Project+'s in-match Code Menu shortcut (`L + R + D-Pad Down`) is disabled by default in Training to prevent conflicts. Re-enable it under **Lab > Controls > P+ Code Menu Shortcut**. Main Menu access (**Versus > Codes**) and regular Versus matches are unchanged.

### Troubleshooting

- **Boot Menu does not appear:** Confirm the file is named `TrainingMode.rel` directly inside `Project+/pf/plugins/` (not nested in a subfolder). On Dolphin, confirm you clicked **Pack SD Card Now** (or enabled **Automatically Sync with Folder**).
- **Settings do not save:** Confirm **Allow Writes to SD Card** is enabled in Dolphin's **Wii -> SD Card Settings** (or check the physical write-lock switch on a Wii SD card). P+TMM writes its settings, bests, and `.tmr` recordings strictly inside `/Project+/tm/`.

### Uninstalling

Delete `TrainingMode.rel` from `Project+/pf/plugins/` (and optionally `/Project+/tm/` if you do not want to keep saved settings, best streaks, or exported recordings).

---

## Getting Help and Reporting Bugs

Ask in the [P+TMM Discord](https://discord.gg/WHNtJd2KAJ) or open a [GitHub Issue](https://github.com/skierrz/pplus-training-mode/issues) with:

- Whether you are on Dolphin or a Wii, and which Project+ v3.2.1 download you are using.
- Fighter, stage, settings, and exact steps to reproduce.
- On Dolphin, the end of **View -> Show Log** if the game froze or crashed.

See [limitations.md](limitations.md) for known limitations.
