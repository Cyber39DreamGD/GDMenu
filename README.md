# GDMenu
A Geode mod for GD 2.2081 (Geode v5) that adds a bot and a frame stepper.

## Features
- **Record / Play**: records your inputs on every physics tick and plays them back.
- **Resume where you left off**: if you quit a level while recording, the macro and your position are saved to the mod's save folder.
  When you come back and press **Record**, the bot fast-forwards through your inputs at the *Resume Speed* setting, then pauses on the frame where you stopped
  (the frame stepper turns on) and keeps recording from there.
- **Frame Stepper**: `F` turns it on or off, and `G` moves forward one tick. You can change both keys in the mod settings.
- **Noclip** (`N`): you can't die. The anticheat spike still works.
- **Speedhack** (`S`): changes the game speed. Set the value in settings (0.1x to 5x).
- **Hitbox view** (`H`): shows hitboxes outside practice mode.
- **Start-pos switcher** (`Q` / `E`): cycles through the level's start positions and the level start.
- **Save / Clear** buttons on the pause menu.
- Dying while recording removes any inputs after the respawn point, so practice checkpoints work.

## Mobile & PC
- **PC:** every feature has a keybind you can change in the mod settings. Holding the step key keeps stepping frames.
- **Mobile:** on-screen buttons appear on the right during gameplay (FS = stepper, > = step, NC = noclip, SP< / SP> = start pos).
  You can change their size and opacity, or set them to always / never / auto (auto means mobile only).
- The pause menu has bigger buttons on mobile, and green buttons show what's turned on.

## Usage
Pause the level. The bot buttons are on the right side of the screen.

## Build
```sh
geode build
```
