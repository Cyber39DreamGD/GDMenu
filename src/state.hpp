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
		kSpeed = KEY_None, kSpPrev = KEY_None, kSpNext = KEY_None, kPanic = KEY_None;
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

	// every saved session (all levels) - for the sessions list in the Bots tab
	struct Session {
		int levelID;
		std::string levelName;      // from the local level list; "Level <id>" if not found
		float percent;
		size_t inputs;
	};
	std::vector<Session> listSessions();

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
	std::vector<Info> list();       // every .gdbot file in the folder
	bool exists(std::string const& name);
	bool save(std::string name, bool autoSave = false); // saves as "<name>.gdbot"; autoSave = quiet + auto-save wording
	GJGameLevel* findLocalLevel(int levelID); // local level for a replay/session id, or null
	bool load(std::filesystem::path const& path);
	bool remove(std::filesystem::path const& path);
}

namespace share {
	std::string encodeFile(std::filesystem::path const& file, std::string& error); // .gdbot -> base64 code
	bool decodeToFile(std::string const& code, std::filesystem::path const& out, std::string& error);
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
	void panicAll();                // #99 panic key: everything off
	void switchStartPos(int dir);
	std::string startPosLabel();
	void updateStepperControls();   // show/hide touch step bar (only while stepper is ON)
}

namespace extras {
	void flagAutoclick(bool on);  // true while the autoclicker is driving an input
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

// ---------------------------------------------------------------- gameplay layer
// The editor's test-play runs on a LevelEditorLayer (it IS a GJBaseGameLayer), not a
// PlayLayer - so PlayLayer::get() is null there. These helpers treat "the layer that
// is actually running gameplay right now" uniformly.
namespace gameplay {
	GJBaseGameLayer* active();                       // current game layer (PlayLayer, or playing editor layer), or null
	bool isMine(GJBaseGameLayer* layer);             // true if `layer` is the active gameplay layer
	bool editorPractice();                           // "Practice in Editor" toggle (from settings)
}

// ---------------------------------------------------------------- hardest level (#58)
namespace hardest {
	int levelID();                                   // 0 = no level set
	void setLevelID(int id);
	struct LevelInfo { bool known = false; std::string name; int stars = 0; };
	LevelInfo info();                                // from the local level list (needs the level downloaded/played once)
	std::filesystem::path shotsDir();                // .../cyber39dreamgd.gdmenu/newhardestpictures
	int shotCount();
	bool requestShot();                              // called on a clean win of the target level; returns true if a shot was queued
	bool consumePending();                           // scheduler hook: takes the queued shot (call once per frame)
}

// ---------------------------------------------------------------- custom click sound (#83)
namespace sounds {
	std::filesystem::path dir();                     // .../cyber39dreamgd.gdmenu/clicksounds (drop .mp3 files here)
	std::vector<std::string> listSounds();           // .mp3/.wav/.ogg file names in that folder
	std::string selected();                          // file name, "" = original sound
	void select(std::string const& name);            // preloads + saves
	void playClick();                                // plays the selected sound (no-op when off)
	bool fromAutoclick();                            // true while the autoclicker is driving an input
}

// ---------------------------------------------------------------- settings export / import (#98)
namespace settingsio {
	std::string exportAll();                         // JSON of every setting + saved value
	bool importAll(std::string const& json, std::string& error);
	std::filesystem::path exportFile();              // .../cyber39dreamgd.gdmenu/settings-export.json
}
