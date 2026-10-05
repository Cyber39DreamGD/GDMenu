// Hardest level (#58): pick a level by ID, see its name, and get an automatic
// screenshot of the win screen (saved to the newhardestpictures folder) when
// you beat it with a clean attempt.
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <ctime>
#include <fstream>
#define STB_IMAGE_WRITE_IMPLEMENTATION  // exactly once, in this TU
#include <third_party/stb_image_write.h>

#if defined(GEODE_IS_IOS)
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#elif defined(GEODE_IS_ANDROID)
#include <GLES2/gl2.h>
#elif defined(GEODE_IS_WINDOWS)
#include <windows.h>
#include <GL/gl.h>
#elif defined(GEODE_IS_MACOS)
#include <OpenGL/gl.h>
#endif

namespace hardest {
	int levelID() { return (int)Mod::get()->getSettingValue<int64_t>("hardest-id", 0); }
	void setLevelID(int id) {
		id = std::max(0, id);
		Mod::get()->setSettingValue<int64_t>("hardest-id", id);
	}

	LevelInfo info() {
		LevelInfo out;
		int id = levelID();
		if (id <= 0) return out;
		if (auto lv = replays::findLocalLevel(id)) {
			out.known = true;
			out.name = std::string(lv->m_levelName);
			out.stars = lv->m_stars.value();
		}
		return out;
	}

	std::filesystem::path shotsDir() {
		auto d = Mod::get()->getSaveDir() / "newhardestpictures";
		std::error_code ec;
		std::filesystem::create_directories(d, ec);
		return d;
	}

	int shotCount() {
		int n = 0;
		std::error_code ec;
		for (auto& e : std::filesystem::directory_iterator(shotsDir(), ec)) {
			if (!e.is_regular_file()) continue;
			auto ext = e.path().extension().string();
			for (char& c : ext) c = (char)std::tolower((unsigned char)c);
			if (ext == ".png") n++;
		}
		return n;
	}

	// ---------------------------------------------------------------- screenshot
	static void flipRows(std::vector<unsigned char>& rgba, int w, int h) {
		for (int y = 0; y < h / 2; y++)
			for (int x = 0; x < w * 4; x++)
				std::swap(rgba[(size_t)y * w * 4 + x], rgba[(size_t)(h - 1 - y) * w * 4 + x]);
	}

	static bool readFrame(std::vector<unsigned char>& rgba, int& w, int& h) {
		// Called mid-frame from the scheduler: the GL context is current and the
		// framebuffer still holds the last completed frame - exactly what we want.
		w = h = 0;
#if defined(GEODE_IS_IOS)
		int rbw = 0, rbh = 0;
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &rbw);
		glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &rbh);
		w = rbw; h = rbh;
		if (w <= 0 || h <= 0)  // fall back to the drawable's viewport
#endif
		{
			int vp[4] = {};
			glGetIntegerv(GL_VIEWPORT, vp);
			w = vp[2]; h = vp[3];
		}
		if (w <= 0 || h <= 0) return false;
		rgba.resize((size_t)w * h * 4);
		glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		flipRows(rgba, w, h);
		return true;
	}

	static std::string timestamp() {
		char buf[32];
		std::time_t t = std::time(nullptr);
		std::tm tm{};
#if defined(GEODE_IS_WINDOWS)
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm);
		return buf;
	}

	// Queued from the win hook, executed on the next scheduler tick.
	bool s_pending = false;

	bool requestShot() {
		int id = levelID();
		if (id <= 0) return false;
		if (auto pl = PlayLayer::get(); pl && pl->m_level && pl->m_level->m_levelID.value() == id) {
			s_pending = true;
			return true;
		}
		return false;
	}

	bool consumePending() {
		if (!s_pending) return false;
		s_pending = false;
		int id = levelID();
		if (id <= 0) return false;
		std::vector<unsigned char> rgba;
		int w = 0, h = 0;
		if (!readFrame(rgba, w, h)) { notify("Hardest level beaten! (screenshot failed)", NotificationIcon::Warning); return true; }
		auto file = shotsDir() / fmt::format("hardest_{}-{}.png", id, timestamp());
		if (!stbi_write_png(file.string().c_str(), w, h, 4, rgba.data(), w * 4)) {
			notify("Hardest level beaten! (saving the screenshot failed)", NotificationIcon::Warning);
			return true;
		}
		notify(fmt::format("HARDEST BEATEN! Screenshot in newhardestpictures: {}", file.filename().string()), NotificationIcon::Success);
		return true;
	}
}
