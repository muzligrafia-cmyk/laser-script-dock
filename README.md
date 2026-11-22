# Laser Script Dock 2.0

A panel for Illustrator (**Window › Laser Script Dock**) with one big button per script.
The buttons come from a normal folder, so adding a script is just dropping a file in
and choosing **Refresh Scripts** from the panel's ≡ menu.

```
┌──────────────────────────────┐
│ LASER WORKFLOW               │
│ ┌──────────────────────────┐ │
│ │ Cleanup                  │ │
│ │ Remove specks and dup... │ │
│ └──────────────────────────┘ │
│ ...                          │
│ PRODUCTION & FRAMING         │
│ ...                          │
│──────────────────────────────│
│ 6 scripts ready              │
└──────────────────────────────┘
```

---

## Where your scripts live

```
Documents\Laser Script Dock\        (Mac: ~/Documents/Laser Script Dock/)
    Cleanup.js
    Prep.js
    ...
    dock.json        <- button names, groups, descriptions, order
```

The first time the panel opens, this folder is filled with your six scripts and a
ready-made `dock.json`. After that the plugin never overwrites anything in it.

The **≡ menu** of the panel has: Refresh Scripts · Open Scripts Folder ·
Edit Button Names (dock.json) · About.

### Adding a script
1. ≡ menu › **Open Scripts Folder**.
2. Drop your `.jsx` / `.js` / `.jsxbin` file in.
3. ≡ menu › **Refresh Scripts**. It appears under **OTHER**, named after the file
   (underscores become spaces), and an entry for it is added to `dock.json`.

### Renaming, regrouping, describing, reordering
Open `dock.json` (≡ menu › *Edit Button Names*). Each script looks like this:

```json
{
  "file": "Cleanup.js",
  "name": "Cleanup",
  "category": "Laser Workflow",
  "description": "Remove specks and duplicate points",
  "hidden": false
},
```

- **name**: the big text on the button
- **category**: the group heading it sits under (make up any name you like)
- **description**: the small one-line text under the name (leave `""` for a slimmer button)
- **hidden**: `true` hides the button without deleting the script
- **Order**: buttons appear in the order they're listed. Groups appear in the
  order of the `"categories"` list at the top.

Save, then ≡ menu › **Refresh Scripts**.

**If you make a typo** (a missing comma or quote, for example), the status line turns red
and tells you the line number, e.g. *"dock.json has a mistake on line 21: expected a comma or }"*.
Until it's fixed, the buttons show plain file names, and your file is never overwritten.
Or just send the file to Claude.

---

## Building it (Windows)

You already have everything installed from the first version (Visual Studio, CMake,
Python, the SDK in `C:\SDK\AdobeIllustrator2027`).

1. Double-click **`build.bat`**.
2. When it asks *"Copy it into Illustrator now?"*, press **Y**, then allow the
   Windows permission prompt (Program Files is protected).
3. Start Illustrator, then open **Window › Laser Script Dock**.

The installer replaces the old version in `Plug-ins\LaserScriptDock\`. If you ever put
the old `LaserScriptDock.aip` somewhere else, delete that copy, because two copies clash.

If something fails, `build.bat` prints the important lines and saves everything in
`build-log.txt`. That's the file to send to Claude.

## Giving it to Windows colleagues

They **don't** need to build anything. Give them the `dist` folder plus
`install.bat` (same layout as here). They double-click `install.bat`, and on their
first start the panel creates their own `Documents\Laser Script Dock` folder with the
default scripts.

To change the defaults they get, edit the `defaults` folder here and rebuild.

## Mac colleagues

A Mac build has to be made on a Mac (with Xcode, CMake and the Mac Illustrator SDK):

```bash
./build_mac.sh
```

then copy `dist-mac/LaserScriptDock.aip` into
`/Applications/Adobe Illustrator 2027/Plug-ins.localized/`.

⚠️ The Mac version is written but **has never been built yet**. Expect the first build
to need a fix or two. Send `build-mac-log.txt` to Claude. On the first click, macOS may
ask to allow controlling Adobe Illustrator: say **OK**.

## New Illustrator version (2028 and later)

Plugins are tied to one Illustrator version. Download the new SDK, put it in `C:\SDK\`,
write its folder at the top of `build.bat` (`set "SDK=..."`), and double-click
`build.bat` again. Your scripts and `dock.json` aren't affected.

---

## What's inside (for whoever maintains it)

| File | Job |
|---|---|
| `src/LaserScriptDockPlugin.cpp` | Plugin startup, panel, Window menu, ≡ menu, theme. SDK calls are identical to v1, which compiled against the 2027 SDK |
| `src/DockConfig.cpp` | Reads the folder and `dock.json`, adds new scripts, writes `dock.json` safely |
| `src/MiniJson.cpp` | Small, forgiving JSON reader (allows trailing commas and `//` comments) |
| `src/PanelUIWin.cpp` / `PanelUIMac.mm` | The panel's look: buttons, groups, scrolling, status line |
| `src/ScriptExecutorWin.cpp` / `ScriptExecutorMac.mm` | Runs a script (Windows: Illustrator's COM `DoJavaScriptFile`; Mac: AppleScript `do javascript`) |
| `defaults/` | Scripts + `dock.json` copied to new users' Documents folder |
| `build.bat`, `install.bat`, `build_mac.sh` | One-click build and install |

Only one script runs at a time. Clicking another button while one is running just
shows a reminder in the status line.
