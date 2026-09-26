# GDMenu
A Geode mod for GD 2.2081 (Geode v5) that adds a bot and a frame stepper.

## Features
- **Record / Play**: records your inputs on every physics tick and plays them back.
- **Resume where you left off**: if you quit a level while recording, the macro and your position are saved to the mod's save folder.
  When you come back and press **Record**, the bot fast-forwards through your inputs at the *Resume Speed* setting, then pauses on the frame where you stopped
  (the frame stepper turns on) and keeps recording from there.
- **Frame Stepper**: `F` turns it on or off, and `G` moves forward one tick. You can change both keys in the mod settings.
- **Save / Clear** buttons on the pause menu.
- Dying while recording removes any inputs after the respawn point, so practice checkpoints work.

## Usage
Pause the level. The bot buttons are on the right side of the screen.

## Build
```sh
geode build
```
