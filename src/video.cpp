// 2.5.0 Video tab: ready-to-paste title & description for the botted level's
// showcase, driven by two small template files in the mod's folder.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <fstream>
#ifdef GEODE_IS_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef GEODE_IS_ANDROID
#include <Geode/cocos/platform/android/jni/JniHelper.h>
#endif

namespace video {

std::filesystem::path dir() { return Mod::get()->getSaveDir(); }
std::filesystem::path titlePath() { return dir() / "video-title.txt"; }
std::filesystem::path descriptionPath() { return dir() / "video-description.txt"; }

namespace {
	char const* DEFAULT_TITLE =
		"{level} by {creator} | Geometry Dash Showcase [Bot]";
	char const* DEFAULT_DESCRIPTION =
		"Level Name: {level}\n"
		"Creator: {creator}\n"
		"Level ID: {id}\n"
		"Difficulty: {difficulty}\n"
		"Bot Engine: {bot}\n"
		"Recording Physics: {fps}\n"
		"\n"
		"Botted Showcase created on mobile.\n"
		"Subscribe to Cyber39Dream Showcases for more GD content!";

	// Read the template file, creating it with the default if it doesn't exist yet.
	std::string ensureFile(std::filesystem::path const& p, char const* fallback) {
		std::ifstream f(p, std::ios::binary);
		if (f) {
			return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		}
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
		std::ofstream o(p, std::ios::binary);
		if (o) o << fallback;
		return std::string(fallback);
	}

	void replaceAll(std::string& s, char const* from, std::string const& to) {
		size_t pos = 0;
		while ((pos = s.find(from, pos)) != std::string::npos) {
			s.replace(pos, std::char_traits<char>::length(from), to);
			pos += to.size();
		}
	}

	void trimEnd(std::string& s) {
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
	}
}

void ensureDefaults() {
	ensureFile(titlePath(), DEFAULT_TITLE);
	ensureFile(descriptionPath(), DEFAULT_DESCRIPTION);
}

void writeDefaults() {
	std::ofstream o(titlePath(), std::ios::binary | std::ios::trunc);
	if (o) o << DEFAULT_TITLE;
	std::ofstream o2(descriptionPath(), std::ios::binary | std::ios::trunc);
	if (o2) o2 << DEFAULT_DESCRIPTION;
}

bool inBottedLevel(int levelID) {
	if (levelID <= 0) return false;
	for (auto& f : replays::list())
		if (f.levelID == levelID) return true;
	return false;
}

// The difficulty shown for {difficulty} is the user's pick, NOT the level's
// actual difficulty (it's for the showcase description, not a stat lookup).
namespace {
	char const* DIFFICULTIES[] = {
		"Auto", "Easy", "Normal", "Hard", "Harder", "Insane", "Easy-Extreme Demon", "Unrated"
	};
	constexpr int DIFFICULTY_COUNT = sizeof(DIFFICULTIES) / sizeof(DIFFICULTIES[0]);
}

int difficultyCount() { return DIFFICULTY_COUNT; }
int difficultyIndex() { return std::clamp((int)Mod::get()->getSavedValue<int64_t>("video-difficulty", 6), 0, DIFFICULTY_COUNT - 1); }
void setDifficultyIndex(int i) { Mod::get()->setSavedValue<int64_t>("video-difficulty", ((i % DIFFICULTY_COUNT) + DIFFICULTY_COUNT) % DIFFICULTY_COUNT); }
char const* difficultyLabel(int i) { return DIFFICULTIES[std::clamp(i, 0, DIFFICULTY_COUNT - 1)]; }

// Order matters a little: the free-text tags ({creator}, {level}) get replaced LAST,
// so a level name containing "{id}" can't be expanded a second time.
std::string fillTags(std::string const& templateText, GJGameLevel* level) {
	std::string out = templateText;
	if (level) {
		std::string creator = level->m_creatorName;
		if (creator.empty()) creator = "RobTop";
		replaceAll(out, "{id}", fmt::format("{}", level->m_levelID.value()));
		replaceAll(out, "{stars}", fmt::format("{}", level->m_stars.value()));
		replaceAll(out, "{creator}", creator);
		replaceAll(out, "{level}", std::string(level->m_levelName));
	}
	replaceAll(out, "{difficulty}", difficultyLabel(difficultyIndex())); // the user's pick
	replaceAll(out, "{bot}", "GDMenu Bot");
	replaceAll(out, "{fps}", "240 FPS");
	return out;
}

std::vector<std::string> splitLines(std::string const& text) {
	std::vector<std::string> out;
	std::string cur;
	for (char c : text) {
		if (c == '\n') { out.push_back(std::move(cur)); cur.clear(); }
		else if (c != '\r') cur += c;
	}
	out.push_back(std::move(cur));
	while (!out.empty() && out.back().empty()) out.pop_back();
	if (out.empty()) out.push_back("");
	return out;
}

Info fill() {
	Info out;
	auto pl = PlayLayer::get();
	if (!pl || !pl->m_level) return out;
	if (!inBottedLevel(pl->m_level->m_levelID.value())) return out;
	out.inBottedLevel = true;
	out.title = fillTags(ensureFile(titlePath(), DEFAULT_TITLE), pl->m_level);
	out.description = fillTags(ensureFile(descriptionPath(), DEFAULT_DESCRIPTION), pl->m_level);
	trimEnd(out.description);
	return out;
}

// ---------------------------------------------------------------- clipboard
// No Geode API for this, so each platform gets its own small implementation.
bool copy(std::string const& text) {
	if (text.empty()) return false;
#ifdef GEODE_IS_WINDOWS
	if (!OpenClipboard(nullptr)) return false;
	bool ok = false;
	if (EmptyClipboard()) {
		int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
		if (len > 0) {
			HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)len * sizeof(wchar_t));
			if (mem) {
				wchar_t* dst = static_cast<wchar_t*>(GlobalLock(mem));
				MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), dst, len);
				GlobalUnlock(mem);
				// on success the clipboard takes ownership of the block
				ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
				if (!ok) GlobalFree(mem);
			}
		}
	}
	CloseClipboard();
	return ok;
#elif defined(GEODE_IS_MACOS) || defined(GEODE_IS_IOS)
	// ObjC pasteboard code lives in apple-clipboard.mm
	return copyApple(text);
#elif defined(GEODE_IS_ANDROID)
	// Geode's own launcher exposes a static clipboard helper - use it the same way
	// the SDK does (no activity reference needed).
	if (!cocos2d::JniHelper::getJavaVM()) return false;
	cocos2d::JniMethodInfo t;
	if (!cocos2d::JniHelper::getStaticMethodInfo(t, "com/geode/launcher/utils/GeodeUtils", "writeClipboard", "(Ljava/lang/String;)V")) return false;
	jstring s = t.env->NewStringUTF(text.c_str());
	t.env->CallStaticVoidMethod(t.classID, t.methodID, s);
	t.env->DeleteLocalRef(s);
	t.env->DeleteLocalRef(t.classID);
	if (t.env->ExceptionCheck()) { t.env->ExceptionClear(); return false; }
	return true;
#else
	return false;
#endif
}
}

$on_mod(Loaded) { video::ensureDefaults(); }
