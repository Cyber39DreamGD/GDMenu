// Custom gameplay click sound (#83): drop .mp3 (or .wav/.ogg) files into the
// clicksounds folder, pick one in the Style tab, and every real jump click in
// gameplay plays it instead of just the stock click.
#include "state.hpp"
#include <Geode/DefaultInclude.hpp>

namespace sounds {
	std::filesystem::path dir() {
		auto d = Mod::get()->getSaveDir() / "clicksounds";
		std::error_code ec;
		std::filesystem::create_directories(d, ec);
		return d;
	}

	static std::string toLower(std::string s) {
		for (char& c : s) c = (char)std::tolower((unsigned char)c);
		return s;
	}

	static bool isSoundExt(char const* ext) {
		auto e = toLower(ext);
		return e == ".mp3" || e == ".wav" || e == ".ogg";
	}

	std::vector<std::string> listSounds() {
		std::vector<std::string> out;
		std::error_code ec;
		for (auto& e : std::filesystem::directory_iterator(dir(), ec)) {
			if (!e.is_regular_file()) continue;
			if (!isSoundExt(e.path().extension().string().c_str())) continue;
			out.push_back(e.path().filename().string());
		}
		std::sort(out.begin(), out.end());
		return out;
	}

	std::string selected() {
		auto n = Mod::get()->getSettingValue<std::string>("click-sound");
		// forget it if the file was removed from the folder
		if (!n.empty() && !std::filesystem::exists(dir() / n)) n = "";
		return n;
	}

	void select(std::string const& name) {
		Mod::get()->setSettingValue<std::string>("click-sound", name);
		if (auto engine = FMODAudioEngine::get())
			if (!name.empty()) engine->preloadEffect((dir() / name).string());
	}

	void playClick() {
		auto name = selected();
		if (name.empty()) return;
		if (auto engine = FMODAudioEngine::get())
			engine->playEffect((dir() / name).string());
	}
}

// the autoclicker drives handleButton itself; flag those presses so the click
// sound doesn't fire on every autoclick
static bool s_fromAutoclick = false;
void extras::flagAutoclick(bool on) { s_fromAutoclick = on; }
bool sounds::fromAutoclick() { return s_fromAutoclick; }
