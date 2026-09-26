// GDMenu - Bot (record / playback / resume) + Frame Stepper
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#ifndef GEODE_IS_IOS
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#endif
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/file.hpp>
#include <gdr/gdr.hpp>
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

// ---------------------------------------------------------------- replay files (.gdr2 / .gdbot)
// .gdbot uses the exact same binary layout as .gdr2 (GDReplayFormat v2), so any bot that
// reads GDR2 (Eclipse, xdBot, etc.) can open it - just rename it or pick it in their file dialog.
struct GDMReplay : gdr::Replay<GDMReplay, gdr::Input<>> {
	GDMReplay() : Replay("GDMenu", 1) {}
};

static std::filesystem::path replaysDir() {
	auto dir = Mod::get()->getSaveDir() / "replays"; // save/geode/mods/cyber39dreamgd.gdmenu/replays
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	return dir;
}

static bool isReplayFile(std::filesystem::path const& p) {
	auto ext = p.extension().string();
	for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
	return ext == ".gdr2" || ext == ".gdbot" || ext == ".gdr";
}

static std::vector<std::filesystem::path> listReplays() {
	std::vector<std::filesystem::path> out;
	std::error_code ec;
	for (auto& e : std::filesystem::directory_iterator(replaysDir(), ec))
		if (e.is_regular_file() && isReplayFile(e.path())) out.push_back(e.path());
	std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.filename().string() < b.filename().string(); });
	return out;
}

static std::string g_loadedReplay; // file name shown in the menu

static bool exportReplay(std::string name, std::string const& ext) {
	if (g_bot.inputs.empty()) { notify("Nothing to save - record first"); return false; }
	// strip characters that are illegal in file names
	std::erase_if(name, [](char c) { return std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos; });
	if (name.empty()) name = "replay";

	GDMReplay r;
	r.author = std::string(GJAccountManager::get()->m_username);
	r.gameVersion = GEODE_COMP_GD_VERSION;
	r.framerate = 240.0;
	if (auto pl = PlayLayer::get()) {
		r.levelInfo.id = pl->m_level->m_levelID.value();
		r.levelInfo.name = std::string(pl->m_level->m_levelName);
		r.platformer = pl->m_level->isPlatformer();
	}
	uint64_t last = 0;
	for (auto& i : g_bot.inputs) {
		r.inputs.emplace_back((uint64_t)std::max(0, i.frame), (uint8_t)i.button, i.player2, i.down);
		last = std::max<uint64_t>(last, i.frame);
	}
	r.duration = last / 240.f;
	r.sortInputs();

	auto data = r.exportData();
	if (data.isErr()) { notify("Save failed: " + data.unwrapErr()); return false; }
	auto path = replaysDir() / (name + ext);
	auto& bytes = data.unwrap();
	std::ofstream f(path, std::ios::binary);
	if (!f) { notify("Couldn't write file"); return false; }
	f.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
	g_loadedReplay = path.filename().string();
	notify("Saved " + g_loadedReplay);
	return true;
}

static bool importReplay(std::filesystem::path const& path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	auto res = GDMReplay::importData(std::span<uint8_t>(bytes));
	if (res.isErr()) { notify("Invalid replay: " + res.unwrapErr()); return false; }
	auto& r = res.unwrap();

	g_bot.inputs.clear();
	for (auto& i : r.inputs)
		g_bot.inputs.push_back({ (int)i.frame, i.button == 0 ? 1 : (int)i.button, i.down, i.player2 });
	std::stable_sort(g_bot.inputs.begin(), g_bot.inputs.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
	g_bot.state = BotState::Idle;
	g_loadedReplay = path.filename().string();
	notify(fmt::format("Loaded {} ({} inputs) - press Play", g_loadedReplay, g_bot.inputs.size()));
	return true;
}

// ---------------------------------------------------------------- save dialog
class SaveReplayPopup : public Popup {
protected:
	TextInput* m_input = nullptr;
	std::string m_ext = ".gdr2";
	CCMenu* m_fmtMenu = nullptr;

	bool init() {
		if (!Popup::init(280.f, 170.f)) return false;
		this->setTitle("Save Bot");
		auto size = m_mainLayer->getContentSize();

		m_input = TextInput::create(220.f, "Replay name");
		m_input->setPosition({ size.width / 2, size.height - 62.f });
		if (auto pl = PlayLayer::get()) m_input->setString(std::string(pl->m_level->m_levelName));
		m_mainLayer->addChild(m_input);

		auto lbl = CCLabelBMFont::create("Format:", "goldFont.fnt");
		lbl->setScale(0.6f);
		lbl->setPosition({ size.width / 2, size.height - 95.f });
		m_mainLayer->addChild(lbl);

		m_fmtMenu = CCMenu::create();
		m_fmtMenu->setPosition({ 0, 0 });
		m_mainLayer->addChild(m_fmtMenu);
		buildFormatButtons();

		auto saveSpr = ButtonSprite::create("Save", "goldFont.fnt", "GJ_button_01.png", 0.9f);
		auto saveBtn = CCMenuItemSpriteExtra::create(saveSpr, this, menu_selector(SaveReplayPopup::onSave));
		saveBtn->setPosition({ size.width / 2, 25.f });
		m_buttonMenu->addChild(saveBtn);
		return true;
	}

	void buildFormatButtons() {
		m_fmtMenu->removeAllChildren();
		auto size = m_mainLayer->getContentSize();
		auto add = [&](const char* ext, float x) {
			bool on = m_ext == ext;
			auto spr = ButtonSprite::create(ext, 70, true, "bigFont.fnt", on ? "GJ_button_01.png" : "GJ_button_04.png", 26.f, 0.6f);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(SaveReplayPopup::onFormat));
			btn->setUserObject(CCString::create(ext));
			btn->setPosition({ x, size.height - 122.f });
			m_fmtMenu->addChild(btn);
		};
		add(".gdr2", size.width / 2 - 50.f);
		add(".gdbot", size.width / 2 + 50.f);
	}

	void onFormat(CCObject* sender) {
		m_ext = static_cast<CCString*>(static_cast<CCNode*>(sender)->getUserObject())->getCString();
		buildFormatButtons();
	}

	void onSave(CCObject*) {
		if (exportReplay(std::string(m_input->getString()), m_ext)) this->onClose(nullptr);
	}

public:
	static SaveReplayPopup* create() {
		auto ret = new SaveReplayPopup();
		if (ret->init()) { ret->autorelease(); return ret; }
		delete ret;
		return nullptr;
	}
};

// ---------------------------------------------------------------- main bot menu popup
class BotMenuPopup : public Popup {
protected:
	CCMenu* m_toggles = nullptr;
	CCMenu* m_list = nullptr;
	CCLabelBMFont* m_status = nullptr;
	int m_page = 0;
	static constexpr int PER_PAGE = 5;

	bool init() {
		if (!Popup::init(420.f, 270.f)) return false;
		this->setTitle("GDMenu");

		m_toggles = CCMenu::create();
		m_toggles->setPosition({ 0, 0 });
		m_mainLayer->addChild(m_toggles);

		m_list = CCMenu::create();
		m_list->setPosition({ 0, 0 });
		m_mainLayer->addChild(m_list);

		m_status = CCLabelBMFont::create("", "chatFont.fnt");
		m_status->setScale(0.7f);
		m_status->setPosition({ m_mainLayer->getContentSize().width / 2, 16.f });
		m_mainLayer->addChild(m_status);

		refresh();
		return true;
	}

	CCMenuItemSpriteExtra* makeBtn(std::string const& text, bool on, SEL_MenuHandler sel, float width = 95.f) {
		auto spr = ButtonSprite::create(text.c_str(), (int)width, true, "bigFont.fnt",
			on ? "GJ_button_01.png" : "GJ_button_04.png", 26.f, 0.5f);
#ifdef GEODE_IS_MOBILE
		spr->setScale(0.95f);
#else
		spr->setScale(0.8f);
#endif
		return CCMenuItemSpriteExtra::create(spr, this, sel);
	}

	void refresh() {
		auto size = m_mainLayer->getContentSize();

		// ---- left side: toggles (2 columns)
		m_toggles->removeAllChildren();
		int i = 0;
		auto add = [&](std::string const& text, bool on, SEL_MenuHandler sel) {
			auto btn = makeBtn(text, on, sel);
			btn->setPosition({ 62.f + (i % 2) * 88.f, size.height - 55.f - (i / 2) * 32.f });
			m_toggles->addChild(btn);
			i++;
		};
		add(g_bot.state == BotState::Recording ? "Stop Rec" : "Record", g_bot.state == BotState::Recording, menu_selector(BotMenuPopup::onRecord));
		add(g_bot.state == BotState::Playing ? "Stop Play" : "Play",    g_bot.state == BotState::Playing,   menu_selector(BotMenuPopup::onPlay));
		add("Stepper",  g_bot.stepper,    menu_selector(BotMenuPopup::onStepper));
		add("Noclip",   g_hacks.noclip,   menu_selector(BotMenuPopup::onNoclip));
		add(fmt::format("Speed {:.2g}x", g_hacks.speed), g_hacks.speedhack, menu_selector(BotMenuPopup::onSpeed));
		add("Hitboxes", g_hacks.hitboxes, menu_selector(BotMenuPopup::onHitbox));
		add("< StartPos", false,          menu_selector(BotMenuPopup::onSpPrev));
		add("StartPos >", false,          menu_selector(BotMenuPopup::onSpNext));
		add("Save Bot", false,            menu_selector(BotMenuPopup::onSave));
		add("Settings", false,            menu_selector(BotMenuPopup::onSettings));

		// ---- right side: replays in the replays folder
		m_list->removeAllChildren();
		float x = size.width - 105.f;
		auto header = CCLabelBMFont::create("Bots", "goldFont.fnt");
		header->setScale(0.6f);
		header->setPosition({ x, size.height - 45.f });
		m_list->addChild(header);

		auto files = listReplays();
		int pages = std::max(1, (int)((files.size() + PER_PAGE - 1) / PER_PAGE));
		m_page = std::clamp(m_page, 0, pages - 1);
		if (files.empty()) {
			auto none = CCLabelBMFont::create("No bots yet.\nSave one or drop\n.gdr2 / .gdbot files\nin the replays folder", "chatFont.fnt");
			none->setScale(0.6f);
			none->setAlignment(kCCTextAlignmentCenter);
			none->setPosition({ x, size.height - 110.f });
			m_list->addChild(none);
		}
		for (int n = 0; n < PER_PAGE; n++) {
			int idx = m_page * PER_PAGE + n;
			if (idx >= (int)files.size()) break;
			auto name = files[idx].filename().string();
			std::string shown = name.size() > 18 ? name.substr(0, 16) + ".." : name;
			auto btn = makeBtn(shown, name == g_loadedReplay, menu_selector(BotMenuPopup::onLoadReplay), 150.f);
			btn->setUserObject(CCString::create(files[idx].string()));
			btn->setPosition({ x, size.height - 72.f - n * 30.f });
			m_list->addChild(btn);
		}
		float by = 48.f;
		auto prev = makeBtn("<", false, menu_selector(BotMenuPopup::onPrevPage), 30.f);
		prev->setPosition({ x - 70.f, by });
		auto next = makeBtn(">", false, menu_selector(BotMenuPopup::onNextPage), 30.f);
		next->setPosition({ x + 70.f, by });
		auto folder = makeBtn("Folder", false, menu_selector(BotMenuPopup::onFolder), 70.f);
		folder->setPosition({ x, by });
		m_list->addChild(prev);
		m_list->addChild(next);
		m_list->addChild(folder);

		m_status->setString(fmt::format("Inputs: {}   Page {}/{}   {}", g_bot.inputs.size(), m_page + 1, pages,
			g_loadedReplay.empty() ? "" : "Loaded: " + g_loadedReplay).c_str());
	}

	// Actions that restart the level must close the popup AND resume from the pause menu.
	void closeAndResume() {
		auto pause = static_cast<PauseLayer*>(this->getUserObject("pause-layer"_spr));
		this->onClose(nullptr);
		if (pause) pause->onResume(nullptr);
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
		} else {
			g_bot.inputs.clear();
			g_loadedReplay.clear();
			g_bot.state = BotState::Recording;
			notify("Recording");
		}
		closeAndResume();
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
			notify("No bot loaded - pick one from the list");
			return;
		}
		g_bot.state = BotState::Playing;
		g_bot.playIndex = 0;
		notify("Playing bot");
		closeAndResume();
		pl->resetLevel();
	}

	void onLoadReplay(CCObject* sender) {
		auto path = static_cast<CCString*>(static_cast<CCNode*>(sender)->getUserObject())->getCString();
		importReplay(path);
		refresh();
	}

	void onStepper(CCObject*)  { toggleStepper();  refresh(); }
	void onNoclip(CCObject*)   { toggleNoclip();   refresh(); }
	void onSpeed(CCObject*)    { reloadSettings(); toggleSpeed(); refresh(); }
	void onHitbox(CCObject*)   { toggleHitboxes(); refresh(); }
	void onSpPrev(CCObject*)   { closeAndResume(); switchStartPos(-1); }
	void onSpNext(CCObject*)   { closeAndResume(); switchStartPos(1); }
	void onSettings(CCObject*) { geode::openSettingsPopup(Mod::get()); }
	void onFolder(CCObject*)   { geode::utils::file::openFolder(replaysDir()); }
	void onPrevPage(CCObject*) { m_page--; refresh(); }
	void onNextPage(CCObject*) { m_page++; refresh(); }
	void onSave(CCObject*) {
		if (g_bot.inputs.empty()) { notify("Nothing to save - record first"); return; }
		if (auto pl = PlayLayer::get()) // also keep the resume-session up to date
			saveSession(g_bot.levelID, currentFrame(pl), pl->getCurrentPercent());
		SaveReplayPopup::create()->show();
	}

public:
	static BotMenuPopup* create(PauseLayer* pause) {
		auto ret = new BotMenuPopup();
		ret->setUserObject("pause-layer"_spr, pause);
		if (ret->init()) { ret->autorelease(); return ret; }
		delete ret;
		return nullptr;
	}
};

// ---------------------------------------------------------------- floating (draggable) button
class FloatingButton : public CCLayer {
protected:
	CCSprite* m_sprite = nullptr;
	PauseLayer* m_pause = nullptr;
	CCPoint m_touchStart;
	CCPoint m_nodeStart;
	bool m_dragged = false;

	bool init(PauseLayer* pause) {
		if (!CCLayer::init()) return false;
		m_pause = pause;
		this->setID("floating-button"_spr);
		this->ignoreAnchorPointForPosition(false);

		m_sprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
		float scale = (float)Mod::get()->getSettingValue<double>("hud-scale");
#ifdef GEODE_IS_MOBILE
		scale *= 1.1f;
#else
		scale *= 0.8f;
#endif
		m_sprite->setScale(scale);
		auto sz = m_sprite->getScaledContentSize();
		this->setContentSize(sz);
		this->setAnchorPoint({ 0.5f, 0.5f });
		m_sprite->setPosition(sz / 2);
		this->addChild(m_sprite);

		// restore saved position (fractions of screen so it survives resolution changes)
		auto win = CCDirector::get()->getWinSize();
		float fx = Mod::get()->getSavedValue<float>("btn-x", 0.93f);
		float fy = Mod::get()->getSavedValue<float>("btn-y", 0.85f);
		this->setPosition({ fx * win.width, fy * win.height });
		clampToScreen();
		return true;
	}

	void clampToScreen() {
		auto win = CCDirector::get()->getWinSize();
		auto sz = this->getContentSize() / 2;
		this->setPosition({
			std::clamp(this->getPositionX(), sz.width, win.width - sz.width),
			std::clamp(this->getPositionY(), sz.height, win.height - sz.height)
		});
	}

	void onEnter() override {
		CCLayer::onEnter();
		CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -510, true);
	}
	void onExit() override {
		CCDirector::get()->getTouchDispatcher()->removeDelegate(this);
		CCLayer::onExit();
	}

	bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
		if (!this->isVisible()) return false;
		auto local = this->convertToNodeSpace(touch->getLocation());
		auto sz = this->getContentSize();
		if (local.x < 0 || local.y < 0 || local.x > sz.width || local.y > sz.height) return false;
		m_touchStart = touch->getLocation();
		m_nodeStart = this->getPosition();
		m_dragged = false;
		m_sprite->setColor({ 180, 180, 180 });
		return true;
	}

	void ccTouchMoved(CCTouch* touch, CCEvent*) override {
		auto delta = touch->getLocation() - m_touchStart;
		if (!m_dragged && delta.getLength() > 6.f) m_dragged = true;
		if (m_dragged) {
			this->setPosition(m_nodeStart + delta);
			clampToScreen();
		}
	}

	void ccTouchEnded(CCTouch*, CCEvent*) override {
		m_sprite->setColor({ 255, 255, 255 });
		if (m_dragged) {
			auto win = CCDirector::get()->getWinSize();
			Mod::get()->setSavedValue<float>("btn-x", this->getPositionX() / win.width);
			Mod::get()->setSavedValue<float>("btn-y", this->getPositionY() / win.height);
			return;
		}
		if (auto popup = BotMenuPopup::create(m_pause)) popup->show();
	}

	void ccTouchCancelled(CCTouch* t, CCEvent* e) override { ccTouchEnded(t, e); }

public:
	static FloatingButton* create(PauseLayer* pause) {
		auto ret = new FloatingButton();
		if (ret->init(pause)) { ret->autorelease(); return ret; }
		delete ret;
		return nullptr;
	}
};

class $modify(BotPauseLayer, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();
		if (auto btn = FloatingButton::create(this)) this->addChild(btn, 100);
	}
};

#ifndef GEODE_IS_IOS // iOS has no keyboard hook (and no keyboard)
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
#endif
