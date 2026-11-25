# Installing Training Mode for Project+

This takes about five minutes. You need Project+ v3.2.1 already working, and the file
`TrainingMode.rel`.

The whole mod is that one file. It goes into Project+'s `pf/plugins` folder on the SD card,
and nothing else in your Project+ build is changed. The lab saves its settings in a
`Project+/tm` folder on the same card. Deleting the file (and that folder) removes the mod.

Before you start:

- **Use an offline Project+ build, not netplay.** The mod is for offline practice only.
- **Remove ProjectPunch first** if you have it installed. The two can't run together:
  Project+ doesn't have the memory for both.
- **Make a backup** of your SD card (on Dolphin, the `sd.raw` file) if you want an easy way
  back. The mod doesn't change any other file, but a backup costs nothing.

## On Dolphin

Project+ on Dolphin keeps its SD card in one file, usually called `sd.raw`. Dolphin can
unpack that file into a normal folder and pack it back up, and that's how you add the mod.

1. **Close the game** if it's running (Dolphin itself can stay open).
2. In Dolphin, open **Options > Configuration**, then the **Wii** tab. Find the
   **SD Card Settings** box.
3. Make sure **Insert SD Card** is ticked, and **Allow Writes to SD Card** too. Without it the
   lab can't save your settings, and you'll see "Lab settings not saved: the SD card is
   locked" in the match.
4. Look at **SD Sync Folder**. That's the folder the SD card unpacks into. Unpacking
   deletes whatever is already in it, so if it isn't set, or holds anything you want to
   keep, pick an empty folder instead, for example a new folder called `sd` next to your
   `sd.raw`.
5. Click **Unpack SD Card Now** and say yes if Dolphin asks. The SD Sync Folder now holds
   everything on the SD card, including a `Project+` folder.
6. In that folder, open `Project+`, then `pf`, then `plugins`. Copy `TrainingMode.rel` in
   there. If a `TrainingMode.rel` is already there, replace it.
7. Back in the SD Card Settings, click **Pack SD Card Now** and say yes.
8. Start Project+ as usual.

If **Automatically Sync with Folder** is ticked, Dolphin works the other way round: it packs
the folder into the card when the game starts, and unpacks the card back into the folder
when the game stops (only if Allow Writes to SD Card is ticked). The folder is then the
real copy. You can skip steps 5 and 7: copy the file into the SD Sync Folder's
`Project+/pf/plugins` while the game isn't running, and start the game.

To update the mod later, repeat the same steps with the new file. To remove it, delete
`TrainingMode.rel` from `Project+/pf/plugins` the same way, and the `Project+/tm` folder if
you don't want to keep your lab settings.

## On a Wii

Earlier pre-releases have booted and saved settings on real Wii hardware, while
the newest features in this pre-release have only been tested in Dolphin so far.
Feedback from console hardware is very welcome.

1. Turn the Wii off and take the SD card out.
2. On a computer, open the SD card and go to `Project+`, then `pf`, then `plugins`.
3. Copy `TrainingMode.rel` in there.
4. Put the card back and start Project+ as usual.

## Check that it worked

1. Start Project+. It opens straight onto Training's character select with the **Training Menu**
   open (hold X or Y on a GameCube controller while it starts if you want Versus instead).
   If you see the Training Menu, the mod is installed.
2. Pick **Training Lab** (or one of the Scenario presets), choose your characters and start the match.
3. You should see **P+TM** and the version in the bottom-right corner of the screen.
4. Press **Start** (`+` on Wii controllers) to open the lab menu, and press it again to close
   it. Z in the lab menu (or **Brawl Training Menu**) takes you to Brawl's own Training menu.
   (If you'd rather keep Start for Brawl's menu, set **Controls > Menu Open** to **Z in Pause**,
   then open the lab menu from Brawl's pause menu.)
5. Project+'s in-match Code Menu shortcut (L+R+D-pad Down) is disabled by default in
   Training so it doesn't collide with Training shortcuts. Turn on
   **Controls > P+ Code Menu Shortcut** to allow it in the Training Lab. Main Menu access
   (**Versus > Codes**), regular Versus matches, and any Debug options you turn on from the
   Main Menu stay untouched.

If the Training Menu doesn't show up, check that the file is in `Project+/pf/plugins` (not in
a folder inside it) and that it's called exactly `TrainingMode.rel`. On Dolphin, make sure
you packed the SD card after copying the file (step 7), or that Automatically Sync with
Folder is ticked.

## Reporting problems

Please tell us:

- Dolphin or a Wii, and which Project+ download you use.
- What you did right before the problem, and what you expected to happen.
- On Dolphin, if the game froze or crashed: the last lines of Dolphin's log
  (**View > Show Log**) are a big help.

Known issues are listed in [limitations.md](limitations.md).
