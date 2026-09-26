// GDMenu - Bot (record / playback / resume) + Frame Stepper
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/CCScheduler.hpp>
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

struct Hacks {
	bool noclip = false;
	bool speedhack = false;
	bool hitboxes = false;
	float speed = 1.f;
	std::vector<Ref<StartPosObject>> startPositions; // sorted by X
	int startPosIndex = -1;                           // -1 = level start
	// cached keybinds (reading settings every keypress is wasteful)
	enumKeyCodes kToggleStep, kStep, kNoclip, kHitbox, kSpeed, kSpPrev, kSpNext;
} g_hacks;

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
				notify(fmt::format("Resumed at {:.1f}% - frame stepper ON (step / toggle it to continue)", g_bot.lastPercent));
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

// ---------------------------------------------------------------- settings cache
static enumKeyCodes keyFromSetting(char const* id) {
	auto s = Mod::get()->getSettingValue<std::string>(id);
	if (s.empty()) return KEY_None;
	char c = (char)std::toupper((unsigned char)s[0]);
	if (c >= 'A' && c <= 'Z') return (enumKeyCodes)(KEY_A + (c - 'A'));
	if (c >= '0' && c <= '9') return (enumKeyCodes)(KEY_Zero + (c - '0'));
	return KEY_None;
}

static void reloadSettings() {
	g_hacks.speed       = (float)Mod::get()->getSettingValue<double>("speedhack");
	g_hacks.kToggleStep = keyFromSetting("toggle-stepper-key");
	g_hacks.kStep       = keyFromSetting("step-key");
	g_hacks.kNoclip     = keyFromSetting("noclip-key");
	g_hacks.kHitbox     = keyFromSetting("hitbox-key");
	g_hacks.kSpeed      = keyFromSetting("speed-key");
	g_hacks.kSpPrev     = keyFromSetting("startpos-prev-key");
	g_hacks.kSpNext     = keyFromSetting("startpos-next-key");
}

static bool hudEnabled() {
	auto mode = Mod::get()->getSettingValue<std::string>("show-hud");
	if (mode == "always") return true;
	if (mode == "never") return false;
#ifdef GEODE_IS_MOBILE
	return true;
#else
	return false;
#endif
}

// ---------------------------------------------------------------- actions (shared by PC keys, HUD and pause menu)
static void toggleStepper() {
	g_bot.stepper = !g_bot.stepper;
	g_bot.pendingSteps = 0;
	notify(g_bot.stepper ? "Frame stepper ON" : "Frame stepper OFF");
}
static void stepFrame() { if (g_bot.stepper) g_bot.pendingSteps++; }
static void toggleNoclip() { g_hacks.noclip = !g_hacks.noclip; notify(g_hacks.noclip ? "Noclip ON" : "Noclip OFF"); }
static void toggleSpeed() {
	g_hacks.speedhack = !g_hacks.speedhack;
	notify(g_hacks.speedhack ? fmt::format("Speedhack {:.2f}x", g_hacks.speed) : "Speedhack OFF");
}
static void toggleHitboxes() {
	g_hacks.hitboxes = !g_hacks.hitboxes;
	if (!g_hacks.hitboxes)
		if (auto pl = PlayLayer::get(); pl && pl->m_debugDrawNode && !pl->m_isPracticeMode)
			pl->m_debugDrawNode->clear();
	notify(g_hacks.hitboxes ? "Hitboxes ON" : "Hitboxes OFF");
}

static void switchStartPos(int dir) {
	auto pl = PlayLayer::get();
	if (!pl) return;
	if (g_bot.state != BotState::Idle) { notify("Stop the bot before switching start pos"); return; }
	int count = (int)g_hacks.startPositions.size();
	if (count == 0) { notify("No start positions in this level"); return; }
	g_hacks.startPosIndex += dir;
	if (g_hacks.startPosIndex < -1) g_hacks.startPosIndex = count - 1;
	if (g_hacks.startPosIndex >= count) g_hacks.startPosIndex = -1;

	pl->m_currentCheckpoint = nullptr;
	pl->setStartPosObject(g_hacks.startPosIndex < 0 ? nullptr : g_hacks.startPositions[g_hacks.startPosIndex].data());
	if (pl->m_isPracticeMode) pl->resetLevelFromStart();
	pl->resetLevel();
	pl->startMusic();
	notify(fmt::format("Start pos {}/{}", g_hacks.startPosIndex + 1, count));
}

// ---------------------------------------------------------------- hack hooks
class $modify(HackScheduler, CCScheduler) {
	void update(float dt) {
		if (g_hacks.speedhack && g_bot.state != BotState::Resuming) dt *= g_hacks.speed;
		CCScheduler::update(dt);
	}
};

class $modify(HackGameLayer, GJBaseGameLayer) {
	void updateDebugDraw() {
		bool old = m_isDebugDrawEnabled;
		if (g_hacks.hitboxes) m_isDebugDrawEnabled = true;
		GJBaseGameLayer::updateDebugDraw();
		m_isDebugDrawEnabled = old;
	}
};

class $modify(HackPlayLayer, PlayLayer) {
	struct Fields { CCMenu* hud = nullptr; };

	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		g_hacks.startPositions.clear();
		g_hacks.startPosIndex = -1;
		reloadSettings();
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

		std::sort(g_hacks.startPositions.begin(), g_hacks.startPositions.end(),
			[](auto& a, auto& b) { return a->getPositionX() < b->getPositionX(); });
		// if the level was opened from a start pos, remember which one
		for (int i = 0; i < (int)g_hacks.startPositions.size(); i++)
			if (g_hacks.startPositions[i].data() == m_startPosObject) g_hacks.startPosIndex = i;

		if (hudEnabled()) buildHud();
		return true;
	}

	void addObject(GameObject* obj) {
		PlayLayer::addObject(obj);
		if (obj->m_objectID == 31) // start pos
			g_hacks.startPositions.push_back(static_cast<StartPosObject*>(obj));
	}

	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		if (g_hacks.noclip && obj != m_anticheatSpike) return;
		PlayLayer::destroyPlayer(player, obj);
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (g_hacks.hitboxes && !m_isPracticeMode) {
			m_debugDrawNode->setVisible(true);
			updateDebugDraw();
		}
	}

	// ---- on-screen touch buttons (mobile) ----
	void buildHud() {
		float scale = (float)Mod::get()->getSettingValue<double>("hud-scale");
		auto opacity = (GLubyte)Mod::get()->getSettingValue<int64_t>("hud-opacity");
		auto win = CCDirector::get()->getWinSize();

		auto menu = CCMenu::create();
		menu->setID("hud"_spr);
		menu->setPosition({ 0, 0 });
		menu->setTouchPriority(-500); // beat the gameplay touch handler so buttons don't make you jump

		auto add = [&](const char* text, SEL_MenuHandler sel, CCPoint pos) {
			auto spr = ButtonSprite::create(text, 40, true, "bigFont.fnt", "GJ_button_05.png", 24.f, 0.6f);
			spr->setScale(scale);
			spr->setOpacity(opacity);
			spr->setCascadeOpacityEnabled(true);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
			btn->setPosition(pos);
			menu->addChild(btn);
		};
		float gap = 30.f * scale;
		float x = win.width - 25.f * scale, y = win.height / 2 + gap * 2;
		add("FS",   menu_selector(HackPlayLayer::onHudStepper), { x, y });           y -= gap;
		add(">",    menu_selector(HackPlayLayer::onHudStep),    { x, y });           y -= gap;
		add("NC",   menu_selector(HackPlayLayer::onHudNoclip),  { x, y });           y -= gap;
		add("SP<",  menu_selector(HackPlayLayer::onHudSpPrev),  { x, y });           y -= gap;
		add("SP>",  menu_selector(HackPlayLayer::onHudSpNext),  { x, y });

		m_uiLayer->addChild(menu, 100);
		m_fields->hud = menu;
	}
	void onHudStepper(CCObject*) { toggleStepper(); }
	void onHudStep(CCObject*)    { stepFrame(); }
	void onHudNoclip(CCObject*)  { toggleNoclip(); }
	void onHudSpPrev(CCObject*)  { switchStartPos(-1); }
	void onHudSpNext(CCObject*)  { switchStartPos(1); }
};

// ---------------------------------------------------------------- pause menu
class $modify(BotPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();
		buildMenu();
	}

	void refresh() {
		if (auto m = this->getChildByID("bot-menu"_spr)) m->removeFromParent();
		buildMenu();
	}

	void buildMenu() {
		auto win = CCDirector::get()->getWinSize();
		auto menu = CCMenu::create();
		menu->setID("bot-menu"_spr);
		menu->setPosition({ 0, 0 });
		this->addChild(menu, 10);

#ifdef GEODE_IS_MOBILE
		float btnScale = 1.1f, gap = 36.f;   // bigger tap targets on phones
#else
		float btnScale = 0.9f, gap = 30.f;
#endif
		int i = 0;
		auto add = [&](std::string const& text, bool on, SEL_MenuHandler sel) {
			auto spr = ButtonSprite::create(text.c_str(), 95, true, "bigFont.fnt",
				on ? "GJ_button_01.png" : "GJ_button_04.png", 26.f, 0.5f);
			spr->setScale(btnScale);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
			// two columns on the right side
			int col = i % 2, row = i / 2;
			btn->setPosition({ win.width - 50.f - (1 - col) * 110.f * btnScale, win.height - 35.f - row * gap });
			menu->addChild(btn);
			i++;
		};

		add(g_bot.state == BotState::Recording ? "Stop Rec" : "Record", g_bot.state == BotState::Recording, menu_selector(BotPauseLayer::onRecord));
		add(g_bot.state == BotState::Playing ? "Stop Play" : "Play",    g_bot.state == BotState::Playing,   menu_selector(BotPauseLayer::onPlay));
		add("Stepper",  g_bot.stepper,     menu_selector(BotPauseLayer::onStepper));
		add("Noclip",   g_hacks.noclip,    menu_selector(BotPauseLayer::onNoclip));
		add(fmt::format("Speed {:.2g}x", g_hacks.speed), g_hacks.speedhack, menu_selector(BotPauseLayer::onSpeed));
		add("Hitboxes", g_hacks.hitboxes,  menu_selector(BotPauseLayer::onHitbox));
		add("< StartPos", false,           menu_selector(BotPauseLayer::onSpPrev));
		add("StartPos >", false,           menu_selector(BotPauseLayer::onSpNext));
		add("Save",     false,             menu_selector(BotPauseLayer::onSave));
		add("Clear",    false,             menu_selector(BotPauseLayer::onClear));
		add("Settings", false,             menu_selector(BotPauseLayer::onSettings));
	}

	void onNoclip(CCObject*)   { toggleNoclip();   refresh(); }
	void onSpeed(CCObject*)    { reloadSettings(); toggleSpeed(); refresh(); }
	void onHitbox(CCObject*)   { toggleHitboxes(); refresh(); }
	void onSpPrev(CCObject*)   { this->onResume(nullptr); switchStartPos(-1); }
	void onSpNext(CCObject*)   { this->onResume(nullptr); switchStartPos(1); }
	void onSettings(CCObject*) { geode::openSettingsPopup(Mod::get()); }

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

	void onStepper(CCObject*) { toggleStepper(); refresh(); }

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

// ---------------------------------------------------------------- PC keybinds
class $modify(CCKeyboardDispatcher) {
	bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double time) {
		auto pl = PlayLayer::get();
		if (down && key != KEY_None && pl && !pl->m_isPaused) {
			if (key == g_hacks.kStep && g_bot.stepper) { stepFrame(); return true; } // holding repeats steps
			if (!repeat) {
				if (key == g_hacks.kToggleStep) { toggleStepper();     return true; }
				if (key == g_hacks.kNoclip)     { toggleNoclip();      return true; }
				if (key == g_hacks.kHitbox)     { toggleHitboxes();    return true; }
				if (key == g_hacks.kSpeed)      { toggleSpeed();       return true; }
				if (key == g_hacks.kSpPrev)     { switchStartPos(-1);  return true; }
				if (key == g_hacks.kSpNext)     { switchStartPos(1);   return true; }
			}
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, time);
	}
};
