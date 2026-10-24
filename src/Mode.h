#pragma once

// Modalita' "lama in fiamme": macchina a stati, mana, danni di fuoco, blocco comandi.

#include <string>

namespace bf::mode
{
	enum class St { Off, Drawing, Requested, Playing, Igniting, On, Extinguishing };

	struct Snapshot
	{
		std::string state{ "spenta" };
		float       stateTime{ 0.0f };
		float       clipTime{ -1.0f };   // secondi dall'inizio dell'animazione (se agganciata)
		bool        anchored{ false };
		bool        swordOk{ false };
		std::string weapon;
		std::string model;
		float       mana{ 0.0f };
		float       manaMax{ 0.0f };
		std::string fx;
		bool        espOk{ false };
		long long   hits{ 0 };
		float       lastExtra{ 0.0f };
		int         burning{ 0 };
	};

	void Install();         // collega gli ascoltatori (dopo il caricamento dei dati)
	void OnPreLoad();
	void OnGameLoaded();
	void Tick();            // thread di gioco, ~30 volte al secondo

	void RequestOn();       // da qualunque thread
	void RequestOff();
	void RequestToggle();
	void RequestEvent(const std::string& a_event);  // prova manuale di un evento d'animazione
	void RequestSoundTest(const std::string& a_name);  // prova di un suono (editor ID)

	Snapshot GetSnapshot();
	bool     IsActive();    // acceso o in accensione
	bool     IsOARIgniting();  // condizione OAR esclusiva dell'apertura Ignite
}

