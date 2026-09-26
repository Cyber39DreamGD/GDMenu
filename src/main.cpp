// GDMenu - Bot (record / playback / resume) + Frame Stepper
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <fstream>

using namespace geode::prelude;

// ---------------------------------------------------------------- data
struct Input {
	int frame;     // physics tick (m_gameState.m_currentProgress)
	int button;    // 1 = jump, 2 = left, 3 = right
	bool down;
	bool player2;
};

enum class BotState { Idle, Recording, Playing, Resuming };

struct Bot {
	BotState state = BotState::Idle;
	std::vector<Input> inputs;
	size_t playIndex = 0;
	int levelID = 0;
	int resumeFrame = 0;     // frame we should fast-forward to
	float lastPercent = 0.f;
	bool replaying = false;  // true while bot itself calls handleButton

	bool stepper = false;
	int pendingSteps = 0;
} g_bot;

static std::filesystem::path sessionPath(int levelID) {
	return Mod::get()->getSaveDir() / fmt::format("session_{}.gdm", levelID);
}

// File format: "GDM1 <resumeFrame> <percent> <count>\n" then "frame button down p2" lines
static void saveSession(int levelID, int frame, float percent) {
	std::ofstream f(sessionPath(levelID));
	if (!f) return;
	f << "GDM1 " << frame << " " << percent << " " << g_bot.inputs.size() << "\n";
	for (auto& i : g_bot.inputs)
		f << i.frame << " " << i.button << " " << i.down << " " << i.player2 << "\n";
	log::info("Saved session for level {} at frame {} ({:.2f}%)", levelID, frame, percent);
}

static bool loadSession(int levelID) {
	std::ifstream f(sessionPath(levelID));
	if (!f) return false;
	std::string magic; size_t count = 0;
	f >> magic >> g_bot.resumeFrame >> g_bot.lastPercent >> count;
	if (magic != "GDM1") return false;
	g_bot.inputs.clear();
	for (size_t n = 0; n < count; n++) {
		Input i; int d, p;
		if (!(f >> i.frame >> i.button >> d >> p)) break;
		i.down = d; i.player2 = p;
		g_bot.inputs.push_back(i);
	}
	return true;
}

static bool hasSession(int levelID) {
	return std::filesystem::exists(sessionPath(levelID));
}

static void deleteSession(int levelID) {
	std::error_code ec;
	std::filesystem::remove(sessionPath(levelID), ec);
}

static void setSpeed(float s) {
	CCDirector::get()->getScheduler()->setTimeScale(s);
}

static void notify(std::string const& msg) {
	Notification::create(msg, NotificationIcon::Info, 1.2f)->show();
}

static int currentFrame(GJBaseGameLayer* gl) {
	return gl->m_gameState.m_currentProgress;
}

// ---------------------------------------------------------------- game hooks
class $modify(BotGameLayer, GJBaseGameLayer) {
	void handleButton(bool down, int button, bool isPlayer1) {
		if (!g_bot.replaying && PlayLayer::get() == static_cast<PlayLayer*>(static_cast<GJBaseGameLayer*>(this))) {
			// Block the real player during playback / fast-forward
			if (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming) return;
			if (g_bot.state == BotState::Recording)
				g_bot.inputs.push_back({ currentFrame(this), button, down, !isPlayer1 });
		}
		GJBaseGameLayer::handleButton(down, button, isPlayer1);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		if (PlayLayer::get() && (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming)) {
			int frame = currentFrame(this);
			g_bot.replaying = true;
			while (g_bot.playIndex < g_bot.inputs.size() && g_bot.inputs[g_bot.playIndex].frame <= frame) {
				auto& in = g_bot.inputs[g_bot.playIndex++];
				GJBaseGameLayer::handleButton(in.down, in.button, !in.player2);
			}
			g_bot.replaying = false;

			// Reached the spot where we left off -> hand control back and keep recording
			if (g_bot.state == BotState::Resuming && frame >= g_bot.resumeFrame) {
				g_bot.state = BotState::Recording;
				setSpeed(1.f);
				// release any held buttons so the player starts clean
				g_bot.replaying = true;
				for (int b = 1; b <= 3; b++) {
					GJBaseGameLayer::handleButton(false, b, true);
					GJBaseGameLayer::handleButton(false, b, false);
				}
				g_bot.replaying = false;
				g_bot.stepper = true; // pause on the exact frame so you're not caught off guard
				notify(fmt::format("Resumed at {:.1f}% - press {} to step / {} to unpause",
					g_bot.lastPercent,
					Mod::get()->getSettingValue<std::string>("step-key"),
					Mod::get()->getSettingValue<std::string>("toggle-stepper-key")));
			}
		}
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
	}

	void update(float dt) {
		// Frame stepper: freeze unless a step was requested
		if (g_bot.stepper && PlayLayer::get() && g_bot.state != BotState::Resuming) {
			if (g_bot.pendingSteps <= 0) return;
			g_bot.pendingSteps--;
			GJBaseGameLayer::update(1.f / 240.f);
			return;
		}
		GJBaseGameLayer::update(dt);
	}
};

class $modify(BotPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		g_bot.state = BotState::Idle;
		g_bot.inputs.clear();
		g_bot.playIndex = 0;
		g_bot.stepper = false;
		g_bot.pendingSteps = 0;
		g_bot.levelID = level->m_levelID.value();
		if (hasSession(g_bot.levelID))
			notify("Saved bot session found - Pause > Record to continue");
		return true;
	}

	void resetLevel() {
		PlayLayer::resetLevel();
		int frame = currentFrame(this);
		if (g_bot.state == BotState::Recording) {
			// Died / restarted: drop inputs that happen after the respawn point
			std::erase_if(g_bot.inputs, [&](Input const& i) { return i.frame >= frame; });
		}
		else if (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming) {
			g_bot.playIndex = 0;
			while (g_bot.playIndex < g_bot.inputs.size() && g_bot.inputs[g_bot.playIndex].frame < frame)
				g_bot.playIndex++;
		}
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		if (g_bot.state == BotState::Recording) {
			saveSession(g_bot.levelID, currentFrame(this), 100.f);
			notify("Level complete - macro saved");
		}
	}

	void onQuit() {
		// Save where you were so you can come back and continue
		if (g_bot.state == BotState::Recording && !g_bot.inputs.empty())
			saveSession(g_bot.levelID, currentFrame(this), this->getCurrentPercent());
		g_bot.state = BotState::Idle;
		g_bot.stepper = false;
		setSpeed(1.f);
		PlayLayer::onQuit();
	}
};

// ---------------------------------------------------------------- pause menu
class $modify(BotPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();

		customSetupBotOnly();
	}

	void refresh() {
		// Rebuild labels by re-running the pause menu
		if (auto m = this->getChildByID("bot-menu"_spr)) m->removeFromParent();
		this->customSetupBotOnly();
	}

	void customSetupBotOnly() {
		auto win = CCDirector::get()->getWinSize();
		auto menu = CCMenu::create();
		menu->setID("bot-menu"_spr);
		menu->setPosition({ 0, 0 });
		this->addChild(menu, 10);
		auto add = [&](const char* text, SEL_MenuHandler sel, float y) {
			auto spr = ButtonSprite::create(text, 90, true, "bigFont.fnt", "GJ_button_04.png", 26.f, 0.5f);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
			btn->setPosition({ win.width - 60.f, y });
			menu->addChild(btn);
		};
		float y = win.height - 40.f;
		add(g_bot.state == BotState::Recording ? "Stop Rec" : "Record", menu_selector(BotPauseLayer::onRecord), y); y -= 32;
		add(g_bot.state == BotState::Playing ? "Stop Play" : "Play",    menu_selector(BotPauseLayer::onPlay),   y); y -= 32;
		add(g_bot.stepper ? "Stepper: ON" : "Stepper: OFF",            menu_selector(BotPauseLayer::onStepper), y); y -= 32;
		add("Save",                                                     menu_selector(BotPauseLayer::onSave),   y); y -= 32;
		add("Clear",                                                    menu_selector(BotPauseLayer::onClear),  y);
	}

	void onRecord(CCObject*) {
		auto pl = PlayLayer::get();
		if (!pl) return;
		if (g_bot.state == BotState::Recording) {
			g_bot.state = BotState::Idle;
			notify("Recording stopped");
			refresh();
			return;
		}
		// Came back to a level with a saved session -> fast-forward to where you left off
		if (hasSession(g_bot.levelID) && loadSession(g_bot.levelID) && g_bot.resumeFrame > 0) {
			std::erase_if(g_bot.inputs, [](Input const& i) { return i.frame > g_bot.resumeFrame; });
			g_bot.state = BotState::Resuming;
			g_bot.playIndex = 0;
			setSpeed((float)Mod::get()->getSettingValue<double>("resume-speed"));
			notify(fmt::format("Resuming to {:.1f}%...", g_bot.lastPercent));
			this->onResume(nullptr);
			pl->resetLevel();
			return;
		}
		g_bot.inputs.clear();
		g_bot.state = BotState::Recording;
		notify("Recording");
		this->onResume(nullptr);
		pl->resetLevel();
	}

	void onPlay(CCObject*) {
		auto pl = PlayLayer::get();
		if (!pl) return;
		if (g_bot.state == BotState::Playing) {
			g_bot.state = BotState::Idle;
			refresh();
			return;
		}
		if (g_bot.inputs.empty() && !loadSession(g_bot.levelID)) {
			notify("No macro to play");
			return;
		}
		g_bot.state = BotState::Playing;
		g_bot.playIndex = 0;
		notify("Playing macro");
		this->onResume(nullptr);
		pl->resetLevel();
	}

	void onStepper(CCObject*) {
		g_bot.stepper = !g_bot.stepper;
		g_bot.pendingSteps = 0;
		refresh();
	}

	void onSave(CCObject*) {
		auto pl = PlayLayer::get();
		if (!pl || g_bot.inputs.empty()) { notify("Nothing to save"); return; }
		saveSession(g_bot.levelID, currentFrame(pl), pl->getCurrentPercent());
		notify("Session saved");
	}

	void onClear(CCObject*) {
		deleteSession(g_bot.levelID);
		g_bot.inputs.clear();
		g_bot.state = BotState::Idle;
		notify("Macro & saved session cleared");
		refresh();
	}
};

// ---------------------------------------------------------------- keybinds
static enumKeyCodes keyFromSetting(char const* id) {
	auto s = Mod::get()->getSettingValue<std::string>(id);
	if (s.empty()) return KEY_None;
	char c = (char)std::toupper((unsigned char)s[0]);
	if (c >= 'A' && c <= 'Z') return (enumKeyCodes)(KEY_A + (c - 'A'));
	if (c >= '0' && c <= '9') return (enumKeyCodes)(KEY_Zero + (c - '0'));
	return KEY_None;
}

class $modify(CCKeyboardDispatcher) {
	bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double time) {
		if (down && PlayLayer::get() && !PlayLayer::get()->m_isPaused) {
			if (key == keyFromSetting("toggle-stepper-key") && !repeat) {
				g_bot.stepper = !g_bot.stepper;
				g_bot.pendingSteps = 0;
				notify(g_bot.stepper ? "Frame stepper ON" : "Frame stepper OFF");
				return true;
			}
			if (key == keyFromSetting("step-key") && g_bot.stepper) {
				g_bot.pendingSteps++;
				return true;
			}
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, time);
	}
};
