#include "pch.h"

#include "Settings.h"

namespace bf
{
	Settings   S;
	std::mutex gBindMx;

	namespace
	{
		constexpr auto kIni = "Data/SKSE/Plugins/BladeFireMode.ini";

		enum class T { Bool, Float, Str, Bind };

		struct Item
		{
			const char* section;
			const char* key;
			T           type;
			void*       ptr;
			std::size_t cap;  // per Str
		};

		std::atomic<bool> gDirty{ false };

		std::vector<Item> Table()
		{
			return {
				{ "Generale", "Versione", T::Float, &S.iniVersion, 0 },

				{ "Spada", "Match", T::Str, S.swordMatch, sizeof(S.swordMatch) },

				{ "Tasti", "Accendi", T::Bind, &S.bindOn, 0 },
				{ "Tasti", "Spegni", T::Bind, &S.bindOff, 0 },


				{ "Animazione", "EstraiArma", T::Bool, &S.drawIfSheathed, 0 },
				{ "Animazione", "SecondiContatto", T::Float, &S.ignitionOffset, 0 },
				{ "Animazione", "SecondiAccensione", T::Float, &S.rampSeconds, 0 },
				{ "Animazione", "DurataClip", T::Float, &S.clipLength, 0 },
				{ "Animazione", "AttesaPartenza", T::Float, &S.startTimeout, 0 },

				{ "Animazione", "SecondiRiferimento", T::Float, &S.anchorTime, 0 },
				{ "Animazione", "UltimoFileScelto", T::Str, S.animSource, sizeof(S.animSource) },

				{ "Suoni", "Attivi", T::Bool, &S.soundEnabled, 0 },
				{ "Suoni", "Accensione", T::Str, S.soundIgnite, sizeof(S.soundIgnite) },
				{ "Suoni", "Spegnimento", T::Str, S.soundOff, sizeof(S.soundOff) },

				{ "Danni", "Fuoco", T::Bool, &S.fireDamage, 0 },
				{ "Danni", "Moltiplicatore", T::Float, &S.fireMult, 0 },
				{ "Danni", "Fisso", T::Float, &S.fireFlat, 0 },
				{ "Danni", "MoltAttaccoPotente", T::Float, &S.powerMult, 0 },
				{ "Danni", "RispettaResistenza", T::Bool, &S.respectResist, 0 },
				{ "Danni", "Bruciatura", T::Bool, &S.burn, 0 },
				{ "Danni", "BruciaturaAlSecondo", T::Float, &S.burnDps, 0 },
				{ "Danni", "BruciaturaSecondi", T::Float, &S.burnSeconds, 0 },

				{ "Mana", "CostoAccensione", T::Float, &S.manaStart, 0 },
				{ "Mana", "ConsumoAlSecondo", T::Float, &S.manaDrain, 0 },
				{ "Mana", "SogliaSpegnimento", T::Float, &S.manaMin, 0 },
				{ "Mana", "SoloInCombattimento", T::Bool, &S.drainOnlyInCombat, 0 },

				{ "Comportamento", "SpegniSeCambioArma", T::Bool, &S.offOnUnequip, 0 },
				{ "Comportamento", "SpegniSeRinfoderi", T::Bool, &S.offOnSheathe, 0 },

				{ "Diagnostica", "Log", T::Bool, &S.logEvents, 0 },
			};
		}

		std::string Trim(std::string s)
		{
			auto issp = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
			while (!s.empty() && issp(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
			while (!s.empty() && issp(static_cast<unsigned char>(s.back()))) s.pop_back();
			return s;
		}

		std::string Lower(std::string s)
		{
			for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		struct NamedKey { InputId id; const char* name; };

		// pad: 1..16 + 20/21 (grilletti)
		constexpr NamedKey kPad[] = {
			{ 1, "Pad_DpadUp" }, { 2, "Pad_DpadDown" }, { 3, "Pad_DpadLeft" }, { 4, "Pad_DpadRight" },
			{ 5, "Pad_Start" }, { 6, "Pad_Back" }, { 7, "Pad_L3" }, { 8, "Pad_R3" },
			{ 9, "Pad_LB" }, { 10, "Pad_RB" }, { 11, "Pad_A" }, { 12, "Pad_B" },
			{ 13, "Pad_X" }, { 14, "Pad_Y" }, { 20, "Pad_LT" }, { 21, "Pad_RT" },
		};

		// tastiera/mouse: 0x100 + VK
		constexpr NamedKey kKb[] = {
			{ 0x101, "Mouse1" }, { 0x102, "Mouse2" }, { 0x104, "Mouse3" }, { 0x105, "Mouse4" }, { 0x106, "Mouse5" },
			{ 0x108, "Backspace" }, { 0x109, "Tab" }, { 0x10D, "Enter" }, { 0x110, "Shift" }, { 0x111, "Ctrl" },
			{ 0x112, "Alt" }, { 0x113, "Pause" }, { 0x114, "CapsLock" }, { 0x11B, "Esc" }, { 0x120, "Space" },
			{ 0x121, "PageUp" }, { 0x122, "PageDown" }, { 0x123, "End" }, { 0x124, "Home" },
			{ 0x125, "Left" }, { 0x126, "Up" }, { 0x127, "Right" }, { 0x128, "Down" },
			{ 0x12D, "Insert" }, { 0x12E, "Delete" },
			{ 0x160, "Num0" }, { 0x161, "Num1" }, { 0x162, "Num2" }, { 0x163, "Num3" }, { 0x164, "Num4" },
			{ 0x165, "Num5" }, { 0x166, "Num6" }, { 0x167, "Num7" }, { 0x168, "Num8" }, { 0x169, "Num9" },
			{ 0x16A, "Num*" }, { 0x16B, "Num+" }, { 0x16D, "Num-" }, { 0x16E, "Num." }, { 0x16F, "Num/" },
			{ 0x1BA, ";" }, { 0x1BB, "=" }, { 0x1BC, "," }, { 0x1BD, "-" }, { 0x1BE, "." }, { 0x1BF, "/" },
			{ 0x1C0, "`" }, { 0x1DB, "[" }, { 0x1DC, "\\" }, { 0x1DD, "]" }, { 0x1DE, "'" },
		};
	}

	std::string InputName(InputId a_id)
	{
		for (auto& k : kPad) {
			if (k.id == a_id) return k.name;
		}
		for (auto& k : kKb) {
			if (k.id == a_id) return k.name;
		}
		if (a_id >= 0x100) {
			const auto vk = a_id - 0x100;
			if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
				return std::string(1, static_cast<char>(vk));
			}
			if (vk >= 0x70 && vk <= 0x87) {
				return std::format("F{}", vk - 0x70 + 1);
			}
			return std::format("VK{:02X}", vk);
		}
		return std::format("Pad{}", a_id);
	}

	bool InputFromName(const std::string& a_name, InputId& a_out)
	{
		const auto n = Lower(Trim(a_name));
		if (n.empty()) return false;
		for (auto& k : kPad) {
			if (Lower(k.name) == n) { a_out = k.id; return true; }
		}
		for (auto& k : kKb) {
			if (Lower(k.name) == n) { a_out = k.id; return true; }
		}
		if (n.size() == 1 && ((n[0] >= '0' && n[0] <= '9') || (n[0] >= 'a' && n[0] <= 'z'))) {
			a_out = static_cast<InputId>(0x100 + std::toupper(static_cast<unsigned char>(n[0])));
			return true;
		}
		if (n[0] == 'f' && n.size() <= 3) {
			int v = 0;
			bool ok = n.size() > 1;
			for (std::size_t i = 1; i < n.size(); ++i) {
				if (n[i] < '0' || n[i] > '9') { ok = false; break; }
				v = v * 10 + (n[i] - '0');
			}
			if (ok && v >= 1 && v <= 24) { a_out = static_cast<InputId>(0x100 + 0x70 + v - 1); return true; }
		}
		if (n.rfind("vk", 0) == 0 && n.size() > 2) {
			char* e = nullptr;
			const auto v = std::strtoul(n.c_str() + 2, &e, 16);
			if (e && *e == 0 && v > 0 && v < 0x100) { a_out = static_cast<InputId>(0x100 + v); return true; }
		}
		return false;
	}

	// formato: "Pad_LB+Pad_RB+Pad_DpadDown|0.30"  (dopo | i secondi di pressione)
	std::string BindingToString(const Binding& a_b)
	{
		std::string s;
		for (std::size_t i = 0; i < a_b.keys.size(); ++i) {
			if (i) s += '+';
			s += InputName(a_b.keys[i]);
		}
		if (!s.empty()) {
			s += std::format("|{:.2f}", a_b.hold);
		}
		return s;
	}

	bool BindingFromString(const std::string& a_s, Binding& a_out)
	{
		Binding b;
		auto    s = Trim(a_s);
		if (s.empty()) {
			a_out = b;
			return true;
		}
		std::string keys = s;
		if (const auto bar = s.find('|'); bar != std::string::npos) {
			keys = s.substr(0, bar);
			try {
				b.hold = std::clamp(std::stof(s.substr(bar + 1)), 0.0f, 5.0f);
			} catch (...) {
				b.hold = 0.0f;
			}
		}
		std::stringstream ss(keys);
		std::string       tok;
		while (std::getline(ss, tok, '+')) {
			InputId id{};
			if (!InputFromName(tok, id)) {
				return false;
			}
			if (std::find(b.keys.begin(), b.keys.end(), id) == b.keys.end()) {
				b.keys.push_back(id);
			}
		}
		if (b.keys.size() > 5) {
			b.keys.resize(5);
		}
		a_out = std::move(b);
		return true;
	}

	std::string BindingLabel(const Binding& a_b)
	{
		if (a_b.empty()) return "(nessuno)";
		std::string s;
		for (std::size_t i = 0; i < a_b.keys.size(); ++i) {
			if (i) s += " + ";
			s += InputName(a_b.keys[i]);
		}
		if (a_b.hold > 0.001f) {
			s += std::format("  (tieni {:.1f}s)", a_b.hold);
		}
		return s;
	}

	namespace
	{
		void Defaults()
		{
			S = Settings{};
			std::lock_guard l(gBindMx);
			S.bindOn.keys = { 9, 10, 2 };  // LB + RB + croce giu'
			S.bindOn.hold = 0.35f;
			S.bindOff.keys.clear();
		}
	}

	void LoadSettings()
	{
		Defaults();

		std::ifstream f(kIni);
		if (!f.good()) {
			SaveSettings();
			return;
		}

		std::unordered_map<std::string, std::string> kv;
		std::string                                  line, sec;
		while (std::getline(f, line)) {
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') continue;
			if (line.front() == '[' && line.back() == ']') {
				sec = Lower(line.substr(1, line.size() - 2));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) continue;
			kv[sec + "." + Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
		}

		for (auto& it : Table()) {
			const auto k = Lower(it.section) + "." + Lower(it.key);
			const auto p = kv.find(k);
			if (p == kv.end()) continue;
			const auto& v = p->second;
			switch (it.type) {
			case T::Bool:
				*static_cast<bool*>(it.ptr) = (v == "1" || Lower(v) == "true" || Lower(v) == "si" || Lower(v) == "yes");
				break;
			case T::Float:
				try {
					*static_cast<float*>(it.ptr) = std::stof(v);
				} catch (...) {}
				break;
			case T::Str:
			{
				const auto n = std::min(v.size(), it.cap - 1);
				std::memcpy(it.ptr, v.data(), n);
				static_cast<char*>(it.ptr)[n] = 0;
			}
				break;
			case T::Bind: {
				Binding b;
				if (BindingFromString(v, b)) {
					std::lock_guard l(gBindMx);
					*static_cast<Binding*>(it.ptr) = b;
				}
				break;
			}
			}
		}

		// Invarianti dell'opening: l'INI delle vecchie versioni non puo' piu'
		// disattivare la clip, cambiare evento o accendere la lama come fallback.
		S.iniVersion = 3.0f;
		S.animEnabled = true;
		S.fallbackIgnite = false;
		S.lockControls = true;
		S.lockAttack = true;
		S.badEvents[0] = 0;
		std::strncpy(S.triggerEvent, "IdleForceDefaultState", sizeof(S.triggerEvent) - 1);
		S.triggerEvent[sizeof(S.triggerEvent) - 1] = 0;
		std::strncpy(S.anchorTag, "Pie", sizeof(S.anchorTag) - 1);
		S.anchorTag[sizeof(S.anchorTag) - 1] = 0;
		std::strncpy(S.anchorPayload, "@APPLYSPELL", sizeof(S.anchorPayload) - 1);
		S.anchorPayload[sizeof(S.anchorPayload) - 1] = 0;
		S.anchorTime = 0.20f;
		S.startTimeout = 1.50f;
		// limiti di sicurezza
		S.fireMult = std::clamp(S.fireMult, 0.0f, 50.0f);
		S.fireFlat = std::clamp(S.fireFlat, 0.0f, 5000.0f);
		S.powerMult = std::clamp(S.powerMult, 0.0f, 10.0f);
		S.burnDps = std::clamp(S.burnDps, 0.0f, 1000.0f);
		S.burnSeconds = std::clamp(S.burnSeconds, 0.0f, 60.0f);
		S.manaStart = std::clamp(S.manaStart, 0.0f, 5000.0f);
		S.manaDrain = std::clamp(S.manaDrain, 0.0f, 500.0f);
		S.manaMin = std::clamp(S.manaMin, 0.0f, 1000.0f);
		S.ignitionOffset = std::clamp(S.ignitionOffset, 0.0f, 20.0f);
		S.rampSeconds = std::clamp(S.rampSeconds, 0.3f, 10.0f);
		S.clipLength = std::clamp(S.clipLength, 0.5f, 30.0f);
		if (S.clipLength <= S.ignitionOffset) S.clipLength = S.ignitionOffset + 0.25f;
		gDirty = true;

	}

	void SaveSettings()
	{
		std::filesystem::create_directories("Data/SKSE/Plugins");
		std::ofstream f(kIni, std::ios::trunc);
		if (!f.good()) {
			SKSE::log::warn("impossibile scrivere {}", kIni);
			return;
		}
		f << "; Impostazioni di BladeFireMode (progetto personale). Si modificano meglio dal menu F1.\n";
		f << "; I tasti si scrivono cosi': Pad_LB+Pad_RB+Pad_DpadDown|0.35  (dopo | i secondi di pressione)\n";
		f << "; Pad: Pad_A/B/X/Y, Pad_LB/RB, Pad_LT/RT, Pad_L3/R3, Pad_Start/Back, Pad_DpadUp/Down/Left/Right\n";

		std::string last;
		for (auto& it : Table()) {
			if (last != it.section) {
				f << "\n[" << it.section << "]\n";
				last = it.section;
			}
			f << it.key << " = ";
			switch (it.type) {
			case T::Bool:
				f << (*static_cast<bool*>(it.ptr) ? "1" : "0");
				break;
			case T::Float:
				f << std::format("{:.3f}", *static_cast<float*>(it.ptr));
				break;
			case T::Str:
				f << static_cast<char*>(it.ptr);
				break;
			case T::Bind: {
				std::lock_guard l(gBindMx);
				f << BindingToString(*static_cast<Binding*>(it.ptr));
				break;
			}
			}
			f << "\n";
		}
		gDirty = false;
	}

	void RequestSave() { gDirty = true; }

	bool SaveIfDirty()
	{
		if (gDirty.exchange(false)) {
			SaveSettings();
			return true;
		}
		return false;
	}
}

