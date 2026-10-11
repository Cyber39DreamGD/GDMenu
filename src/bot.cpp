// Bot: recording, playback, resume sessions and .gdbot replay files (native GDMR layout; old GDR2 files still load).
#include "state.hpp"
#include <Geode/utils/base64.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <gdr/gdr.hpp>
#include <fstream>
#include <cmath>
#include <cstring>
#include <span>

BotData g_bot;

void notify(std::string const& msg, NotificationIcon icon) {
	Notification::create(msg, icon, 1.4f)->show();
}

// ---------------------------------------------------------------- helpers
namespace {
	// Click Between Frames makes inputs land *between* physics ticks, so a replay (which is
	// tick-based) can't reproduce them exactly -> random desyncs/deaths. Eclipse soft-disables
	// it while the bot is active; we do the same and restore it afterwards.
	constexpr auto CBF_ID = "syzzi.click_between_frames";
	bool s_cbfTouched = false;
	bool s_cbfPrev = false;

	void updateCBF(bool botActive) {
		auto cbf = Loader::get()->getLoadedMod(CBF_ID);
		if (!cbf) return;
		if (botActive && !s_cbfTouched) {
			s_cbfPrev = cbf->getSettingValue<bool>("soft-toggle");
			cbf->setSettingValue<bool>("soft-toggle", true);
			s_cbfTouched = true;
		}
		else if (!botActive && s_cbfTouched) {
			cbf->setSettingValue<bool>("soft-toggle", s_cbfPrev);
			s_cbfTouched = false;
		}
	}

	void setState(BotState st) {
		g_bot.state = st;
		updateCBF(st != BotState::Idle);
	}

	bool isTwoPlayer(GJBaseGameLayer* gl) {
		return gl->m_levelSettings && gl->m_levelSettings->m_twoPlayerMode;
	}

	// GDR2 / Eclipse convention: handleButton's 3rd arg is "isPlayer1"; an input is player 2
	// only in a 2-player level while dual mode is active.
	void pressRaw(GJBaseGameLayer* gl, bool down, int button, bool player2) {
		g_bot.botInput = true;
		gl->handleButton(down, button, !player2);
		g_bot.botInput = false;
	}

	void releaseAll(GJBaseGameLayer* gl) {
		for (int b = 1; b <= 3; b++) {
			pressRaw(gl, false, b, false);
			pressRaw(gl, false, b, true);
		}
		std::memset(g_bot.held, 0, sizeof(g_bot.held));
	}

	PlayerObject* playerFor(GJBaseGameLayer* gl, bool player2) {
		return (player2 && gl->m_player2) ? gl->m_player2 : gl->m_player1;
	}

	BotInput makeInput(GJBaseGameLayer* gl, int frame, int button, bool down, bool player2) {
		BotInput in{ frame, button, down, player2 };
		if (auto pl = playerFor(gl, player2)) {
			in.phys = true;
			in.x = pl->m_position.x;
			in.y = pl->m_position.y;
			in.rot = pl->getRotation();
			in.xVel = pl->m_platformerXVelocity;
			in.yVel = pl->m_yVelocity;
		}
		return in;
	}

	bool inputFixEnabled() {
		return Mod::get()->getSettingValue<bool>("input-fix");
	}

	void applyPhys(GJBaseGameLayer* gl, BotInput const& in) {
		if (!in.phys || !inputFixEnabled()) return;
		auto pl = playerFor(gl, in.player2);
		if (!pl || pl->m_isDead) return;
		pl->m_position = CCPoint(in.x, in.y);
		pl->setPosition(pl->m_position);
		pl->setRotation(in.rot);
		pl->m_yVelocity = in.yVel;
		if (gl->m_isPlatformer) pl->m_platformerXVelocity = in.xVel;
	}

	PlayerFix capture(PlayerObject* p) {
		return { p->m_position.x, p->m_position.y, p->getRotation(), p->m_platformerXVelocity, p->m_yVelocity };
	}
	void restore(GJBaseGameLayer* gl, PlayerObject* p, PlayerFix const& f) {
		if (!p || p->m_isDead) return;
		p->m_position = CCPoint(f.x, f.y);
		p->setPosition(p->m_position);
		p->setRotation(f.rot);
		p->m_yVelocity = f.yVel;
		if (gl->m_isPlatformer) p->m_platformerXVelocity = f.xVel;
	}

	std::filesystem::path fixesPath(int levelID) {
		return Mod::get()->getSaveDir() / "sessions" / fmt::format("{}.gdmf", levelID);
	}

	std::filesystem::path sessionPath(int levelID) {
		return Mod::get()->getSaveDir() / "sessions" / fmt::format("{}.gdm", levelID);
	}
}

int bot::frame() {
	auto pl = PlayLayer::get();
	return pl ? (int)pl->m_gameState.m_currentProgress : 0;
}

char const* bot::stateName() {
	switch (g_bot.state) {
		case BotState::Recording: return "RECORDING";
		case BotState::Playing:   return "PLAYING";
		case BotState::Resuming:  return "RESUMING";
		default:                  return "IDLE";
	}
}

ccColor3B bot::stateColor() {
	switch (g_bot.state) {
		case BotState::Recording: return { 255, 90, 90 };
		case BotState::Playing:   return { 90, 255, 120 };
		case BotState::Resuming:  return { 255, 210, 80 };
		default:                  return { 200, 200, 200 };
	}
}

void bot::setTimeScale(float s) {
	CCDirector::get()->getScheduler()->setTimeScale(s);
}

void bot::startRecording() {
	auto pl = PlayLayer::get();
	if (!pl) return;
	g_bot.inputs.clear();
	g_bot.fixes.clear();
	g_bot.loadedName.clear();
	setState(BotState::Recording);
	std::memset(g_bot.held, 0, sizeof(g_bot.held));
	pl->resetLevel();
	notify("Recording started", NotificationIcon::Success);
}

void bot::stop() {
	auto was = g_bot.state;
	setState(BotState::Idle);
	bot::setTimeScale(1.f);
	if (auto pl = PlayLayer::get()) releaseAll(pl);
	if (was == BotState::Recording) notify(fmt::format("Recording stopped ({} inputs)", g_bot.inputs.size()));
	else if (was != BotState::Idle) notify("Playback stopped");
}

bool bot::startPlayback() {
	auto pl = PlayLayer::get();
	if (!pl) return false;
	if (g_bot.inputs.empty()) { notify("No bot loaded - open the Bots tab", NotificationIcon::Warning); return false; }
	std::stable_sort(g_bot.inputs.begin(), g_bot.inputs.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
	setState(BotState::Playing);
	g_bot.playIndex = 0;
	pl->resetLevel();
	notify("Playing bot", NotificationIcon::Success);
	return true;
}

void bot::clear() {
	bot::stop();
	g_bot.inputs.clear();
	g_bot.fixes.clear();
	g_bot.loadedName.clear();
}

void bot::arm(std::string const& file, int levelID) {
	g_bot.armedBot = file;
	g_bot.armedLevelID = levelID;
}
void bot::disarm() {
	g_bot.armedBot.clear();
	g_bot.armedLevelID = 0;
}

// ---------------------------------------------------------------- sessions
// Binary: "GDMS" u32 version, i32 resumeFrame, f32 percent, u32 count, then count * {i32 frame, u8 button, u8 down, u8 p2}
static void writeFixes(std::ostream& f, std::vector<FrameFix> const& fixes) {
	auto w = [&](auto v) { f.write(reinterpret_cast<char const*>(&v), sizeof(v)); };
	w(uint32_t(fixes.size()));
	for (auto& x : fixes) {
		w(int32_t(x.frame)); w(uint8_t(x.hasP2));
		for (auto* p : { &x.p1, &x.p2 }) { w(p->x); w(p->y); w(p->rot); w(p->xVel); w(p->yVel); }
	}
}
static void readFixes(std::istream& f, std::vector<FrameFix>& fixes) {
	auto r = [&](auto& v) { f.read(reinterpret_cast<char*>(&v), sizeof(v)); return (bool)f; };
	fixes.clear();
	uint32_t n;
	if (!r(n)) return;
	fixes.reserve(n);
	for (uint32_t k = 0; k < n; k++) {
		FrameFix x; int32_t fr; uint8_t h;
		if (!r(fr) || !r(h)) break;
		x.frame = fr; x.hasP2 = h != 0;
		bool ok = true;
		for (auto* p : { &x.p1, &x.p2 }) ok = ok && r(p->x) && r(p->y) && r(p->rot) && r(p->xVel) && r(p->yVel);
		if (!ok) break;
		fixes.push_back(x);
	}
}

void bot::saveSession() {
	auto pl = PlayLayer::get();
	if (!pl || g_bot.inputs.empty()) return;
	auto path = sessionPath(g_bot.levelID);
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream f(path, std::ios::binary);
	if (!f) return;
	auto w = [&](auto v) { f.write(reinterpret_cast<char const*>(&v), sizeof(v)); };
	f.write("GDMS", 4);
	w(uint32_t(3));
	w(int32_t(bot::frame()));
	w(float(pl->getCurrentPercent()));
	w(uint32_t(g_bot.inputs.size()));
	for (auto& i : g_bot.inputs) {
		w(int32_t(i.frame)); w(uint8_t(i.button)); w(uint8_t(i.down)); w(uint8_t(i.player2));
		w(uint8_t(i.phys)); w(i.x); w(i.y); w(i.rot); w(i.xVel); w(i.yVel);
	}
	std::ofstream ff(fixesPath(g_bot.levelID), std::ios::binary | std::ios::trunc);
	if (ff) writeFixes(ff, g_bot.fixes);
}

static bool readSession(int levelID, int& resumeFrame, float& percent, std::vector<BotInput>* out, size_t& count) {
	std::ifstream f(sessionPath(levelID), std::ios::binary);
	if (!f) return false;
	char magic[4]; f.read(magic, 4);
	if (std::string_view(magic, 4) != "GDMS") return false;
	auto r = [&](auto& v) { f.read(reinterpret_cast<char*>(&v), sizeof(v)); return (bool)f; };
	uint32_t ver, n; int32_t rf; float pc;
	if (!r(ver) || !r(rf) || !r(pc) || !r(n)) return false;
	resumeFrame = rf; percent = pc; count = n;
	if (!out) return true;
	out->clear();
	out->reserve(n);
	for (uint32_t k = 0; k < n; k++) {
		int32_t fr; uint8_t b, d, p;
		if (!r(fr) || !r(b) || !r(d) || !r(p)) break;
		BotInput in{ fr, b, d != 0, p != 0 };
		if (ver >= 3) {
			uint8_t ph;
			if (!r(ph) || !r(in.x) || !r(in.y) || !r(in.rot) || !r(in.xVel) || !r(in.yVel)) break;
			in.phys = ph != 0;
		}
		out->push_back(in);
	}
	return true;
}

bool bot::hasSession(int levelID) {
	return std::filesystem::exists(sessionPath(levelID));
}

bool bot::sessionInfo(int levelID, float& percent, size_t& inputs) {
	int rf;
	return readSession(levelID, rf, percent, nullptr, inputs);
}

void bot::deleteSession(int levelID) {
	std::error_code ec;
	std::filesystem::remove(fixesPath(levelID), ec);
	std::filesystem::remove(sessionPath(levelID), ec);
}

std::vector<bot::Session> bot::listSessions() {
	std::vector<Session> out;
	auto dir = sessionPath(0).parent_path();
	std::error_code ec;
	for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
		auto p = e.path();
		if (!e.is_regular_file() || p.extension() != ".gdm") continue; // we always write lowercase
		std::string stem = p.stem().string();
		if (stem.empty() || stem.find_first_not_of("0123456789") != std::string::npos) continue;
		int id = 0;
		try { id = std::stoi(stem); } catch (...) { continue; }
		int rf; float pct = 0.f; size_t n = 0;
		if (!readSession(id, rf, pct, nullptr, n) || n == 0) continue;
		Session s;
		s.levelID = id;
		s.percent = pct;
		s.inputs = n;
		if (auto lv = replays::findLocalLevel(id)) s.levelName = std::string(lv->m_levelName);
		out.push_back(std::move(s));
	}
	std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.levelID < b.levelID; });
	return out;
}

bool bot::resumeSession() {
	auto pl = PlayLayer::get();
	if (!pl) return false;
	size_t n;
	if (!readSession(g_bot.levelID, g_bot.resumeFrame, g_bot.lastPercent, &g_bot.inputs, n) || g_bot.resumeFrame <= 0) {
		notify("No session to resume", NotificationIcon::Warning);
		return false;
	}
	std::erase_if(g_bot.inputs, [](BotInput const& i) { return i.frame > g_bot.resumeFrame; });
	{
		std::ifstream ff(fixesPath(g_bot.levelID), std::ios::binary);
		if (ff) readFixes(ff, g_bot.fixes); else g_bot.fixes.clear();
		std::erase_if(g_bot.fixes, [](FrameFix const& x) { return x.frame > g_bot.resumeFrame; });
	}
	g_bot.fixIndex = 0;
	setState(BotState::Resuming);
	g_bot.playIndex = 0;
	bot::setTimeScale((float)Mod::get()->getSettingValue<double>("resume-speed"));
	pl->resetLevel();
	notify(fmt::format("Resuming to {:.1f}%...", g_bot.lastPercent));
	return true;
}

// ---------------------------------------------------------------- replay files
namespace {
	// GDR2 import - IMPORT ONLY: reads files and share codes from v2.7.0 and earlier.
	// Same tag + byte layout as gdr's standard PhysicsInput ("Phys"), but fields default to NaN
	// so we can tell whether a loaded file actually had physics data.
	struct GDMInput : gdr::Input<"Phys"> {
		float xPosition = NAN, yPosition = NAN, rotation = 0.f;
		double xVelocity = 0.0, yVelocity = 0.0;
		GDMInput() = default;
		GDMInput(uint64_t frame, uint8_t button, bool player2, bool down, float x, float y, float rot, double xv, double yv)
			: Input(frame, button, player2, down), xPosition(x), yPosition(y), rotation(rot), xVelocity(xv), yVelocity(yv) {}
		void parseExtension(binary_reader& reader) override {
			reader >> xPosition >> yPosition >> rotation >> xVelocity >> yVelocity;
		}
		void saveExtension(binary_writer& writer) const override {
			writer << xPosition << yPosition << rotation << xVelocity << yVelocity;
		}
	};

	struct GDMReplay : gdr::Replay<GDMReplay, GDMInput> {
		std::vector<FrameFix> fixes;
		GDMReplay() : Replay("GDMenu", 4) {}

		void saveExtension(binary_writer& w) const override {
			w << (uint64_t)fixes.size();
			for (auto& x : fixes) {
				w << (uint64_t)x.frame << x.hasP2;
				for (auto* p : { &x.p1, &x.p2 }) w << p->x << p->y << p->rot << p->xVel << p->yVel;
			}
		}
		void parseExtension(binary_reader& r) override {
			uint64_t n = 0;
			r >> n;
			fixes.clear();
			fixes.reserve((size_t)std::min<uint64_t>(n, 50'000'000));
			for (uint64_t k = 0; k < n; k++) {
				FrameFix x; uint64_t fr;
				r >> fr >> x.hasP2;
				for (auto* p : { &x.p1, &x.p2 }) r >> p->x >> p->y >> p->rot >> p->xVel >> p->yVel;
				x.frame = (int)fr;
				fixes.push_back(x);
			}
		}
		bool shouldParseExtension() const override {
			return botInfo.name == "GDMenu" && botInfo.version >= 4;
		}
	};

	// ---------------------------------------------------------------- native format (GDMR)
	// GDMenu's own bot file layout. v2.7.1+ no longer writes GDR2; old .gdbot files
	// and share codes (GDR2) still load through the GDMReplay fallback below.
	namespace gdmr {
		char const MAGIC[4] = { 'G', 'D', 'M', 'R' };
		constexpr uint32_t VERSION = 1;

		struct Data {
			std::string author;
			std::string description;
			int gameVersion = 0;
			double framerate = 240.0;
			int levelID = 0;
			std::string levelName;
			bool platformer = false;
			bool ldm = false;
			float duration = 0.f;
			std::vector<BotInput> inputs;
			std::vector<FrameFix> fixes;
		};

		std::vector<uint8_t> encode(Data const& d) {
			std::vector<uint8_t> b;
			auto w8  = [&](uint8_t v) { b.push_back(v); };
			auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) w8((v >> (8 * i)) & 0xff); };
			auto w64 = [&](uint64_t v) { for (int i = 0; i < 8; i++) w8((v >> (8 * i)) & 0xff); };
			auto wf32 = [&](float v)  { uint32_t u; std::memcpy(&u, &v, 4); w32(u); };
			auto wf64 = [&](double v) { uint64_t u; std::memcpy(&u, &v, 8); w64(u); };
			auto wstr = [&](std::string const& s) { w32((uint32_t)s.size()); for (char c : s) w8((uint8_t)c); };

			for (char c : MAGIC) w8((uint8_t)c);
			w32(VERSION);
			wstr(d.author);
			wstr(d.description);
			w32((uint32_t)(int32_t)d.gameVersion);
			wf64(d.framerate);
			w32((uint32_t)(int32_t)d.levelID);
			wstr(d.levelName);
			w8(d.platformer ? 1 : 0);
			w8(d.ldm ? 1 : 0);
			wf32(d.duration);
			w32((uint32_t)d.inputs.size());
			for (auto& i : d.inputs) {
				w64((uint64_t)(int64_t)i.frame);
				w8((uint8_t)i.button);
				w8(i.player2 ? 1 : 0);
				w8(i.down ? 1 : 0);
				w8(i.phys ? 1 : 0);
				wf32(i.x); wf32(i.y); wf32(i.rot);
				wf64(i.xVel); wf64(i.yVel);
			}
			w32((uint32_t)d.fixes.size());
			for (auto& x : d.fixes) {
				w32((uint32_t)(int32_t)x.frame);
				w8(x.hasP2 ? 1 : 0);
				for (auto* p : { &x.p1, &x.p2 }) { wf32(p->x); wf32(p->y); wf32(p->rot); wf64(p->xVel); wf64(p->yVel); }
			}
			return b;
		}

		bool decode(std::span<uint8_t> bytes, Data& out, std::string& error) {
			size_t pos = 0;
			bool ok = true;
			auto r8   = [&]() -> uint8_t  { if (pos >= bytes.size()) { ok = false; return 0; } return bytes[pos++]; };
			auto r32  = [&]() -> uint32_t { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)r8() << (8 * i); return v; };
			auto r64  = [&]() -> uint64_t { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)r8() << (8 * i); return v; };
			auto rf32 = [&]() -> float  { uint32_t u = r32(); float  v; std::memcpy(&v, &u, 4); return v; };
			auto rf64 = [&]() -> double { uint64_t u = r64(); double v; std::memcpy(&v, &u, 8); return v; };
			auto rstr = [&]() -> std::string {
				uint32_t n = r32();
				if (!ok) return {};
				if (pos + (size_t)n > bytes.size()) { ok = false; return {}; }
				std::string s(reinterpret_cast<char const*>(bytes.data() + pos), n);
				pos += n;
				return s;
			};

			if (bytes.size() < 4) { error = "too short"; return false; }
			for (int i = 0; i < 4; i++) if (bytes[i] != (uint8_t)MAGIC[i]) { error = "not a GDMenu bot file"; return false; }
			pos = 4;
			uint32_t ver = r32();
			if (!ok || ver != VERSION) { error = "unsupported version"; return false; }
			out.author = rstr();
			out.description = rstr();
			out.gameVersion = (int)(int32_t)r32();
			out.framerate = rf64();
			out.levelID = (int)(int32_t)r32();
			out.levelName = rstr();
			out.platformer = r8() != 0;
			out.ldm = r8() != 0;
			out.duration = rf32();
			uint32_t icount = r32();
			if (!ok || icount > 50'000'000u) { error = "bad input count"; return false; }
			out.inputs.clear();
			out.inputs.reserve(icount);
			for (uint32_t k = 0; k < icount; k++) {
				BotInput in{};
				in.frame = (int)(int64_t)r64();
				in.button = r8();
				in.player2 = r8() != 0;
				in.down = r8() != 0;
				in.phys = r8() != 0;
				in.x = rf32(); in.y = rf32(); in.rot = rf32();
				in.xVel = rf64(); in.yVel = rf64();
				if (!ok) { error = "truncated input"; return false; }
				out.inputs.push_back(in);
			}
			uint32_t fcount = r32();
			if (!ok || fcount > 50'000'000u) { error = "bad fix count"; return false; }
			out.fixes.clear();
			out.fixes.reserve(fcount);
			for (uint32_t k = 0; k < fcount; k++) {
				FrameFix x{};
				x.frame = (int)(int32_t)r32();
				x.hasP2 = r8() != 0;
				for (auto* p : { &x.p1, &x.p2 }) { p->x = rf32(); p->y = rf32(); p->rot = rf32(); p->xVel = rf64(); p->yVel = rf64(); }
				if (!ok) { error = "truncated fix"; return false; }
				out.fixes.push_back(x);
			}
			if (!ok) { error = "truncated file"; return false; }
			return true;
		}
	}

	// Parse bot bytes in either the native GDMR format (v2.7.1+) or the old GDR2
	// format (v2.7.0 and earlier). Fills `out`; returns true on success.
	bool parseBot(std::span<uint8_t> bytes, gdmr::Data& out, std::string& error) {
		if (gdmr::decode(bytes, out, error)) return true;
		auto res = GDMReplay::importData(bytes);
		if (res.isErr()) { error = "not a readable bot file"; return false; }
		auto& r = res.unwrap();
		out.author = r.author;
		out.description = r.description;
		out.gameVersion = r.gameVersion;
		out.framerate = r.framerate;
		out.levelID = r.levelInfo.id;
		out.levelName = r.levelInfo.name;
		out.platformer = r.platformer;
		out.ldm = r.ldm;
		out.duration = r.duration;
		out.inputs.clear();
		out.inputs.reserve(r.inputs.size());
		for (auto& i : r.inputs) {
			BotInput in{ (int)i.frame, i.button == 0 ? 1 : (int)i.button, i.down, i.player2 };
			if (!std::isnan(i.xPosition) && !std::isnan(i.yPosition)) {
				in.phys = true;
				in.x = i.xPosition; in.y = i.yPosition; in.rot = i.rotation;
				in.xVel = i.xVelocity; in.yVel = i.yVelocity;
			}
			out.inputs.push_back(in);
		}
		out.fixes = r.fixes;
		return true;
	}

	std::string lower(std::string s) {
		for (auto& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	bool readFile(std::filesystem::path const& p, std::vector<uint8_t>& out) {
		std::ifstream f(p, std::ios::binary);
		if (!f) return false;
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	}

	std::string sanitize(std::string name) {
		std::erase_if(name, [](char c) { return std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos || (unsigned char)c < 32; });
		while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
		return name.empty() ? "replay" : name;
	}
}

std::filesystem::path replays::dir() {
	auto d = Mod::get()->getSaveDir() / "replays";
	std::error_code ec;
	std::filesystem::create_directories(d, ec);
	return d;
}

std::vector<replays::Info> replays::list() {
	std::vector<Info> out;
	std::error_code ec;
	for (auto& e : std::filesystem::directory_iterator(dir(), ec)) {
		if (!e.is_regular_file()) continue;
		if (lower(e.path().extension().string()) != ".gdbot") continue; // .gdbot is the only format
		Info info;
		info.path = e.path();
		info.name = e.path().filename().string();
		std::vector<uint8_t> bytes;
		if (readFile(e.path(), bytes)) {
			gdmr::Data d;
			std::string err;
			if (parseBot(std::span<uint8_t>(bytes), d, err)) {
				info.valid = true;
				info.inputs = d.inputs.size();
				info.levelName = d.levelName;
				info.levelID = d.levelID;
				info.author = d.author;
				info.duration = d.duration;
			}
		}
		out.push_back(std::move(info));
	}
	std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return lower(a.name) < lower(b.name); });
	return out;
}

bool replays::exists(std::string const& name) {
	return std::filesystem::exists(dir() / (sanitize(name) + ".gdbot"));
}

// Local level matching a replay/session's level id (for names + stars). Null if it's
// not in the local level list anymore (e.g. a deleted level).
GJGameLevel* replays::findLocalLevel(int levelID) {
	if (levelID <= 0) return nullptr;
	if (auto mgr = GameLevelManager::get())
		return mgr->getLocalLevel(levelID);
	return nullptr;
}

bool replays::save(std::string name, bool autoSave) {
	if (g_bot.inputs.empty()) {
		if (!autoSave) notify("Nothing to save - record first", NotificationIcon::Warning);
		return false;
	}

	gdmr::Data d;
	d.author = std::string(GJAccountManager::get()->m_username);
	d.description = "Recorded with GDMenu";
	d.gameVersion = GEODE_COMP_GD_VERSION;
	d.framerate = 240.0;
	if (auto gl = gameplay::active()) {
		d.levelID = gl->m_level->m_levelID.value();
		d.levelName = std::string(gl->m_level->m_levelName);
		d.platformer = gl->m_level->isPlatformer();
		d.ldm = gl->m_level->m_lowDetailModeToggled;
	}
	uint64_t last = 0;
	for (auto& i : g_bot.inputs) {
		// the GDMR format stores every button, so nothing needs filtering (unlike GDR2)
		d.inputs.push_back(i);
		if (d.inputs.back().frame < 0) d.inputs.back().frame = 0;
		last = std::max<uint64_t>(last, (uint64_t)std::max(0, i.frame));
	}
	d.duration = (float)(last / d.framerate);
	d.fixes = g_bot.fixes;
	std::stable_sort(d.inputs.begin(), d.inputs.end(), [](auto& a, auto& b) { return a.frame < b.frame; });

	auto path = dir() / (sanitize(name) + ".gdbot");
	auto bytes = gdmr::encode(d);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) { notify("Couldn't write file", NotificationIcon::Error); return false; }
	f.write(reinterpret_cast<char const*>(bytes.data()), (std::streamsize)bytes.size());
	f.close();
	g_bot.loadedName = path.filename().string();
	notify((autoSave ? "Auto-saved " : "Saved ") + g_bot.loadedName, NotificationIcon::Success);
	return true;
}

bool replays::load(std::filesystem::path const& path) {
	std::vector<uint8_t> bytes;
	if (!readFile(path, bytes)) { notify("Couldn't open file", NotificationIcon::Error); return false; }
	gdmr::Data d;
	std::string err;
	if (!parseBot(std::span<uint8_t>(bytes), d, err)) { notify("Not a readable bot file: " + err, NotificationIcon::Error); return false; }

	bot::stop();
	g_bot.inputs = std::move(d.inputs);
	std::stable_sort(g_bot.inputs.begin(), g_bot.inputs.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
	g_bot.fixes = std::move(d.fixes);
	g_bot.loadedName = path.filename().string();
	notify(fmt::format("Loaded {} ({} inputs)", g_bot.loadedName, g_bot.inputs.size()), NotificationIcon::Success);
	return true;
}

bool replays::remove(std::filesystem::path const& path) {
	std::error_code ec;
	bool ok = std::filesystem::remove(path, ec);
	if (ok && path.filename().string() == g_bot.loadedName) g_bot.loadedName.clear();
	return ok;
}

// ---------------------------------------------------------------- share code (#13)
// A bot as one copy-pasteable base64 string - no file manager needed.
namespace share {
	std::string encodeFile(std::filesystem::path const& file, std::string& error) {
		std::vector<uint8_t> bytes;
		if (!readFile(file, bytes)) { error = "Couldn't read the bot file"; return ""; }
		return geode::utils::base64::encode(bytes, geode::utils::base64::Base64Variant::Normal);
	}

	bool decodeToFile(std::string const& code, std::filesystem::path const& out, std::string& error) {
		auto res = geode::utils::base64::decode(code, geode::utils::base64::Base64Variant::Normal);
		if (res.isErr()) { error = "That doesn't look like a bot code"; return false; }
		auto bytes = res.unwrap();
		if (bytes.empty()) { error = "The code is empty"; return false; }
		// must actually be a readable bot (native GDMR or old GDR2)
		gdmr::Data d;
		std::string perr;
		if (!parseBot(std::span<uint8_t>(bytes), d, perr)) { error = "Not a valid bot code: " + perr; return false; }
		std::ofstream f(out, std::ios::binary | std::ios::trunc);
		if (!f) { error = "Couldn't write the file"; return false; }
		f.write(reinterpret_cast<char const*>(bytes.data()), (std::streamsize)bytes.size());
		f.close();
		return true;
	}
}

// ---------------------------------------------------------------- hooks
class $modify(BotGameLayer, GJBaseGameLayer) {
	// Timing matches Eclipse exactly so files are interchangeable:
	//   record:   in handleButton, frame = m_currentProgress
	//   playback: right AFTER processCommands, fire every input with frame <= m_currentProgress
	void handleButton(bool down, int button, bool isPlayer1) {
		bool mine = gameplay::isMine(this);
		if (mine && !g_bot.botInput && (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming))
			return; // the bot is driving: ignore the real player
		// #83 custom gameplay click sound: every real jump click (not the bot, not the autoclicker)
		if (mine && button == 1 && down && !g_bot.botInput && !sounds::fromAutoclick()
			&& g_bot.state == BotState::Idle)
			sounds::playClick();

		bool player2 = isTwoPlayer(this) && m_gameState.m_isDualMode && !isPlayer1;
		BotInput captured = makeInput(this, (int)m_gameState.m_currentProgress, button, down, player2);

		GJBaseGameLayer::handleButton(down, button, isPlayer1);

		if (!mine || g_bot.botInput || button < 1 || button > 3) return;
		g_bot.realHeld[player2][button] = down;

		if (g_bot.state != BotState::Recording) return;
		// non-platformer levels only ever use jump - buttons 2/3 do nothing there, so there is
		// nothing to record.
		if (button != 1 && !m_isPlatformer) return;
		if (m_player1->m_isDead) return;

		bool& held = g_bot.held[player2][button];
		if (held == down) return; // skip key-repeat duplicates
		held = down;
		g_bot.inputs.push_back(captured);
	}

	void processCommands(float dt, bool isHalfTick, bool isLastTick) {
		GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

		if (!gameplay::isMine(this)) return;
		int frame = (int)m_gameState.m_currentProgress;

		// recording: remember exactly where the player is after every tick
		if (g_bot.state == BotState::Recording) {
			if (m_player1->m_isDead) return;
			FrameFix x;
			x.frame = frame;
			x.p1 = capture(m_player1);
			x.hasP2 = m_gameState.m_isDualMode && m_player2;
			if (x.hasP2) x.p2 = capture(m_player2);
			if (!g_bot.fixes.empty() && g_bot.fixes.back().frame >= frame) {
				// shouldn't happen, but keep the list strictly increasing
				std::erase_if(g_bot.fixes, [&](FrameFix const& f) { return f.frame >= frame; });
			}
			g_bot.fixes.push_back(x);
			return;
		}

		if (g_bot.state != BotState::Playing && g_bot.state != BotState::Resuming) return;

		// playback: put the player exactly on the recorded path for this tick
		if (inputFixEnabled()) {
			while (g_bot.fixIndex < g_bot.fixes.size() && g_bot.fixes[g_bot.fixIndex].frame < frame) g_bot.fixIndex++;
			if (g_bot.fixIndex < g_bot.fixes.size() && g_bot.fixes[g_bot.fixIndex].frame == frame) {
				auto& x = g_bot.fixes[g_bot.fixIndex];
				restore(this, m_player1, x.p1);
				if (x.hasP2 && m_gameState.m_isDualMode) restore(this, m_player2, x.p2);
			}
		}

		bool twoP = isTwoPlayer(this);
		while (g_bot.playIndex < g_bot.inputs.size() && g_bot.inputs[g_bot.playIndex].frame <= frame) {
			auto& in = g_bot.inputs[g_bot.playIndex++];
			if (in.player2 && !twoP) continue;
			applyPhys(this, in);
			pressRaw(this, in.down, in.button, in.player2);
		}

		if (g_bot.state == BotState::Resuming && frame >= g_bot.resumeFrame) {
			// reached where you left off: hand control back and keep recording
			setState(BotState::Recording);
			bot::setTimeScale(1.f);
			releaseAll(this);
			// re-sync "held" with what the macro was holding so the next input is recorded correctly
			for (auto& in : g_bot.inputs) g_bot.held[in.player2][in.button] = in.down;
			for (int p = 0; p < 2; p++)
				for (int b = 1; b <= 3; b++)
					if (g_bot.held[p][b]) { g_bot.inputs.push_back(makeInput(this, frame, b, false, p == 1)); g_bot.held[p][b] = false; }
			hacks::setStepper(true); // freeze on the exact frame so you're not caught off guard
			notify(fmt::format("Resumed at {:.1f}% - step or turn off the stepper to continue", g_bot.lastPercent),
				NotificationIcon::Success);
		}
	}
};

class $modify(BotPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		setState(BotState::Idle);
		g_bot.playIndex = 0;
		g_bot.stepper = false;
		g_bot.pendingSteps = 0;
		std::memset(g_bot.realHeld, 0, sizeof(g_bot.realHeld));
		std::memset(g_bot.held, 0, sizeof(g_bot.held));
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		int id = level->m_levelID.value();
		if (id != g_bot.levelID) { // keep a loaded replay when re-entering the same level
			g_bot.inputs.clear();
			g_bot.fixes.clear();
	g_bot.fixes.clear();
			g_bot.loadedName.clear();
		}
		g_bot.levelID = id;
		// #46 armed bot: Play (or GO) was pressed before entering - start it now
		if (!g_bot.armedBot.empty() && g_bot.armedLevelID > 0 && id == g_bot.armedLevelID) {
			auto file = g_bot.armedBot;
			bot::disarm();
			auto path = replays::dir() / file;
			if (std::filesystem::exists(path) && replays::load(path)) bot::startPlayback();
		}
		if (bot::hasSession(id))
			notify("Saved bot session found - Pause > GDMenu > Resume");
		return true;
	}


	void resetLevel() {
		PlayLayer::resetLevel();
		practice::applyPending(this); // exact physics at the checkpoint (practice fix)
		int frame = (int)m_gameState.m_currentProgress;

		if (g_bot.state == BotState::Recording) {
			m_player1->m_isDashing = false; // dash orbs otherwise carry over (Eclipse does this too)
			if (m_player2) m_player2->m_isDashing = false;

			// drop everything at/after the respawn point
			std::erase_if(g_bot.inputs, [&](BotInput const& i) { return i.frame >= frame; });
			std::erase_if(g_bot.fixes, [&](FrameFix const& x) { return x.frame > frame; });

			// What was the macro holding at this frame? In a straight playback run that's the state
			// the bot will be in here, so make the recording continue from exactly that state and
			// only add a press/release if what you're physically holding now is different.
			bool macroHeld[2][4] = {};
			for (auto& in : g_bot.inputs) macroHeld[in.player2][in.button] = in.down;
			bool twoP = isTwoPlayer(this);
			for (int p = 0; p < 2; p++) {
				if (p == 1 && !twoP) continue;
				for (int b = 1; b <= 3; b++) {
					if (b != 1 && !m_isPlatformer) continue;
					bool want = g_bot.realHeld[p][b];
					if (macroHeld[p][b] != want) {
						g_bot.inputs.push_back(makeInput(this, frame, b, want, p == 1));
						pressRaw(this, want, b, p == 1);
					}
					g_bot.held[p][b] = want;
				}
			}
		}
		else if (g_bot.state == BotState::Playing || g_bot.state == BotState::Resuming) {
			releaseAll(this);
			g_bot.fixIndex = 0;
			g_bot.playIndex = 0;
			while (g_bot.playIndex < g_bot.inputs.size() && g_bot.inputs[g_bot.playIndex].frame < frame)
				g_bot.playIndex++;
		}
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		if (g_bot.state == BotState::Recording) {
			// auto-save the finished bot right away (toggle in the Bot tab)
			bool autoSaved = false;
			if (Mod::get()->getSettingValue<bool>("auto-save-bot")) {
				std::string name = m_level ? std::string(m_level->m_levelName) : "Bot";
				autoSaved = replays::save(name, true);
			}
			bot::saveSession();
			setState(BotState::Idle);
			notify(autoSaved ? "Level complete! Bot auto-saved" : "Level complete! Pause > GDMenu > Save Bot to keep it",
				NotificationIcon::Success);
		}
		else if (g_bot.state == BotState::Playing) {
			setState(BotState::Idle);
		}
	}

	void onQuit() {
		// save where you were so you can come back and continue
		if (g_bot.state == BotState::Recording) bot::saveSession();
		setState(BotState::Idle);
		g_bot.stepper = false;
		bot::setTimeScale(1.f);
		PlayLayer::onQuit();
	}
};

// autosave the session every time you pause while recording (protects against crashes)
class $modify(BotAutosavePause, PauseLayer) {
	void customSetup() {
		PauseLayer::customSetup();
		if (g_bot.state == BotState::Recording) bot::saveSession();
	}
};
