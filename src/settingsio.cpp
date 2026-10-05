// Settings export / import (#98): the whole mod configuration as one JSON blob -
// copy it to share your setup, or paste one back to restore it.
#include "state.hpp"
#include <fstream>

namespace settingsio {
	enum Type { Str, Bool, F64, I64 };
	struct Field { char const* key; Type type; bool saved; }; // saved = stored with setSavedValue, else setSettingValue

	// keep in sync with mod.json + every saved value the mod writes
	constexpr Field FIELDS[] = {
		// mod.json settings
		{ "step-key", Str, false }, { "toggle-stepper-key", Str, false },
		{ "resume-speed", F64, false }, { "speedhack", F64, false },
		{ "noclip-key", Str, false }, { "hitbox-key", Str, false }, { "speed-key", Str, false },
		{ "startpos-prev-key", Str, false }, { "startpos-next-key", Str, false },
		{ "hud-scale", F64, false }, { "stepper-touch-controls", Str, false },
		{ "auto-save-bot", Bool, false }, { "hud", Str, false },
		{ "practice-fix", Bool, false }, { "input-fix", Bool, false },
		// v2.7.0
		{ "auto-checkpoint", Bool, false }, { "warmup-mode", Bool, false }, { "editor-practice", Bool, false },
		{ "click-sound", Str, false }, { "hardest-id", I64, false }, { "panic-key", Str, false },
		{ "custom-accent", Bool, false }, { "accent-r", I64, false }, { "accent-g", I64, false }, { "accent-b", I64, false },
		// saved values
		{ "theme", I64, true }, { "bubble-opacity", F64, true }, { "bubble-size", F64, true },
		{ "autoclick-cps", F64, true }, { "safe-mode", Bool, true }, { "accuracy", Bool, true },
		{ "btn-x", F64, true }, { "btn-y", F64, true },
	};

	static matjson::Value read(Field const& f) {
	auto m = Mod::get();
	if (f.type == Str)
		return matjson::Value(f.saved ? m->getSavedValue<std::string>(f.key, std::string{}) : m->getSettingValue<std::string>(f.key));
	if (f.type == Bool)
		return matjson::Value(f.saved ? m->getSavedValue<bool>(f.key, false) : m->getSettingValue<bool>(f.key));
	if (f.type == F64)
		return matjson::Value(f.saved ? m->getSavedValue<double>(f.key, 0.0) : m->getSettingValue<double>(f.key));
	return matjson::Value(f.saved ? m->getSavedValue<int64_t>(f.key, (int64_t)0) : m->getSettingValue<int64_t>(f.key, (int64_t)0));
}

static void write(Field const& f, matjson::Value const& v) {
	auto m = Mod::get();
	if (f.type == Str && v.isString()) {
		auto s = v.asString().unwrapOr("");
		if (f.saved) m->setSavedValue<std::string>(f.key, s); else m->setSettingValue<std::string>(f.key, s);
	}
	else if (f.type == Bool && v.isBool()) {
		auto b = v.asBool().unwrapOr(false);
		if (f.saved) m->setSavedValue<bool>(f.key, b); else m->setSettingValue<bool>(f.key, b);
	}
	else if (f.type == F64 && v.isNumber()) {
		auto d = v.asDouble().unwrapOr(0.0);
		if (f.saved) m->setSavedValue<double>(f.key, d); else m->setSettingValue<double>(f.key, d);
	}
	else if (f.type == I64 && v.isNumber()) {
		auto i = (int64_t)v.asDouble().unwrapOr(0.0);
		if (f.saved) m->setSavedValue<int64_t>(f.key, i); else m->setSettingValue<int64_t>(f.key, i);
	}
}

std::string exportAll() {
		matjson::Value obj = matjson::Value::object();
		obj["gdmenu"] = std::string(Mod::get()->getVersion().toVString());
		for (auto& f : FIELDS) obj[f.key] = read(f);
		return obj.dump(4);
	}

	bool importAll(std::string const& json, std::string& error) {
		auto parsed = matjson::parse(json);
		if (parsed.isErr()) { error = "That is not valid JSON"; return false; }
		auto& obj = parsed.unwrap();
		if (!obj.isObject()) { error = "Expected a JSON object"; return false; }
		int applied = 0;
		for (auto& f : FIELDS)
			if (obj.contains(f.key)) { write(f, obj[f.key]); applied++; }
		if (applied == 0) { error = "No known GDMenu settings found in that text"; return false; }
		// re-read everything into the live state
		hacks::reloadSettings();
		extras::loadHackState();
		return true;
	}

	std::filesystem::path exportFile() {
		auto p = Mod::get()->getSaveDir() / "settings-export.json";
		std::ofstream f(p, std::ios::trunc);
		f << exportAll();
		f.close();
		return p;
	}
}
