#include "pch.h"

#include "Anim.h"
#include "Input.h"
#include "Log.h"
#include "Menu.h"
#include "Mode.h"
#include "OAR.h"
#include "Settings.h"

// BladeFireMode - progetto PERSONALE (mashup per il mio gioco, non una release per altri).

namespace
{
	std::atomic<bool> gTickRun{ false };
	std::atomic<bool> gTickPending{ false };

	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "BladeFireMode.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S] [%l] %v"s);
	}

	// Un thread leggero chiede al gioco di eseguire il Tick ~30 volte al secondo
	// (le chiamate al gioco avvengono sempre sul thread principale).
	void StartTicker()
	{
		if (gTickRun.exchange(true)) {
			return;
		}
		std::thread([] {
			while (gTickRun) {
				std::this_thread::sleep_for(std::chrono::milliseconds(33));
				if (gTickPending.exchange(true)) {
					continue;  // il tick precedente non e' ancora stato eseguito
				}
				if (auto* tasks = SKSE::GetTaskInterface()) {
					tasks->AddTask([] {
						try {
							bf::mode::Tick();
						} catch (const std::exception& e) {
							SKSE::log::error("errore nel tick: {}", e.what());
						} catch (...) {
							SKSE::log::error("errore sconosciuto nel tick");
						}
						gTickPending = false;
					});
				} else {
					gTickPending = false;
				}
			}
		}).detach();
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
			bf::oar::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			try {
				bf::mode::Install();
				bf::menu::Register();
				StartTicker();
			} catch (const std::exception& e) {
				SKSE::log::error("errore in kDataLoaded: {}", e.what());
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			bf::mode::OnPreLoad();
			break;
		case SKSE::MessagingInterface::kNewGame:
		case SKSE::MessagingInterface::kPostLoadGame:
			bf::mode::OnGameLoaded();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();
	bf::Say("BladeFireMode 1.0 in avvio (progetto personale)");

	bf::LoadSettings();
	bf::input::Start();

	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}

