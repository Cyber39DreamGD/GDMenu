# GDMenu

A **replay bot** and toolkit for Geometry Dash, made for **both PC and mobile**.

## <cy>The Bot</c>
- **Record & Play** your inputs, frame-perfect (240 ticks per second).
- **Resume where you left off**: quit while recording and come back later. The bot fast-forwards to your exact frame, freezes there, and you keep recording.
- **Practice mode support**: GDMenu restores the player's exact physics when you respawn at a checkpoint, and saves each input's position and speed so playback can correct any drift.
- **Save as <cg>.gdbot</c>** - the GDR2 binary layout (with the standard "Phys" extension), so it also works with other GDR2 bots.
- **Your bots library**: every `.gdbot` in `geode/mods/cyber39dreamgd.gdmenu/replays` shows up in the menu. **Load**, **Play** (load + play in one tap) or delete them from there. Each row also shows the level's stars if it's one of your local levels.
- **Auto-save on complete**: finish a level while recording and the bot is saved straight away as `<level name>.gdbot` (can be turned off in the Bot tab).
- **Share codes**: any bot can be turned into one copy-pasteable **code** (Bot tab > Share Code, or the **C** button on a bot row) - and a code someone sent you becomes a bot again with **Import Code**. No files, no file manager.
- **Sessions are listed**: the Bots tab also shows every saved resume session (level, %, inputs) so you can clean up old ones; use **Resume** from the level you're in.
- Click Between Frames is paused automatically while the bot runs so replays stay in sync.

## <cy>Tools & Hacks</c>
- **Frame Stepper**: freeze the game and move one tick at a time.
- **Noclip**
- **Speedhack** (0.1x to 5x)
- **Show Hitboxes** outside practice mode
- **Start Pos Switcher**
- **Auto-Checkpoint** (practice): dies automatically place a checkpoint at your furthest % - set-and-forget checkpoints
- **Warm-Up Mode**: attempts don't count - the attempt counter stays frozen while it's on
- **Practice in the editor**: noclip, hitboxes and the stepper work in the editor's test-play too
- **Panic key** (default <cy>P</c>): one press turns every hack off at once

## <cy>Hardest Level</c>
Type the ID of the level you're conquering into the **Hardest** tab and it tells you the level's name and stars. Beat it for real (no cheats, ever) and GDMenu **takes a screenshot of the win** and saves it to `newhardestpictures` in the mod folder - your proof, in one picture.

## <cy>More & Style</c>
- **Autoclicker** with adjustable clicks per second
- **Safe Mode** so cheated attempts never save progress
- **Noclip Accuracy** counter (optional)
- **11 themes** (incl. Retro, OLED, Pastel, Contrast), a **custom accent colour** to recolor the whole mod UI, bubble opacity/size and **preset profiles**
- **Custom click sound**: make any `.mp3` / `.wav` / `.ogg` your jump click (drop it in the `clicksounds` folder and pick it in the Style tab) - only your real clicks play it
- **Settings export / import**: your entire setup as a file or a copy-paste code - share it, back it up, restore it
- **In-game Status**: a small top-right indicator while playing that tells you which hacks are active (NC / SPD / AC / STEP), the bot state (REC / PLAY / FFWD) and when Safe Mode is protecting your attempt. `auto` (default), `always` or `off`

## <cy>How to open it</c>
Tap the round **GDM bubble**. It floats on **every screen** (main menu, level lists, pause menu...) and hides while you're playing. You can drag it anywhere and it remembers where you put it.

- **PC**: every hack also has a keybind (see the *Keys* tab or the mod settings).
- **Mobile**: bigger buttons, plus +1 / +10 touch buttons that appear only while the frame stepper is on.
