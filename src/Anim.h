#pragma once

#include <filesystem>
#include <string>

// Installazione della singola clip di accensione usata dallo stato dedicato Nemesis.

namespace bf::anim
{
	std::filesystem::path TargetDir();
	std::string           Install(const std::string& a_source);  // messaggio per il menu
	int                   InstalledCount();
	const char*           OarRelative();
}
