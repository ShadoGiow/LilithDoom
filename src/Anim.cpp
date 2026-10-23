#include "pch.h"

#include "Anim.h"
#include "Log.h"

namespace bf::anim
{
	namespace
	{
		constexpr auto kRel = "Data/meshes/actors/character/animations/OpenAnimationReplacer/BladeFireMode";

		// nomi delle clip di combat idle del personaggio (tutte, cosi' funziona qualunque venga scelta dal gioco)
		constexpr std::array kClips{
			"combatidlea.hkx", "combatidleb.hkx", "combatidlec.hkx",
			"combatidlesmalla.hkx", "combatidlesmallb.hkx",
			"combatidle_speciala.hkx", "combatidle_specialb.hkx", "combatidle_specialc.hkx",
			"combatidleweaponchecka.hkx", "combatidleweaponcheckb.hkx",
			"combatidlestretchinga.hkx", "combatidlestretchingb.hkx",
			"combatidlelookinga.hkx", "combatidlelookingb.hkx", "combatidlelookingc.hkx", "combatidlelookingd.hkx",
			"combatidleshieldhit.hkx"
		};

		constexpr auto kModJson =
			R"JSON({
  "name": "BladeFireMode",
  "author": "uso personale",
  "description": "Animazione di accensione della lama (progetto personale)"
})JSON";

		constexpr auto kSubJson =
			R"JSON({
  "name": "Accensione",
  "description": "Sostituisce le clip di combat idle solo mentre il plugin ha acceso la finestra animazione",
  "priority": 2147483600,
  "replaceOnEcho": true,
  "keepRandomResultsOnLoop": true,
  "conditions": [
    {
      "condition": "IsActorBase",
      "requiredVersion": "1.0.0.0",
      "Actor base": { "form": { "pluginName": "Skyrim.esm", "formID": "7" } }
    },
    {
      "condition": "CompareValues",
      "requiredVersion": "1.0.0.0",
      "Value A": { "form": { "pluginName": "BladeFireMode.esp", "formID": "800" } },
      "Comparison": "==",
      "Value B": { "value": 1.0 }
    }
  ]
})JSON";

		void WriteIfMissing(const std::filesystem::path& a_p, const char* a_text)
		{
			if (std::filesystem::exists(a_p)) return;
			std::ofstream f(a_p, std::ios::trunc);
			f << a_text;
		}
	}

	const char* OarRelative() { return kRel; }

	std::filesystem::path TargetDir()
	{
		return std::filesystem::path(kRel) / "Ignite";
	}

	int InstalledCount()
	{
		int  n = 0;
		std::error_code ec;
		for (auto c : kClips) {
			if (std::filesystem::exists(TargetDir() / c, ec)) ++n;
		}
		return n;
	}

	std::string Install(const std::string& a_source)
	{
		namespace fs = std::filesystem;
		std::error_code ec;

		if (a_source.empty()) {
			return "ERRORE: indica il percorso del file .hkx";
		}
		const fs::path src(a_source);
		if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec)) {
			return "ERRORE: file non trovato";
		}
		const auto size = fs::file_size(src, ec);
		if (ec || size < 2048) {
			return "ERRORE: file troppo piccolo per essere un'animazione";
		}

		// firma dei file Havok (packfile): 57 E0 E0 57 10 C0 C0 10
		{
			std::ifstream f(src, std::ios::binary);
			unsigned char m[8]{};
			f.read(reinterpret_cast<char*>(m), 8);
			constexpr unsigned char sig[8]{ 0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10 };
			if (!f || std::memcmp(m, sig, 8) != 0) {
				return "ERRORE: non sembra un file .hkx di Skyrim (firma Havok non trovata)";
			}
		}

		const auto dir = TargetDir();
		fs::create_directories(dir, ec);
		if (ec) {
			return "ERRORE: impossibile creare la cartella di destinazione";
		}
		WriteIfMissing(fs::path(kRel) / "config.json", kModJson);
		WriteIfMissing(dir / "config.json", kSubJson);

		int ok = 0;
		for (auto c : kClips) {
			fs::copy_file(src, dir / c, fs::copy_options::overwrite_existing, ec);
			if (!ec) ++ok;
		}
		Say("animazione installata: {} copie in {}", ok, dir.string());
		if (ok != static_cast<int>(kClips.size())) {
			return std::format("ATTENZIONE: copiati {} file su {}", ok, kClips.size());
		}
		return std::format("OK: animazione installata ({} copie). Riavvia la partita o ricarica il salvataggio.", ok);
	}
}
