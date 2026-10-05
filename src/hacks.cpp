// Hacks: noclip, speedhack, hitboxes, start-pos switcher, frame stepper + PC keybinds.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#ifndef GEODE_IS_IOS
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#endif

HackData g_hacks;

// ---------------------------------------------------------------- active gameplay layer
// PlayLayer::get() only knows about real PlayLayers. In the editor's test-play the
// LevelEditorLayer itself runs the gameplay, so fall back to it when the toggle allows.
GJBaseGameLayer* gameplay::active() {
	if (auto pl = PlayLayer::get()) return pl;
	if (auto ed = LevelEditorLayer::get())
		if (ed->m_playbackMode == PlaybackMode::Playing) return ed;
	return nullptr;
}
bool gameplay::isMine(GJBaseGameLayer* layer) {
	if (!layer) return false;
	if (auto pl = PlayLayer::get()) return static_cast<GJBaseGameLayer*>(pl) == layer;
	if (!gameplay::editorPractice()) return false;
	auto ed = LevelEditorLayer::get();
	return ed && static_cast<GJBaseGameLayer*>(ed) == layer && layer->m_playbackMode == PlaybackMode::Playing;
}
bool gameplay::editorPractice() { return Mod::get()->getSettingValue<bool>("editor-practice"); }

// ---------------------------------------------------------------- settings
static enumKeyCodes keyFromSetting(char const* id) {
	auto s = Mod::get()->getSettingValue<std::string>(id);
	if (s.empty()) return KEY_None;
	char c = (char)std::toupper((unsigned char)s[0]);
	if (c >= 'A' && c <= 'Z') return (enumKeyCodes)(KEY_A + (c - 'A'));
	if (c >= '0' && c <= '9') return (enumKeyCodes)(KEY_Zero + (c - '0'));
	return KEY_None;
}

void hacks::reloadSettings() {
	g_hacks.speed       = (float)Mod::get()->getSettingValue<double>("speedhack");
	g_hacks.kToggleStep = keyFromSetting("toggle-stepper-key");
	g_hacks.kStep       = keyFromSetting("step-key");
	g_hacks.kNoclip     = keyFromSetting("noclip-key");
	g_hacks.kHitbox     = keyFromSetting("hitbox-key");
	g_hacks.kSpeed      = keyFromSetting("speed-key");
	g_hacks.kSpPrev     = keyFromSetting("startpos-prev-key");
	g_hacks.kSpNext     = keyFromSetting("startpos-next-key");
	g_hacks.kPanic      = keyFromSetting("panic-key");
}

// ---------------------------------------------------------------- actions
void hacks::setStepper(bool on) {
	g_bot.stepper = on;
	g_bot.pendingSteps = 0;
	hacks::updateStepperControls();
}
void hacks::toggleStepper() {
	setStepper(!g_bot.stepper);
	notify(g_bot.stepper ? "Frame stepper ON" : "Frame stepper OFF");
}
void hacks::stepFrames(int n) {
	if (g_bot.stepper) g_bot.pendingSteps += n;
}
void hacks::toggleNoclip() {
	g_hacks.noclip = !g_hacks.noclip;
	notify(g_hacks.noclip ? "Noclip ON" : "Noclip OFF");
}
void hacks::toggleSpeed() {
	g_hacks.speedhack = !g_hacks.speedhack;
	notify(g_hacks.speedhack ? fmt::format("Speedhack {:.2f}x", g_hacks.speed) : "Speedhack OFF");
}
void hacks::setSpeed(float v) {
	g_hacks.speed = std::clamp(std::round(v * 100.f) / 100.f, 0.1f, 5.f);
	Mod::get()->setSettingValue<double>("speedhack", g_hacks.speed);
}
void hacks::toggleHitboxes() {
	g_hacks.hitboxes = !g_hacks.hitboxes;
	if (!g_hacks.hitboxes)
		if (auto pl = gameplay::active(); pl && pl->m_debugDrawNode && !pl->m_isPracticeMode)
			pl->m_debugDrawNode->clear();
	notify(g_hacks.hitboxes ? "Hitboxes ON" : "Hitboxes OFF");
}

// #99 panic key: every hack off, one press
void hacks::panicAll() {
	bool any = g_hacks.noclip || g_hacks.speedhack || g_hacks.hitboxes || g_hacks.autoclick || g_bot.stepper;
	g_hacks.noclip = false;
	g_hacks.speedhack = false;
	g_hacks.hitboxes = false;
	g_hacks.autoclick = false;
	if (g_bot.stepper) hacks::setStepper(false);
	if (any) {
		extras::saveHackState();
		notify("Panic: all hacks OFF", NotificationIcon::Warning);
	}
}

// #19 auto-checkpoint: per-level "last auto-placed %" so we only place at new furthest points
static float s_lastAutoCpPct = 0.f;

std::string hacks::startPosLabel() {
	int count = (int)g_hacks.startPositions.size();
	if (count == 0) return "No start positions";
	if (g_hacks.startPosIndex < 0) return fmt::format("Level start  (0/{})", count);
	return fmt::format("Start pos {}/{}", g_hacks.startPosIndex + 1, count);
}

void hacks::switchStartPos(int dir) {
	auto pl = PlayLayer::get();
	if (!pl) return;
	if (g_bot.state != BotState::Idle) { notify("Stop the bot before switching start pos", NotificationIcon::Warning); return; }
	int count = (int)g_hacks.startPositions.size();
	if (count == 0) { notify("No start positions in this level", NotificationIcon::Warning); return; }
	g_hacks.startPosIndex += dir;
	if (g_hacks.startPosIndex < -1) g_hacks.startPosIndex = count - 1;
	if (g_hacks.startPosIndex >= count) g_hacks.startPosIndex = -1;

	pl->m_currentCheckpoint = nullptr;
	pl->setStartPosObject(g_hacks.startPosIndex < 0 ? nullptr : g_hacks.startPositions[g_hacks.startPosIndex].data());
	if (pl->m_isPracticeMode) pl->resetLevelFromStart();
	pl->resetLevel();
	pl->startMusic();
	notify(hacks::startPosLabel());
}

// ---------------------------------------------------------------- stepper touch bar
// Only appears while the frame stepper is ON (you can't step on a phone otherwise).
// Normal gameplay shows nothing - everything else lives in the pause menu.
static bool stepperTouchEnabled() {
	auto mode = Mod::get()->getSettingValue<std::string>("stepper-touch-controls");
	if (mode == "always") return true;
	if (mode == "never") return false;
#ifdef GEODE_IS_MOBILE
	return true;
#else
	return false;
#endif
}

class StepperBar : public CCMenu {
public:
	static StepperBar* create() {
		auto ret = new StepperBar();
		if (ret->init()) { ret->autorelease(); return ret; }
		delete ret;
		return nullptr;
	}
	bool init() override {
		if (!CCMenu::init()) return false;
		this->setID("stepper-bar"_spr);
		auto win = CCDirector::get()->getWinSize();
		float s = (float)Mod::get()->getSettingValue<double>("hud-scale");
		this->setPosition({ win.width / 2, 28.f * s });
		this->setTouchPriority(-500);
		auto add = [&](const char* text, const char* bg, SEL_MenuHandler sel, float x) {
			auto spr = ButtonSprite::create(text, 50, true, "bigFont.fnt", bg, 26.f, 0.6f);
			spr->setScale(0.8f * s);
			spr->setOpacity(190);
			spr->setCascadeOpacityEnabled(true);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
			btn->setPosition({ x * s, 0 });
			this->addChild(btn);
		};
		add("+1",   "GJ_button_01.png", menu_selector(StepperBar::onStep1), -55.f);
		add("+10",  "GJ_button_05.png", menu_selector(StepperBar::onStep10), 0.f);
		add("Play", "GJ_button_06.png", menu_selector(StepperBar::onOff), 55.f);
		return true;
	}
	void onStep1(CCObject*)  { hacks::stepFrames(1); }
	void onStep10(CCObject*) { hacks::stepFrames(10); }
	void onOff(CCObject*)    { hacks::setStepper(false); }
};

void hacks::updateStepperControls() {
	auto pl = PlayLayer::get();
	if (!pl || !pl->m_uiLayer) return;
	auto bar = pl->m_uiLayer->getChildByID("stepper-bar"_spr);
	bool want = g_bot.stepper && stepperTouchEnabled();
	if (want && !bar) pl->m_uiLayer->addChild(StepperBar::create(), 100);
	else if (!want && bar) bar->removeFromParent();
}

// ---------------------------------------------------------------- hooks
class $modify(HackScheduler, CCScheduler) {
	void update(float dt) {
		hardest::consumePending(); // takes the queued win screenshot (GL context is current here)
		if (g_hacks.speedhack && g_bot.state != BotState::Resuming && gameplay::active()) dt *= g_hacks.speed;
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

	void update(float dt) {
		// frame stepper: freeze unless a step was requested
		if (g_bot.stepper && g_bot.state != BotState::Resuming && gameplay::isMine(this)) {
			if (g_bot.pendingSteps <= 0) return;
			g_bot.pendingSteps--;
			GJBaseGameLayer::update(1.f / 240.f);
			return;
		}
		GJBaseGameLayer::update(dt);
	}
};

class $modify(HackPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		g_hacks.startPositions.clear();
		g_hacks.startPosIndex = -1;
		s_lastAutoCpPct = 0.f;
		hacks::reloadSettings();
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

		std::sort(g_hacks.startPositions.begin(), g_hacks.startPositions.end(),
			[](auto& a, auto& b) { return a->getPositionX() < b->getPositionX(); });
		for (int i = 0; i < (int)g_hacks.startPositions.size(); i++)
			if (g_hacks.startPositions[i].data() == m_startPosObject) g_hacks.startPosIndex = i;
		return true;
	}

	void addObject(GameObject* obj) {
		PlayLayer::addObject(obj);
		if (obj->m_objectID == 31) g_hacks.startPositions.push_back(static_cast<StartPosObject*>(obj));
	}

	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		if (g_hacks.noclip && obj != m_anticheatSpike) return;
		// #19 auto-checkpoint: practice mode, clean attempt, bot not recording -
		// place a checkpoint at a new furthest point (player is still alive here,
		// so the checkpoint gets the correct position/physics).
		if (m_isPracticeMode && !g_hacks.noclip && g_bot.state == BotState::Idle
			&& Mod::get()->getSettingValue<bool>("auto-checkpoint")) {
			float pct = m_percentage / 1000.f * 100.f;
			if (pct > s_lastAutoCpPct + 0.25f)
				if (markCheckpoint()) {
					s_lastAutoCpPct = pct;
					notify(fmt::format("Auto-checkpoint at {:.1f}%", pct));
				}
		}
		PlayLayer::destroyPlayer(player, obj);
	}

	void postUpdate(float dt) {
		PlayLayer::postUpdate(dt);
		if (g_hacks.hitboxes && !m_isPracticeMode && m_debugDrawNode) {
			m_debugDrawNode->setVisible(true);
			updateDebugDraw();
		}
	}
};

// #73 practice tools during the editor's test-play (the LevelEditorLayer runs the
// gameplay itself, so the PlayLayer hooks above never see it)
class $modify(EditorPracticeLayer, LevelEditorLayer) {
	void destroyPlayer(PlayerObject* player, GameObject* obj) {
		if (g_hacks.noclip && obj != m_anticheatSpike) return;
		LevelEditorLayer::destroyPlayer(player, obj);
	}
	void postUpdate(float dt) {
		LevelEditorLayer::postUpdate(dt);
		if (g_hacks.hitboxes && m_debugDrawNode) {
			m_debugDrawNode->setVisible(true);
			updateDebugDraw();
		}
	}
};

// ---------------------------------------------------------------- PC keybinds (hidden, no UI during gameplay)
#ifndef GEODE_IS_IOS
class $modify(CCKeyboardDispatcher) {
	bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double time) {
		auto pl = gameplay::active(); // PlayLayer or the editor's test-play layer
		if (down && key != KEY_None && pl && !pl->m_isPaused) {
			if (key == g_hacks.kStep && g_bot.stepper) { hacks::stepFrames(1); return true; } // hold = keep stepping
			if (!repeat) {
				if (key == g_hacks.kToggleStep) { hacks::toggleStepper();     return true; }
				if (key == g_hacks.kNoclip)     { hacks::toggleNoclip();      return true; }
				if (key == g_hacks.kHitbox)     { hacks::toggleHitboxes();    return true; }
				if (key == g_hacks.kSpeed)      { hacks::toggleSpeed();       return true; }
				if (key == g_hacks.kSpPrev)     { hacks::switchStartPos(-1);  return true; }
				if (key == g_hacks.kSpNext)     { hacks::switchStartPos(1);   return true; }
				if (key == g_hacks.kPanic)      { hacks::panicAll();          return true; }
			}
		}
		return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, time);
	}
};
#endif
