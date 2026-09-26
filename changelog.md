# v2.5.0
- **Video tab** (between Style and Keys): a **Title** and a **Description** filled in with your botted level's info, each with a **Copy** button
- **Edit Template** opens the mod's folder, where the text comes from `video-title.txt` and `video-description.txt` (created with sensible defaults if missing)
- Template tags: `{level}`, `{creator}` (RobTop if empty), `{id}`, `{difficulty}`, `{stars}`, `{bot}`, `{fps}`
- Tab buttons resized so all 8 tabs fit

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
