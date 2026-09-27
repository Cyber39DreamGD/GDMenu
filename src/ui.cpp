// UI: floating (draggable) button in the pause menu -> tabbed GDMenu panel.
// Nothing is shown while you play; everything lives behind the floating button.
#include "state.hpp"
#include <cmath>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/ui/OverlayManager.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/utils/file.hpp>

namespace {
	// ------------------------------------------------------------ small UI helpers
#define ACCENT (extras::accent())
	constexpr ccColor3B SUBTLE   = { 170, 170, 190 };

#ifdef GEODE_IS_MOBILE
	constexpr float UI_SCALE = 1.0f;  // bigger tap targets on phones
#else
	constexpr float UI_SCALE = 0.85f;
#endif

	CCNode* card(CCSize size, GLubyte opacity = 70) {
		auto bg = NineSlice::create("square02b_001.png");
		bg->setScaleMultiplier(0.5f);
		bg->setContentSize(size);
		bg->setColor({ 0, 0, 0 });
		bg->setOpacity(opacity);
		bg->setAnchorPoint({ 0.5f, 0.5f });
		return bg;
	}

	CCLabelBMFont* label(std::string const& text, char const* font, float scale, ccColor3B color = { 255, 255, 255 }) {
		auto l = CCLabelBMFont::create(text.c_str(), font);
		l->setScale(scale);
		l->setColor(color);
		return l;
	}

	// keep a label inside a width
	void fit(CCLabelBMFont* l, float maxWidth, float scale) {
		l->setScale(scale);
		if (l->getScaledContentWidth() > maxWidth) l->setScale(scale * maxWidth / l->getScaledContentWidth());
	}

	CCMenuItemSpriteExtra* button(std::string const& text, char const* bg, CCObject* target, SEL_MenuHandler sel,
		int width = 80, float scale = 0.7f) {
		auto spr = ButtonSprite::create(text.c_str(), width, true, "bigFont.fnt", bg, 26.f, 0.6f);
		spr->setScale(scale * UI_SCALE);
		return CCMenuItemSpriteExtra::create(spr, target, sel);
	}

	std::string formatTime(float seconds) {
		int s = (int)seconds;
		return fmt::format("{}:{:02}", s / 60, s % 60);
	}
}

// ---------------------------------------------------------------- save dialog
class SaveBotPopup : public Popup {
protected:
	TextInput* m_input = nullptr;
	CCMenu* m_fmtMenu = nullptr;
	std::function<void()> m_onSaved;
	static inline std::string s_ext = ".gdr2"; // remember the last choice
	static inline bool s_copyEclipse = true;
	CCMenuItemToggler* m_eclipseToggle = nullptr;

	bool init(std::function<void()> onSaved) {
		if (!Popup::init(300.f, 230.f)) return false;
		m_onSaved = std::move(onSaved);
		this->setTitle("Save Bot");
		auto size = m_mainLayer->getContentSize();

		auto nameLbl = label("Name", "goldFont.fnt", 0.55f);
		nameLbl->setPosition({ size.width / 2, size.height - 48.f });
		m_mainLayer->addChild(nameLbl);

		m_input = TextInput::create(240.f, "Replay name");
		m_input->setPosition({ size.width / 2, size.height - 72.f });
		m_input->setMaxCharCount(48);
		if (auto pl = PlayLayer::get()) m_input->setString(std::string(pl->m_level->m_levelName));
		m_mainLayer->addChild(m_input);

		auto fmtLbl = label("Format", "goldFont.fnt", 0.55f);
		fmtLbl->setPosition({ size.width / 2, size.height - 102.f });
		m_mainLayer->addChild(fmtLbl);

		m_fmtMenu = CCMenu::create();
		m_fmtMenu->setPosition({ 0, 0 });
		m_mainLayer->addChild(m_fmtMenu);
		buildFormats();

		// copy to Eclipse's own replay folder (Eclipse only lists .gdr2 files from there)
		bool hasEclipse = replays::eclipseInstalled();
		auto optMenu = CCMenu::create();
		optMenu->setPosition({ 0, 0 });
		m_mainLayer->addChild(optMenu);
		m_eclipseToggle = CCMenuItemToggler::createWithStandardSprites(this, menu_selector(SaveBotPopup::onEclipse), 0.6f);
		m_eclipseToggle->toggle(s_copyEclipse && hasEclipse);
		m_eclipseToggle->setPosition({ 40.f, 78.f });
		m_eclipseToggle->setEnabled(hasEclipse);
		optMenu->addChild(m_eclipseToggle);
		auto eLbl = label(hasEclipse ? "Also copy to Eclipse (as .gdr2)" : "Eclipse not installed", "bigFont.fnt", 0.35f,
			hasEclipse ? ccColor3B{ 255, 255, 255 } : SUBTLE);
		eLbl->setAnchorPoint({ 0, 0.5f });
		eLbl->setPosition({ 58.f, 78.f });
		m_mainLayer->addChild(eLbl);

		auto hint = label(".gdbot = same bytes as .gdr2. Other bots only list .gdr2, so use the copy there.", "chatFont.fnt", 0.5f, SUBTLE);
		fit(hint, size.width - 30.f, 0.5f);
		hint->setPosition({ size.width / 2, 52.f });
		m_mainLayer->addChild(hint);

		auto save = button("Save", "GJ_button_01.png", this, menu_selector(SaveBotPopup::onSave), 80, 0.8f);
		save->setPosition({ size.width / 2, 25.f });
		m_buttonMenu->addChild(save);
		return true;
	}

	void buildFormats() {
		m_fmtMenu->removeAllChildren();
		auto size = m_mainLayer->getContentSize();
		float x = size.width / 2 - 55.f;
		for (auto ext : { ".gdr2", ".gdbot" }) {
			bool on = s_ext == ext;
			auto btn = button(ext, on ? "GJ_button_01.png" : "GJ_button_04.png", this, menu_selector(SaveBotPopup::onFormat), 70, 0.75f);
			btn->setUserObject(CCString::create(ext));
			btn->setPosition({ x, size.height - 128.f });
			m_fmtMenu->addChild(btn);
			x += 110.f;
		}
	}

	void onEclipse(CCObject*) {
		s_copyEclipse = !m_eclipseToggle->isToggled(); // callback fires before the toggle flips
	}

	void onFormat(CCObject* sender) {
		s_ext = static_cast<CCString*>(static_cast<CCNode*>(sender)->getUserObject())->getCString();
		buildFormats();
	}

	void doSave(std::string const& name) {
		if (replays::save(name, s_ext, s_copyEclipse && replays::eclipseInstalled())) {
			if (m_onSaved) m_onSaved();
			this->onClose(nullptr);
		}
	}

	void onSave(CCObject*) {
		std::string name = m_input->getString();
		if (replays::exists(name, s_ext)) {
			Ref<SaveBotPopup> self = this;
			createQuickPopup("Overwrite?", fmt::format("<cy>{}{}</c> already exists. Replace it?", name, s_ext),
				"Cancel", "Replace", [self, name](FLAlertLayer*, bool replace) { if (replace) self->doSave(name); });
			return;
		}
		doSave(name);
	}

public:
	static SaveBotPopup* create(std::function<void()> onSaved) {
		auto ret = new SaveBotPopup();
		if (ret->init(std::move(onSaved))) { ret->autorelease(); return ret; }
		delete ret;
		return nullptr;
	}
};

// ---------------------------------------------------------------- main panel
class GDMenuPopup : public Popup {
protected:
	enum Tab { TabBot, TabBots, TabHacks, TabTools, TabMore, TabStyle, TabVideo, TabKeys, TabCount };
	static inline int s_tab = TabBot; // reopen on the last tab

	PauseLayer* m_pause = nullptr;
	CCMenu* m_tabMenu = nullptr;
	CCLabelBMFont* m_stateLabel = nullptr;
	CCNode* m_content = nullptr;      // everything inside the right-hand area
	CCSize m_area;                    // size of the content area
	CCPoint m_areaOrigin;             // bottom-left of the content area

public:
	static inline int s_openCount = 0;
protected:
	void onEnter() override { Popup::onEnter(); s_openCount++; }
	void onExit() override { s_openCount = std::max(0, s_openCount - 1); Popup::onExit(); }

	bool init(PauseLayer* pause) {
		if (!Popup::init(460.f, 290.f)) return false;
		m_pause = pause;
		this->setTitle("GDMenu", "goldFont.fnt", 0.8f, 18.f);
		auto size = m_mainLayer->getContentSize();

		auto ver = label(Mod::get()->getVersion().toVString(), "chatFont.fnt", 0.5f, SUBTLE);
		ver->setAnchorPoint({ 1, 0.5f });
		ver->setPosition({ size.width - 14.f, size.height - 14.f });
		m_mainLayer->addChild(ver);

		// sidebar
		auto side = card({ 100.f, size.height - 50.f });
		side->setPosition({ 62.f, (size.height - 36.f) / 2 });
		m_mainLayer->addChild(side);

		m_tabMenu = CCMenu::create();
		m_tabMenu->setPosition({ 0, 0 });
		m_mainLayer->addChild(m_tabMenu);

		// content area
		m_areaOrigin = CCPoint(122.f, 12.f);
		m_area = CCSize(size.width - 134.f, size.height - 48.f);
		auto areaBg = card(m_area, 50);
		areaBg->setPosition(m_areaOrigin + m_area / 2);
		m_mainLayer->addChild(areaBg);

		buildTabs();
		showTab(s_tab);
		return true;
	}

	void buildTabs() {
		m_tabMenu->removeAllChildren();
		auto size = m_mainLayer->getContentSize();
		const char* names[TabCount] = { "Bot", "Bots", "Hacks", "Tools", "More", "Style", "Video", "Keys" };
		// 8 tabs: tighter spacing (23px) so they all fit the sidebar above the state label
		float y = size.height - 58.f;
		for (int i = 0; i < TabCount; i++) {
			bool on = i == s_tab;
			auto spr = ButtonSprite::create(names[i], 70, true, "bigFont.fnt",
				on ? "GJ_button_02.png" : "GJ_button_04.png", 28.f, 0.6f);
			spr->setScale(0.62f);
			auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(GDMenuPopup::onTab));
			btn->setTag(i);
			btn->setPosition({ 62.f, y });
			m_tabMenu->addChild(btn);
			y -= 23.f;
		}
		// recording indicator under the tabs
		if (m_stateLabel) m_stateLabel->removeFromParent();
		m_stateLabel = label(bot::stateName(), "goldFont.fnt", 0.45f, bot::stateColor());
		m_stateLabel->setPosition({ 62.f, 24.f });
		m_mainLayer->addChild(m_stateLabel);
	}

	void onTab(CCObject* sender) {
		s_tab = static_cast<CCNode*>(sender)->getTag();
		buildTabs();
		showTab(s_tab);
	}

	void refresh() {
		buildTabs();
		showTab(s_tab);
	}

	void showTab(int tab) {
		if (m_content) m_content->removeFromParent();
		m_content = CCNode::create();
		m_content->setPosition(m_areaOrigin);
		m_content->setContentSize(m_area);
		m_mainLayer->addChild(m_content);
		switch (tab) {
			case TabBot:   buildBotTab(); break;
			case TabBots:  buildBotsTab(); break;
			case TabHacks: buildHacksTab(); break;
			case TabTools: buildToolsTab(); break;
			case TabKeys:  buildKeysTab(); break;
			case TabMore:  buildMoreTab(); break;
			case TabStyle: buildStyleTab(); break;
			case TabVideo: buildVideoTab(); break;
		}
	}

	CCMenu* contentMenu() {
		auto menu = CCMenu::create();
		menu->setPosition({ 0, 0 });
		m_content->addChild(menu);
		return menu;
	}

	void heading(std::string const& text, float y) {
		auto h = label(text, "goldFont.fnt", 0.6f);
		h->setAnchorPoint({ 0, 0.5f });
		h->setPosition({ 12.f, y });
		m_content->addChild(h);
	}

	// A toggle row: [title / description ......... (toggle)]
	void toggleRow(CCMenu* menu, float y, std::string const& title, std::string const& desc, bool on, SEL_MenuHandler sel) {
		float w = m_area.width - 16.f;
		auto bg = card({ w, 38.f }, 60);
		bg->setPosition({ m_area.width / 2, y });
		m_content->addChild(bg);

		auto t = label(title, "bigFont.fnt", 0.42f, on ? ccColor3B{ 140, 255, 140 } : ccColor3B{ 255, 255, 255 });
		t->setAnchorPoint({ 0, 0.5f });
		t->setPosition({ 18.f, y + 7.f });
		m_content->addChild(t);

		auto d = label(desc, "chatFont.fnt", 0.55f, SUBTLE);
		d->setAnchorPoint({ 0, 0.5f });
		fit(d, w - 70.f, 0.55f);
		d->setPosition({ 18.f, y - 8.f });
		m_content->addChild(d);

		auto toggler = CCMenuItemToggler::createWithStandardSprites(this, sel, 0.7f * UI_SCALE);
		toggler->toggle(on);
		toggler->setPosition({ m_area.width - 28.f, y });
		menu->addChild(toggler);
	}

	// ------------------------------------------------------------ Bot tab
	void buildBotTab() {
		auto menu = contentMenu();
		float W = m_area.width, H = m_area.height;

		// status card
		auto status = card({ W - 16.f, 78.f }, 80);
		status->setPosition({ W / 2, H - 47.f });
		m_content->addChild(status);

		auto st = label(bot::stateName(), "bigFont.fnt", 0.65f, bot::stateColor());
		st->setAnchorPoint({ 0, 0.5f });
		st->setPosition({ 20.f, H - 26.f });
		m_content->addChild(st);

		auto frameLbl = label(fmt::format("Frame {}", bot::frame()), "chatFont.fnt", 0.65f, SUBTLE);
		frameLbl->setAnchorPoint({ 1, 0.5f });
		frameLbl->setPosition({ W - 20.f, H - 26.f });
		m_content->addChild(frameLbl);

		auto info = label(fmt::format("Inputs: {}", g_bot.inputs.size()), "chatFont.fnt", 0.7f);
		info->setAnchorPoint({ 0, 0.5f });
		info->setPosition({ 20.f, H - 50.f });
		m_content->addChild(info);

		auto loaded = label(g_bot.loadedName.empty() ? "No file loaded" : "Loaded: " + g_bot.loadedName,
			"chatFont.fnt", 0.6f, g_bot.loadedName.empty() ? SUBTLE : ACCENT);
		loaded->setAnchorPoint({ 0, 0.5f });
		fit(loaded, W - 40.f, 0.6f);
		loaded->setPosition({ 20.f, H - 68.f });
		m_content->addChild(loaded);

		// main actions
		bool rec = g_bot.state == BotState::Recording;
		bool play = g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming;
		float y = H - 115.f;
		auto recBtn = button(rec ? "Stop" : "Record", rec ? "GJ_button_04.png" : "GJ_button_06.png", this, menu_selector(GDMenuPopup::onRecord), 80, 0.8f);
		recBtn->setPosition({ W * 0.2f, y });
		auto playBtn = button(play ? "Stop" : "Play", play ? "GJ_button_04.png" : "GJ_button_01.png", this, menu_selector(GDMenuPopup::onPlay), 80, 0.8f);
		playBtn->setPosition({ W * 0.5f, y });
		auto saveBtn = button("Save Bot", "GJ_button_05.png", this, menu_selector(GDMenuPopup::onSave), 80, 0.8f);
		saveBtn->setPosition({ W * 0.8f, y });
		menu->addChild(recBtn);
		menu->addChild(playBtn);
		menu->addChild(saveBtn);

		// resume session card
		float pct; size_t n;
		bool hasSession = bot::sessionInfo(g_bot.levelID, pct, n);
		auto sess = card({ W - 16.f, 60.f }, 60);
		sess->setPosition({ W / 2, 48.f });
		m_content->addChild(sess);

		auto sTitle = label("Resume session", "bigFont.fnt", 0.4f);
		sTitle->setAnchorPoint({ 0, 0.5f });
		sTitle->setPosition({ 20.f, 62.f });
		m_content->addChild(sTitle);

		auto sDesc = label(hasSession
			? fmt::format("You left off at {:.1f}% ({} inputs)", pct, n)
			: "Quit while recording and you can continue later", "chatFont.fnt", 0.55f, SUBTLE);
		sDesc->setAnchorPoint({ 0, 0.5f });
		fit(sDesc, W - 150.f, 0.55f);
		sDesc->setPosition({ 20.f, 40.f });
		m_content->addChild(sDesc);

		if (hasSession) {
			auto resume = button("Resume", "GJ_button_02.png", this, menu_selector(GDMenuPopup::onResumeSession), 60, 0.65f);
			resume->setPosition({ W - 95.f, 48.f });
			auto del = button("X", "GJ_button_06.png", this, menu_selector(GDMenuPopup::onDeleteSession), 20, 0.65f);
			del->setPosition({ W - 35.f, 48.f });
			menu->addChild(resume);
			menu->addChild(del);
		}
	}

	// ------------------------------------------------------------ Bots tab (replays folder)
	void buildBotsTab() {
		auto menu = contentMenu();
		float W = m_area.width, H = m_area.height;

		heading("Bots", H - 16.f);
		auto path = label("geode/mods/cyber39dreamgd.gdmenu/replays", "chatFont.fnt", 0.5f, SUBTLE);
		path->setAnchorPoint({ 1, 0.5f });
		fit(path, W - 80.f, 0.5f);
		path->setPosition({ W - 12.f, H - 16.f });
		m_content->addChild(path);

		auto files = replays::list();
		float listH = H - 72.f;
		auto scroll = ScrollLayer::create({ W - 16.f, listH });
		scroll->setPosition({ 8.f, 40.f });
		m_content->addChild(scroll);

		float rowH = 42.f;
		float total = std::max(listH, rowH * files.size() + 4.f);
		scroll->m_contentLayer->setContentSize({ W - 16.f, total });

		if (files.empty()) {
			auto none = label("No bots yet!\nRecord one and press Save Bot,\nor drop .gdr2 / .gdbot files in the folder.", "chatFont.fnt", 0.65f, SUBTLE);
			none->setAlignment(kCCTextAlignmentCenter);
			none->setPosition({ (W - 16.f) / 2, listH / 2 });
			scroll->m_contentLayer->addChild(none);
		}

		auto rowMenu = CCMenu::create();
		rowMenu->setPosition({ 0, 0 });
		scroll->m_contentLayer->addChild(rowMenu, 2);

		float y = total - rowH / 2 - 2.f;
		for (auto& f : files) {
			bool isLoaded = f.name == g_bot.loadedName;
			auto bg = card({ W - 24.f, rowH - 4.f }, isLoaded ? 110 : 60);
			if (isLoaded) static_cast<NineSlice*>(bg)->setColor({ 20, 60, 30 });
			bg->setPosition({ (W - 16.f) / 2, y });
			scroll->m_contentLayer->addChild(bg);

			auto name = label(f.name, "bigFont.fnt", 0.4f, f.valid ? ccColor3B{ 255, 255, 255 } : ccColor3B{ 255, 120, 120 });
			name->setAnchorPoint({ 0, 0.5f });
			fit(name, W - 140.f, 0.4f);
			name->setPosition({ 12.f, y + 8.f });
			scroll->m_contentLayer->addChild(name);

			std::string sub = f.valid
				? fmt::format("{} inputs  |  {}  |  {}", f.inputs, formatTime(f.duration), f.levelName.empty() ? "?" : f.levelName)
				: "Unsupported / old GDR1 file";
			auto subLbl = label(sub, "chatFont.fnt", 0.5f, SUBTLE);
			subLbl->setAnchorPoint({ 0, 0.5f });
			fit(subLbl, W - 140.f, 0.5f);
			subLbl->setPosition({ 12.f, y - 8.f });
			scroll->m_contentLayer->addChild(subLbl);

			auto load = button(isLoaded ? "Loaded" : "Load", isLoaded ? "GJ_button_04.png" : "GJ_button_01.png",
				this, menu_selector(GDMenuPopup::onLoadBot), 50, 0.6f);
			load->setUserObject(CCString::create(f.path.string()));
			load->setPosition({ W - 90.f, y });
			rowMenu->addChild(load);

			auto del = button("X", "GJ_button_06.png", this, menu_selector(GDMenuPopup::onDeleteBot), 20, 0.6f);
			del->setUserObject(CCString::create(f.path.string()));
			del->setPosition({ W - 40.f, y });
			rowMenu->addChild(del);
			y -= rowH;
		}
		scroll->scrollToTop();

		auto folder = button("Open Folder", "GJ_button_05.png", this, menu_selector(GDMenuPopup::onFolder), 90, 0.65f);
		folder->setPosition({ W / 2 - 60.f, 20.f });
		auto refresh = button("Refresh", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onRefresh), 70, 0.65f);
		refresh->setPosition({ W / 2 + 65.f, 20.f });
		menu->addChild(folder);
		menu->addChild(refresh);
	}

	// ------------------------------------------------------------ Hacks tab
	void buildHacksTab() {
		auto menu = contentMenu();
		float W = m_area.width, H = m_area.height;
		heading("Hacks", H - 16.f);
		toggleRow(menu, H - 52.f,  "Noclip", "You can't die (anticheat spike still works)", g_hacks.noclip, menu_selector(GDMenuPopup::onNoclip));
		toggleRow(menu, H - 94.f,  "Show Hitboxes", "Draw hitboxes outside practice mode", g_hacks.hitboxes, menu_selector(GDMenuPopup::onHitbox));
		toggleRow(menu, H - 136.f, "Speedhack", "Change the game speed", g_hacks.speedhack, menu_selector(GDMenuPopup::onSpeed));

		// speed controls
		float y = H - 182.f;
		auto bg = card({ W - 16.f, 44.f }, 60);
		bg->setPosition({ W / 2, y });
		m_content->addChild(bg);
		auto val = label(fmt::format("{:.2f}x", g_hacks.speed), "bigFont.fnt", 0.6f, ACCENT);
		val->setPosition({ W / 2, y });
		m_content->addChild(val);
		struct Step { const char* text; float delta; float x; };
		for (auto s : { Step{ "-0.25", -0.25f, W / 2 - 125.f }, Step{ "-0.05", -0.05f, W / 2 - 70.f },
		                Step{ "+0.05", 0.05f, W / 2 + 70.f }, Step{ "+0.25", 0.25f, W / 2 + 125.f } }) {
			auto b = button(s.text, "GJ_button_04.png", this, menu_selector(GDMenuPopup::onSpeedStep), 40, 0.55f);
			b->setUserObject(CCFloat::create(s.delta));
			b->setPosition({ s.x, y });
			menu->addChild(b);
		}
		auto reset = button("Reset to 1x", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onSpeedReset), 80, 0.55f);
		reset->setPosition({ W / 2, y - 36.f });
		menu->addChild(reset);
	}

	// ------------------------------------------------------------ Tools tab
	void buildToolsTab() {
		auto menu = contentMenu();
		float W = m_area.width, H = m_area.height;
		heading("Tools", H - 16.f);

		toggleRow(menu, H - 52.f, "Frame Stepper",
#ifdef GEODE_IS_MOBILE
			"Freeze the game - step with the +1 / +10 buttons",
#else
			"Freeze the game - step with your step key",
#endif
			g_bot.stepper, menu_selector(GDMenuPopup::onStepper));

		// start pos switcher
		float y = H - 110.f;
		auto bg = card({ W - 16.f, 60.f }, 60);
		bg->setPosition({ W / 2, y });
		m_content->addChild(bg);
		auto t = label("Start Pos Switcher", "bigFont.fnt", 0.42f);
		t->setPosition({ W / 2, y + 16.f });
		m_content->addChild(t);
		auto cur = label(hacks::startPosLabel(), "chatFont.fnt", 0.7f, ACCENT);
		cur->setPosition({ W / 2, y - 8.f });
		m_content->addChild(cur);
		auto prev = button("<", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onSpPrev), 20, 0.7f);
		prev->setPosition({ 40.f, y - 4.f });
		auto next = button(">", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onSpNext), 20, 0.7f);
		next->setPosition({ W - 40.f, y - 4.f });
		menu->addChild(prev);
		menu->addChild(next);

		// resume speed info
		auto rs = label(fmt::format("Resume fast-forward speed: {:.1f}x  (change in Keys > Settings)",
			Mod::get()->getSettingValue<double>("resume-speed")), "chatFont.fnt", 0.55f, SUBTLE);
		fit(rs, W - 20.f, 0.55f);
		rs->setPosition({ W / 2, 30.f });
		m_content->addChild(rs);
	}

	// small helper: [-big][-small]  value  [+small][+big] row inside a card
	void stepperRow(CCMenu* menu, float y, std::string const& title, std::string const& value,
		std::initializer_list<std::pair<char const*, float>> steps, SEL_MenuHandler sel) {
		float W = m_area.width;
		auto bg = card({ W - 16.f, 34.f }, 60);
		bg->setPosition({ W / 2, y });
		m_content->addChild(bg);
		auto t = label(title, "bigFont.fnt", 0.36f);
		t->setAnchorPoint({ 0, 0.5f });
		t->setPosition({ 16.f, y });
		m_content->addChild(t);
		float cx = W - 100.f;
		auto v = label(value, "bigFont.fnt", 0.45f, ACCENT);
		fit(v, 60.f, 0.45f);
		v->setPosition({ cx, y });
		m_content->addChild(v);
		int n = (int)steps.size(), i = 0;
		for (auto& [txt, d] : steps) {
			float off = (i < n / 2) ? -(n / 2 - i) * 34.f - 20.f : (i - n / 2 + 1) * 34.f + 20.f;
			auto b = button(txt, "GJ_button_04.png", this, sel, 26, 0.5f);
			b->setUserObject(CCFloat::create(d));
			b->setPosition({ cx + off, y });
			menu->addChild(b);
			i++;
		}
	}
	static float stepOf(CCObject* s) { return static_cast<CCFloat*>(static_cast<CCNode*>(s)->getUserObject())->getValue(); }

	// ------------------------------------------------------------ More tab
	void buildMoreTab() {
		auto menu = contentMenu();
		float W = m_area.width, H = m_area.height;
		heading("More", H - 16.f);
		toggleRow(menu, H - 52.f, "Autoclicker", "Auto-clicks jump (gets recorded by the bot)", g_hacks.autoclick, menu_selector(GDMenuPopup::onAutoclick));
		stepperRow(menu, H - 92.f, "Clicks / sec", fmt::format("{:.0f}", g_hacks.cps),
			{ { "-5", -5.f }, { "-1", -1.f }, { "+1", 1.f }, { "+5", 5.f } }, menu_selector(GDMenuPopup::onCps));
		toggleRow(menu, H - 134.f, "Safe Mode", "No % / completions saved after using noclip, speed, bot...", g_hacks.safeMode, menu_selector(GDMenuPopup::onSafe));
		toggleRow(menu, H - 176.f, "Noclip Accuracy", "Small % + deaths counter while noclip is on", g_hacks.accuracy, menu_selector(GDMenuPopup::onAccuracy));
		auto note = label(g_hacks.cheatedAttempt && PlayLayer::get() ? "This attempt is marked as cheated" : "Safe Mode only kicks in while a cheat is used",
			"chatFont.fnt", 0.55f, SUBTLE);
		fit(note, W - 20.f, 0.55f);
		note->setPosition({ W / 2, 22.f });
		m_content->addChild(note);
	}

	// ------------------------------------------------------------ Style tab (themes + profiles)
	void buildStyleTab() {
		auto menu = contentMenu();
		float W = m_area.width, H = m_area.height;
		heading("Theme", H - 16.f);

		float y = H - 46.f;
		auto bg = card({ W - 16.f, 34.f }, 60);
		bg->setPosition({ W / 2, y });
		m_content->addChild(bg);
		auto name = label(extras::themeName(extras::themeIndex()), "bigFont.fnt", 0.5f, ACCENT);
		name->setPosition({ W / 2, y });
		m_content->addChild(name);
		auto prev = button("<", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onTheme), 20, 0.55f);
		prev->setTag(-1); prev->setPosition({ 40.f, y });
		auto next = button(">", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onTheme), 20, 0.55f);
		next->setTag(1); next->setPosition({ W - 40.f, y });
		menu->addChild(prev); menu->addChild(next);

		stepperRow(menu, H - 82.f, "Bubble Opacity", fmt::format("{:.0f}%", extras::bubbleOpacity() * 100.f),
			{ { "-", -0.1f }, { "+", 0.1f } }, menu_selector(GDMenuPopup::onOpacity));
		stepperRow(menu, H - 118.f, "Bubble Size", fmt::format("{:.1f}x", extras::bubbleSize()),
			{ { "-", -0.1f }, { "+", 0.1f } }, menu_selector(GDMenuPopup::onSize));

		heading("Profiles", H - 150.f);
		for (int i = 0; i < 3; i++) {
			float x = W * (1 + 2 * i) / 6.f;
			auto pc = card({ W / 3 - 10.f, 76.f }, 60);
			pc->setPosition({ x, H - 200.f });
			m_content->addChild(pc);
			auto n = label(extras::profileName(i), "bigFont.fnt", 0.36f, extras::profileExists(i) ? ACCENT : SUBTLE);
			n->setPosition({ x, H - 172.f });
			m_content->addChild(n);
		auto load = button("Load", "GJ_button_01.png", this, menu_selector(GDMenuPopup::onProfileLoad), 50, 0.5f);
		load->setTag(i); load->setPosition({ x, H - 194.f });
		auto save = button("Save", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onProfileSave), 50, 0.5f);
		save->setTag(i); save->setPosition({ x, H - 220.f });
		menu->addChild(load); menu->addChild(save);
	}
	}

	// ------------------------------------------------------------ Video tab
	void buildVideoTab() {
		auto menu = contentMenu();
		float W = m_area.width, H = m_area.height;

		heading("Video", H - 16.f);

		float listW = W - 16.f;
		auto scroll = ScrollLayer::create({ listW, H - 66.f });
		scroll->setPosition({ 8.f, 36.f });
		m_content->addChild(scroll);

		auto copyMenu = CCMenu::create();
		copyMenu->setPosition({ 0, 0 });

		// [Difficulty ......... value  <  >] - what {difficulty} fills in (saved pick)
		const float diffH = 34.f;
		auto addDifficultyRow = [&](float y) {
			auto bg = card({ listW, diffH }, 60);
			bg->setPosition({ listW / 2, y });
			scroll->m_contentLayer->addChild(bg);
			auto t = label("Difficulty", "bigFont.fnt", 0.36f);
			t->setAnchorPoint({ 0, 0.5f });
			t->setPosition({ 16.f, y });
			scroll->m_contentLayer->addChild(t);
			auto v = label(video::difficultyLabel(video::difficultyIndex()), "bigFont.fnt", 0.45f, ACCENT);
			fit(v, 115.f, 0.45f);
			v->setPosition({ listW - 92.f, y });
			scroll->m_contentLayer->addChild(v);
			auto prev = button("<", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onVideoDifficulty), 20, 0.5f);
			prev->setTag(-1);
			prev->setPosition({ listW - 54.f, y });
			auto next = button(">", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onVideoDifficulty), 20, 0.5f);
			next->setTag(1);
			next->setPosition({ listW - 26.f, y });
			copyMenu->addChild(prev);
			copyMenu->addChild(next);
		};

		auto info = video::fill();
		if (!info.inBottedLevel) {
			float h = 64.f;
			float total = diffH + 8.f + h + 4.f;
			scroll->m_contentLayer->setContentSize({ listW, total });
			addDifficultyRow(total - diffH / 2);
			float my = total - diffH - 8.f - h / 2;
			auto bg = card({ listW, h }, 60);
			bg->setPosition({ listW / 2, my });
			scroll->m_contentLayer->addChild(bg);
			auto msg = label("Open a level you've botted and pause here\n- the title & description fill in automatically.",
				"chatFont.fnt", 0.6f, SUBTLE);
			msg->setAlignment(kCCTextAlignmentCenter);
			msg->setPosition({ listW / 2, my });
			scroll->m_contentLayer->addChild(msg);
		}
		else {
			auto lines = video::splitLines(info.description);
			float titleH = 54.f;
			float descH = 30.f + (float)(lines.size() - 1) * 13.f + 14.f;
			float total = diffH + 8.f + titleH + 8.f + descH + 4.f;
			scroll->m_contentLayer->setContentSize({ listW, total });
			addDifficultyRow(total - diffH / 2);

			// title box
			float ty = total - diffH - 8.f - titleH / 2;
			auto tBg = card({ listW, titleH }, 70);
			tBg->setPosition({ listW / 2, ty });
			scroll->m_contentLayer->addChild(tBg);
			auto tLbl = label("Title", "goldFont.fnt", 0.4f);
			tLbl->setAnchorPoint({ 0, 0.5f });
			tLbl->setPosition({ 14.f, ty + titleH / 2 - 13.f });
			scroll->m_contentLayer->addChild(tLbl);
			auto tText = label(info.title, "bigFont.fnt", 0.4f);
			tText->setAnchorPoint({ 0, 0.5f });
			fit(tText, listW - 90.f, 0.4f);
			tText->setPosition({ 14.f, ty - 6.f });
			scroll->m_contentLayer->addChild(tText);
			auto tCopy = button("Copy", "GJ_button_01.png", this, menu_selector(GDMenuPopup::onCopyTitle), 50, 0.55f);
			tCopy->setPosition({ listW - 38.f, ty });
			copyMenu->addChild(tCopy);

			// description box (one label per template line, blank lines kept as spacing)
			float dy = ty - titleH / 2 - 8.f - descH / 2;
			auto dBg = card({ listW, descH }, 60);
			dBg->setPosition({ listW / 2, dy });
			scroll->m_contentLayer->addChild(dBg);
			auto dLbl = label("Description", "goldFont.fnt", 0.4f);
			dLbl->setAnchorPoint({ 0, 0.5f });
			dLbl->setPosition({ 14.f, dy + descH / 2 - 13.f });
			scroll->m_contentLayer->addChild(dLbl);
			for (size_t i = 0; i < lines.size(); i++) {
				if (lines[i].empty()) continue;
				auto l = label(lines[i], "chatFont.fnt", 0.5f);
				l->setAnchorPoint({ 0, 0.5f });
				fit(l, listW - 90.f, 0.5f);
				l->setPosition({ 14.f, dy + descH / 2 - 30.f - (float)i * 13.f });
				scroll->m_contentLayer->addChild(l);
			}
			auto dCopy = button("Copy", "GJ_button_01.png", this, menu_selector(GDMenuPopup::onCopyDescription), 50, 0.55f);
			dCopy->setPosition({ listW - 38.f, dy });
			copyMenu->addChild(dCopy);
		}
		scroll->m_contentLayer->addChild(copyMenu, 2);
		scroll->scrollToTop();

		auto edit = button("Edit Template", "GJ_button_05.png", this, menu_selector(GDMenuPopup::onEditTemplate), 110, 0.65f);
		edit->setPosition({ W / 2, 18.f });
		menu->addChild(edit);
	}

	void copyNotify(bool ok, char const* what) {
		notify(ok ? std::string(what) + " copied to clipboard" : "Couldn't copy to clipboard",
			ok ? NotificationIcon::Success : NotificationIcon::Error);
	}

	void onCopyTitle(CCObject*) {
		auto info = video::fill();
		if (!info.inBottedLevel) { notify("Open a level you've botted first", NotificationIcon::Warning); return; }
		copyNotify(video::copy(info.title), "Title");
	}

	void onCopyDescription(CCObject*) {
		auto info = video::fill();
		if (!info.inBottedLevel) { notify("Open a level you've botted first", NotificationIcon::Warning); return; }
		copyNotify(video::copy(info.description), "Description");
	}

	void onEditTemplate(CCObject*) {
		video::ensureDefaults();
		geode::utils::file::openFolder(video::dir());
		notify("Template folder opened - edit video-title.txt / video-description.txt");
	}

	void onVideoDifficulty(CCObject* s) {
		video::setDifficultyIndex(video::difficultyIndex() + static_cast<CCNode*>(s)->getTag());
		refresh();
	}

	void onAutoclick(CCObject*) { g_hacks.autoclick = !g_hacks.autoclick; refresh(); }
	void onCps(CCObject* s)     { g_hacks.cps = std::clamp(g_hacks.cps + stepOf(s), 1.f, 60.f); extras::saveHackState(); refresh(); }
	void onSafe(CCObject*)      { g_hacks.safeMode = !g_hacks.safeMode; extras::saveHackState(); refresh(); }
	void onAccuracy(CCObject*)  { g_hacks.accuracy = !g_hacks.accuracy; extras::saveHackState(); refresh(); }
	void onTheme(CCObject* s)   { extras::setTheme(extras::themeIndex() + static_cast<CCNode*>(s)->getTag()); refresh(); }
	void onOpacity(CCObject* s) { extras::setBubbleOpacity(extras::bubbleOpacity() + stepOf(s)); refresh(); }
	void onSize(CCObject* s)    { extras::setBubbleSize(extras::bubbleSize() + stepOf(s)); refresh(); }
	void onProfileSave(CCObject* s) {
		int slot = static_cast<CCNode*>(s)->getTag();
		if (!extras::profileExists(slot)) { extras::saveProfile(slot); refresh(); return; }
		Ref<GDMenuPopup> self = this;
		createQuickPopup("Overwrite?", fmt::format("Replace profile <cy>{}</c> with your current settings?", extras::profileName(slot)),
			"Cancel", "Save", [self, slot](FLAlertLayer*, bool ok) { if (ok) { extras::saveProfile(slot); self->refresh(); } });
	}
	void onProfileLoad(CCObject* s) { if (extras::loadProfile(static_cast<CCNode*>(s)->getTag())) refresh(); }

	// ------------------------------------------------------------ Keys tab
	void buildKeysTab() {
		auto menu = contentMenu();
		float W = m_area.width, H = m_area.height;
		heading("Keybinds (PC)", H - 16.f);

		struct Row { const char* action; const char* key; };
		Row rows[] = {
			{ "Toggle frame stepper", "toggle-stepper-key" }, { "Step one frame", "step-key" },
			{ "Noclip", "noclip-key" }, { "Hitboxes", "hitbox-key" }, { "Speedhack", "speed-key" },
			{ "Previous start pos", "startpos-prev-key" }, { "Next start pos", "startpos-next-key" },
		};
		float y = H - 42.f;
		for (auto& r : rows) {
			auto a = label(r.action, "chatFont.fnt", 0.65f);
			a->setAnchorPoint({ 0, 0.5f });
			a->setPosition({ 20.f, y });
			m_content->addChild(a);
			auto k = label(Mod::get()->getSettingValue<std::string>(r.key), "bigFont.fnt", 0.4f, ACCENT);
			k->setAnchorPoint({ 1, 0.5f });
			k->setPosition({ W - 20.f, y });
			m_content->addChild(k);
			y -= 20.f;
		}
		auto settings = button("Settings", "GJ_button_05.png", this, menu_selector(GDMenuPopup::onSettings), 70, 0.65f);
		settings->setPosition({ W / 2 - 65.f, 22.f });
		auto resetBtn = button("Reset Button Pos", "GJ_button_04.png", this, menu_selector(GDMenuPopup::onResetButton), 100, 0.6f);
		resetBtn->setPosition({ W / 2 + 60.f, 22.f });
		menu->addChild(settings);
		menu->addChild(resetBtn);
	}

	// ------------------------------------------------------------ actions
	// Anything that restarts the level closes the panel and unpauses first.
	void closeAndResume() {
		auto pause = m_pause;
		this->onClose(nullptr);
		if (pause) pause->onResume(nullptr);
	}

	// Record / Play / Resume / Start pos need to be inside a level
	bool needLevel() {
		if (PlayLayer::get() && m_pause) return true;
		notify("Open a level first, then use GDMenu from the pause menu", NotificationIcon::Warning);
		return false;
	}

	void onRecord(CCObject*) {
		if (g_bot.state == BotState::Recording) { bot::stop(); refresh(); return; }
		if (!needLevel()) return;
		auto start = [this] { closeAndResume(); bot::startRecording(); };
		if (!g_bot.inputs.empty() && g_bot.loadedName.empty() && g_bot.state == BotState::Idle) {
			Ref<GDMenuPopup> self = this;
			createQuickPopup("New recording?", "Your current <cr>unsaved</c> bot will be replaced.", "Cancel", "Record",
				[self](FLAlertLayer*, bool ok) { if (ok) { self->closeAndResume(); bot::startRecording(); } });
			return;
		}
		start();
	}

	void onPlay(CCObject*) {
		if (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming) { bot::stop(); refresh(); return; }
		if (g_bot.inputs.empty()) { notify("No bot loaded - pick one in the Bots tab", NotificationIcon::Warning); s_tab = TabBots; refresh(); return; }
		if (!needLevel()) return;
		closeAndResume();
		bot::startPlayback();
	}

	void onSave(CCObject*) {
		if (g_bot.inputs.empty()) { notify("Nothing to save - record first", NotificationIcon::Warning); return; }
		if (!PlayLayer::get()) { notify("Open the level to save its bot", NotificationIcon::Warning); return; }
		Ref<GDMenuPopup> self = this;
		SaveBotPopup::create([self] { self->refresh(); })->show();
	}

	void onResumeSession(CCObject*) {
		if (!needLevel()) return;
		closeAndResume();
		bot::resumeSession();
	}

	void onDeleteSession(CCObject*) {
		Ref<GDMenuPopup> self = this;
		createQuickPopup("Delete session?", "You won't be able to resume where you left off.", "Cancel", "Delete",
			[self](FLAlertLayer*, bool ok) { if (ok) { bot::deleteSession(g_bot.levelID); self->refresh(); } });
	}

	void onLoadBot(CCObject* sender) {
		std::string path = static_cast<CCString*>(static_cast<CCNode*>(sender)->getUserObject())->getCString();
		replays::load(path);
		refresh();
	}

	void onDeleteBot(CCObject* sender) {
		std::string path = static_cast<CCString*>(static_cast<CCNode*>(sender)->getUserObject())->getCString();
		Ref<GDMenuPopup> self = this;
		createQuickPopup("Delete bot?", fmt::format("Delete <cy>{}</c>? This can't be undone.",
			std::filesystem::path(path).filename().string()), "Cancel", "Delete",
			[self, path](FLAlertLayer*, bool ok) { if (ok) { replays::remove(path); self->refresh(); } });
	}

	void onFolder(CCObject*)     { geode::utils::file::openFolder(replays::dir()); }
	void onRefresh(CCObject*)    { refresh(); }
	void onNoclip(CCObject*)     { hacks::toggleNoclip(); refresh(); }
	void onHitbox(CCObject*)     { hacks::toggleHitboxes(); refresh(); }
	void onSpeed(CCObject*)      { hacks::toggleSpeed(); refresh(); }
	void onSpeedStep(CCObject* s){ hacks::setSpeed(g_hacks.speed + static_cast<CCFloat*>(static_cast<CCNode*>(s)->getUserObject())->getValue()); refresh(); }
	void onSpeedReset(CCObject*) { hacks::setSpeed(1.f); refresh(); }
	void onStepper(CCObject*)    { hacks::toggleStepper(); refresh(); }
	void onSpPrev(CCObject*)     { if (needLevel()) { closeAndResume(); hacks::switchStartPos(-1); } }
	void onSpNext(CCObject*)     { if (needLevel()) { closeAndResume(); hacks::switchStartPos(1); } }
	void onSettings(CCObject*)   { geode::openSettingsPopup(Mod::get()); }
	void onResetButton(CCObject*);

public:
	static GDMenuPopup* create(PauseLayer* pause) {
		auto ret = new GDMenuPopup();
		if (ret->init(pause)) { ret->autorelease(); return ret; }
		delete ret;
		return nullptr;
	}
};

// ---------------------------------------------------------------- floating button
class FloatingButton : public CCLayer {
protected:
	CCNode* m_body = nullptr;
	CCPoint m_touchStart, m_nodeStart;
	bool m_dragged = false;

	bool init(PauseLayer*) {
		if (!CCLayer::init()) return false;
		this->setID("floating-button"_spr);
		this->ignoreAnchorPointForPosition(false);

		buildBody();

		auto win = CCDirector::get()->getWinSize();
		float fx = Mod::get()->getSavedValue<float>("btn-x", 0.93f);
		float fy = Mod::get()->getSavedValue<float>("btn-y", 0.82f);
		this->setPosition({ fx * win.width, fy * win.height });
		clampToScreen();

		this->scheduleUpdate();
		return true;
	}

	// Visible everywhere EXCEPT while actually playing (level or editor playtest)
	// and while any popup/alert is open on top.
	static bool shouldShow() {
		auto scene = CCDirector::get()->getRunningScene();
		if (!scene) return false;
		if (auto pl = PlayLayer::get(); pl && !pl->m_isPaused && !scene->getChildByType<PauseLayer>(0)) return false;
		if (auto ed = LevelEditorLayer::get(); ed && ed->m_playbackMode == PlaybackMode::Playing) return false;
		if (GDMenuPopup::s_openCount > 0) return false;
		if (scene->getChildByType<FLAlertLayer>(0)) return false;
		return true;
	}

	void update(float) override {
		bool show = shouldShow();
		if (show == this->isVisible()) return;
		this->setVisible(show);
		if (show) {
			rebuildLook();
			this->setScale(0.f);
			this->runAction(CCEaseBackOut::create(CCScaleTo::create(0.2f, 1.f)));
		}
	}

	void rebuildLook() {
		// refresh ring colour / state text
		if (m_body) m_body->removeFromParent();
		m_body = nullptr;
		buildBody();
	}

	void buildBody() {
		float scale = (float)Mod::get()->getSettingValue<double>("hud-scale") * extras::bubbleSize() * (UI_SCALE + 0.1f);
		float alpha = extras::bubbleOpacity();
		float r = 20.f * scale;                 // bubble radius
		CCSize sz = { r * 2, r * 2 };
		m_body = CCNode::create();
		m_body->setContentSize(sz);

		// round floating bubble: soft shadow, coloured ring (shows bot state), dark face
		auto draw = CCDrawNode::create();
		CCPoint c = CCPoint(r, r);
		ccColor3B ring = g_bot.state == BotState::Idle ? extras::accent() : bot::stateColor();
		// real circles built from a 48-sided polygon (GD's drawDot can render as a square)
		auto circle = [&](CCPoint center, float radius, ccColor4F color) {
			constexpr int N = 48;
			CCPoint pts[N];
			for (int i = 0; i < N; i++) {
				float a = (float)i / N * 2.f * (float)M_PI;
				pts[i] = center + CCPoint(std::cos(a) * radius, std::sin(a) * radius);
			}
			color.a *= alpha;
			draw->drawPolygon(pts, N, color, 0.f, color);
		};
		circle(c + CCPoint(0, -1.5f * scale), r + 1.f, { 0.f, 0.f, 0.f, 0.35f });
		circle(c, r, { ring.r / 255.f, ring.g / 255.f, ring.b / 255.f, 1.f });
		circle(c, r - 2.5f * scale, { 0.09f, 0.10f, 0.16f, 0.95f });
		m_body->addChild(draw);

		auto title = CCLabelBMFont::create("GDM", "bigFont.fnt");
		title->setScale(0.42f * scale);
		title->setPosition(c + CCPoint(0, 3.f * scale));
		title->setOpacity((GLubyte)(255 * alpha));
		m_body->addChild(title);

		auto sub = CCLabelBMFont::create(g_bot.state == BotState::Idle ? "menu" : bot::stateName(), "chatFont.fnt");
		sub->setScale(0.42f * scale);
		sub->setColor(ring);
		sub->setPosition(c + CCPoint(0, -8.f * scale));
		sub->setOpacity((GLubyte)(255 * alpha));
		m_body->addChild(sub);

		this->setContentSize(sz);
		this->setAnchorPoint({ 0.5f, 0.5f });
		m_body->setPosition({ 0, 0 });
		this->addChild(m_body);

		// slow idle "breathing" so it reads as a floating bubble
		m_body->runAction(CCRepeatForever::create(CCSequence::create(
			CCEaseSineInOut::create(CCMoveBy::create(1.4f, { 0, 2.f })),
			CCEaseSineInOut::create(CCMoveBy::create(1.4f, { 0, -2.f })), nullptr)));

	}

	void clampToScreen() {
		auto win = CCDirector::get()->getWinSize();
		auto half = this->getContentSize() / 2;
		this->setPosition({
			std::clamp(this->getPositionX(), half.width, win.width - half.width),
			std::clamp(this->getPositionY(), half.height + 8.f, win.height - half.height)
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
		float pad = 6.f; // slightly larger hit area for fingers
		if (local.x < -pad || local.y < -pad || local.x > sz.width + pad || local.y > sz.height + pad) return false;
		m_touchStart = touch->getLocation();
		m_nodeStart = this->getPosition();
		m_dragged = false;
		this->stopAllActions();
		this->runAction(CCEaseOut::create(CCScaleTo::create(0.08f, 0.9f), 2.f));
		return true;
	}

	void ccTouchMoved(CCTouch* touch, CCEvent*) override {
		auto delta = touch->getLocation() - m_touchStart;
		if (!m_dragged && delta.getLength() > 8.f) m_dragged = true;
		if (m_dragged) {
			this->setPosition(m_nodeStart + delta);
			clampToScreen();
		}
	}

	void ccTouchEnded(CCTouch*, CCEvent*) override {
		this->stopAllActions();
		this->runAction(CCEaseBackOut::create(CCScaleTo::create(0.15f, 1.f)));
		if (m_dragged) {
			auto win = CCDirector::get()->getWinSize();
			Mod::get()->setSavedValue<float>("btn-x", this->getPositionX() / win.width);
			Mod::get()->setSavedValue<float>("btn-y", this->getPositionY() / win.height);
			return;
		}
		auto scene = CCDirector::get()->getRunningScene();
		auto pause = scene ? scene->getChildByType<PauseLayer>(0) : nullptr;
		if (auto popup = GDMenuPopup::create(pause)) popup->show();
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

void GDMenuPopup::onResetButton(CCObject*) {
	Mod::get()->setSavedValue<float>("btn-x", 0.93f);
	Mod::get()->setSavedValue<float>("btn-y", 0.82f);
	if (true)
		if (auto btn = OverlayManager::get()->getChildByID("floating-button"_spr)) {
			auto win = CCDirector::get()->getWinSize();
			btn->setPosition({ 0.93f * win.width, 0.82f * win.height });
		}
	notify("Button moved back to the top right");
}



// floating bubble: lives in Geode's overlay (drawn above EVERY scene: search, level info,
// creator, settings, editor, pause...) and hides itself during gameplay.
static void ensureBubble() {
	auto overlay = OverlayManager::get();
	if (!overlay->getChildByID("floating-button"_spr))
		if (auto btn = FloatingButton::create(nullptr)) overlay->addChild(btn, 1000);
}

$on_mod(Loaded) {
	queueInMainThread([] { ensureBubble(); });
}

class $modify(GDMenuMainMenu, MenuLayer) {
	bool init() {
		if (!MenuLayer::init()) return false;
		ensureBubble();
		return true;
	}
};
