# v2.6.0
- **In-game Status** (Tools tab): a small indicator in the top-right corner while playing showing exactly which hacks are on - **NC**, **SPD 2.00x**, **AC**, **STEP** - plus the bot state (**REC** / **PLAY** / **FFWD**) and **SAFE** when Safe Mode is protecting the attempt. Modes: `auto` (only when something is on), `always`, `off`
- **Auto-Save Bot** (Bot tab, on by default): completing a level while recording saves the bot straight away as `<level name>.gdbot` in the bots folder - no more "did I save it?"
- **`.gdbot` is the only format now** - bots are saved, listed and loaded as `.gdbot` (same GDR2 binary layout)
- **Bots tab**: each bot row now has a **Play** button (load + play in one tap), and shows the level's **stars** if it's a local level
- **Saved sessions list** (Bots tab): every resume session is listed with level, % and inputs - delete old ones anywhere, **Resume** from the level you're in
- Frame counter now shows the time too (Frame 5532 (0:23))

# v2.4.1
- **Autoclicker** (More tab): 1-60 clicks/sec, gets recorded by the bot like real clicks
- **Safe Mode** (on by default): no new best % or completion is saved after using noclip, speedhack, autoclicker, frame stepper, start pos or bot playback during an attempt
- **Noclip Accuracy**: optional small % + deaths counter while noclip is on (off by default)
- **Menu Themes** (Style tab): 7 accent colours, bubble opacity and size
- **Preset Profiles** (Style tab): 3 slots (Practice / Showcase / Custom) that save and load your hack settings

# v2.4.0
- Per-tick physics fix: playback follows the exact recorded path (practice-mode bots no longer drift)
- Bubble is now a true circle and appears on every screen (search, level info, creator, settings...) except gameplay

# v2.3.0
- Floating GDM bubble now shows on every screen except gameplay
- Bot saves each input's position/speed (GDR2 "Phys" extension) and uses it on playback, which fixes practice-mode bots dying
- Rewrote about page

# v2.2.0
- Fixed phantom jumps from left/right keys in normal levels (broke Eclipse playback)
- Exact practice-mode respawn and held-button sync

# v2.1.0
- Eclipse-compatible timing, auto-pause Click Between Frames, practice fix, copy to Eclipse

# v2.0.0
- New tabbed menu, bots library, .gdr2 / .gdbot saving
