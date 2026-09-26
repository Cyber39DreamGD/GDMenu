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
};

enum class BotState { Idle, Recording, Playing, Resuming };

struct BotData {
	BotState state = BotState::Idle;
	std::vector<BotInput> inputs;
	size_t playIndex = 0;
	int levelID = 0;
	int resumeFrame = 0;     // frame to fast-forward to when resuming a session
	float lastPercent = 0.f;
	bool botInput = false;   // true while the bot itself is calling handleButton
	bool held[2][4] = {};    // [player][button] currently held while recording
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
