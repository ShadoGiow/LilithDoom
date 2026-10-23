#pragma once

#include <filesystem>
#include <string>

// Installazione dell'animazione di accensione: copia il file .hkx scelto nella cartella
// di Open Animation Replacer sotto tutti i nomi delle clip di "combat idle".

namespace bf::anim
{
	std::filesystem::path TargetDir();
	std::string           Install(const std::string& a_source);  // messaggio per il menu
	int                   InstalledCount();
	const char*           OarRelative();
}
