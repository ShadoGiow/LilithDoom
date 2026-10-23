#include "pch.h"

#include "Anim.h"
#include "Log.h"

namespace bf::anim
{
	namespace
	{
		constexpr auto kRel = "Data/meshes/actors/character/animations/BladeFireMode";
		constexpr auto kClip = "Ignite.hkx";
	}

	const char* OarRelative() { return kRel; }

	std::filesystem::path TargetDir()
	{
		return std::filesystem::path(kRel);
	}

	int InstalledCount()
	{
		std::error_code ec;
		return std::filesystem::exists(TargetDir() / kClip, ec) ? 1 : 0;
	}

	std::string Install(const std::string& a_source)
	{
		namespace fs = std::filesystem;
		std::error_code ec;

		if (a_source.empty()) return "ERRORE: indica il percorso del file .hkx";
		const fs::path src(a_source);
		if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec)) return "ERRORE: file non trovato";
		const auto size = fs::file_size(src, ec);
		if (ec || size < 2048) return "ERRORE: file troppo piccolo per essere un'animazione";

		// Firma dei file Havok (packfile) usati da Skyrim.
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
		if (ec) return "ERRORE: impossibile creare la cartella di destinazione";
		fs::copy_file(src, dir / kClip, fs::copy_options::overwrite_existing, ec);
		if (ec) return "ERRORE: impossibile copiare la clip standalone";
		Say("animazione standalone installata in {}", (dir / kClip).string());
		return "OK: installata come Ignite.hkx. Riavvia il gioco prima di provarla.";
	}
}