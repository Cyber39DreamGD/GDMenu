#pragma once
// Shared state + API used by every part of GDMenu.
#include <Geode/Geode.hpp>
#include <filesystem>
#include <string>
#include <vector>

using namespace geode::prelude;

// ---------------------------------------------------------------- bot
struct BotInput {
	int frame;     // physics tick (m_gameState.m_currentProgress) - same frame base Eclipse/xdBot use for GDR2
	int button;    // 1 = jump, 2 = left, 3 = right
	bool down;
	bool player2;  // true ONLY for the second player in 2-player levels (GDR2 convention)
	// "Phys" extension (GDR2 PhysicsInput): player state right before the input.
	// Playback snaps the player to this, so tiny drift (e.g. from practice respawns) can't build up.
	bool phys = false;
	float x = 0.f, y = 0.f, rot = 0.f;
	double xVel = 0.0, yVel = 0.0;
};

// Per-tick player state recorded while recording; playback snaps to it every tick so the
// run follows the exact recorded path (kills practice-mode drift completely).
struct PlayerFix {
	float x = 0.f, y = 0.f, rot = 0.f;
	double xVel = 0.0, yVel = 0.0;
};
struct FrameFix {
	int frame = 0;
	PlayerFix p1, p2;
	bool hasP2 = false;
};

enum class BotState { Idle, Recording, Playing, Resuming };

struct BotData {
	BotState state = BotState::Idle;
	std::vector<BotInput> inputs;
	std::vector<FrameFix> fixes;
	size_t playIndex = 0;
	size_t fixIndex = 0;
	int levelID = 0;
	int resumeFrame = 0;     // frame to fast-forward to when resuming a session
	float lastPercent = 0.f;
	bool botInput = false;   // true while the bot itself is calling handleButton
	bool held[2][4] = {};    // [player][button] held according to the macro
	bool realHeld[2][4] = {};// [player][button] what the real player is physically holding
	std::string loadedName;  // replay file currently loaded (for UI)

	bool stepper = false;
	int pendingSteps = 0;
};
extern BotData g_bot;

// ---------------------------------------------------------------- hacks
struct HackData {
	bool noclip = false;
	bool speedhack = false;
	bool hitboxes = false;
	float speed = 1.f;
	// 2.4.1
	bool autoclick = false;
	float cps = 10.f;             // clicks per second
	bool safeMode = true;         // block progress/completions after a cheat was used this attempt
	bool cheatedAttempt = false;
	bool accuracy = false;        // noclip accuracy + deaths label (only shown if enabled)
	int accTicks = 0, accDeadTicks = 0, accDeaths = 0;
	bool accHitThisTick = false, accWasHit = false;
	std::vector<Ref<StartPosObject>> startPositions; // sorted by X
	int startPosIndex = -1;                           // -1 = level start
	enumKeyCodes kToggleStep = KEY_None, kStep = KEY_None, kNoclip = KEY_None, kHitbox = KEY_None,
		kSpeed = KEY_None, kSpPrev = KEY_None, kSpNext = KEY_None;
};
extern HackData g_hacks;

// ---------------------------------------------------------------- helpers
void notify(std::string const& msg, NotificationIcon icon = NotificationIcon::Info);

namespace bot {
	int frame();                    // current physics tick, 0 outside a level
	char const* stateName();
	ccColor3B stateColor();

	void startRecording();          // fresh recording from the start
	void stop();                    // stop recording / playback
	bool startPlayback();           // play the loaded inputs (restarts the level)
	void clear();

	// resume sessions (auto-saved when you quit while recording)
	bool hasSession(int levelID);
	bool sessionInfo(int levelID, float& percent, size_t& inputs);
	void saveSession();
	bool resumeSession();           // load session + fast-forward to where you left off
	void deleteSession(int levelID);

	void setTimeScale(float s);
}

namespace replays {
	struct Info {
		std::filesystem::path path;
		std::string name;
		std::string levelName;
		std::string author;
		int levelID = 0;
		size_t inputs = 0;
		float duration = 0.f;
		bool valid = false;
	};
	std::filesystem::path dir();    // save/geode/mods/cyber39dreamgd.gdmenu/replays
	std::vector<Info> list();
	bool exists(std::string const& name, std::string const& ext);
	bool save(std::string name, std::string const& ext, bool copyToEclipse); // ".gdr2" or ".gdbot" (identical layout)
	std::filesystem::path eclipseDir();  // save/geode/mods/eclipse.eclipse-menu/replays
	bool eclipseInstalled();
	bool load(std::filesystem::path const& path);
	bool remove(std::filesystem::path const& path);
}

namespace hacks {
	void reloadSettings();
	void toggleStepper();
	void setStepper(bool on);
	void stepFrames(int n);
	void toggleNoclip();
	void toggleSpeed();
	void setSpeed(float v);
	void toggleHitboxes();
	void switchStartPos(int dir);
	std::string startPosLabel();
	void updateStepperControls();   // show/hide touch step bar (only while stepper is ON)
}

namespace extras {
	bool cheatsActive();          // noclip / speedhack / autoclick / stepper / bot playback
	void saveHackState();         // persist toggles
	void loadHackState();
	// themes
	int themeIndex();
	void setTheme(int i);
	int themeCount();
	char const* themeName(int i);
	ccColor3B accent();
	float bubbleOpacity();        // 0.2 - 1
	void setBubbleOpacity(float v);
	float bubbleSize();           // multiplier
	void setBubbleSize(float v);
	// profiles (3 slots)
	std::string profileName(int slot);
	bool profileExists(int slot);
	void saveProfile(int slot);
	bool loadProfile(int slot);
}

namespace practice {
	bool applyPending(PlayLayer* pl);
}

namespace video {
	struct Info {
		bool inBottedLevel = false;
		std::string title;
		std::string description;
	};
	std::filesystem::path dir();           // save/geode/mods/cyber39dreamgd.gdmenu
	std::filesystem::path titlePath();     // .../video-title.txt
	std::filesystem::path descriptionPath();// .../video-description.txt
	void ensureDefaults();                 // create the template files if missing
	bool inBottedLevel(int levelID);       // do we have a saved replay for this level?
	std::string difficultyName(GJDifficulty d);
	std::string fillTags(std::string const& templateText, GJGameLevel* level);
	std::vector<std::string> splitLines(std::string const& text);
	Info fill();                           // current level: templates filled in (empty if not botted)
	bool copy(std::string const& text);    // copy text to the OS clipboard
}
