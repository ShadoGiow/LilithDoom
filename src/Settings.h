#pragma once

// Impostazioni del plugin: tutte modificabili dal menu F1 e salvate nel file INI.
// Progetto PERSONALE (mashup per il mio gioco), non una release per altre persone.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace bf
{
	// Un tasto/pulsante e' un numero: 1..24 = pulsanti del pad, 0x100+VK = tastiera, 0x200+n = mouse.
	using InputId = std::uint16_t;

	struct Binding
	{
		std::vector<InputId> keys;  // combinazione: tutti questi tasti devono essere premuti insieme
		float                hold{ 0.0f };  // secondi di pressione richiesti (0 = subito)

		[[nodiscard]] bool empty() const noexcept { return keys.empty(); }
	};

	struct Settings
	{
		// --- spada riconosciuta (pezzo del percorso del modello dell'arma equipaggiata a destra)
		char swordMatch[128]{ "harbinger2h" };

		// --- tasti (modificabili dal menu). Se "spegni" e' vuoto, il tasto "accendi" fa anche da interruttore.
		Binding bindOn;
		Binding bindOff;

		// --- animazione di accensione
		bool  animEnabled{ true };
		char  triggerEvent[96]{ "IdleCombatStart" };  // evento di animazione inviato al personaggio
		bool  fallbackIgnite{ true };                 // se l'animazione non parte accendi comunque
		bool  lockControls{ true };                   // blocca i comandi mentre dura l'animazione
		bool  drawIfSheathed{ true };                 // se l'arma e' rinfoderata, la estrae prima
		float ignitionOffset{ 2.70f };                // secondi dall'inizio della clip al contatto mano-lama
		float rampSeconds{ 2.33f };                   // durata dell'accensione graduale
		float clipLength{ 6.50f };                    // durata totale della clip (rilascio comandi)
		float startTimeout{ 1.50f };                  // attesa massima per vedere partire la clip
		char  anchorTag[64]{ "soundplay.paladintransform" };  // annotazione della clip usata per sincronizzare
		float anchorTime{ 0.50f };                    // secondi (dall'inizio della clip) a cui compare quell'annotazione
		char  animSource[260]{ "" };                  // ultimo file .hkx scelto nel menu

		// --- danni di fuoco
		bool  fireDamage{ true };
		float fireMult{ 1.5f };       // danno di fuoco extra = danno base dell'arma * questo valore
		float fireFlat{ 20.0f };      // + questo valore fisso per colpo
		float powerMult{ 1.5f };      // moltiplicatore dell'extra sugli attacchi potenti
		bool  respectResist{ true };  // tiene conto della resistenza al fuoco del bersaglio
		bool  burn{ true };           // bruciatura nel tempo dopo il colpo
		float burnDps{ 8.0f };
		float burnSeconds{ 4.0f };

		// --- mana
		float manaStart{ 40.0f };   // costo unico all'accensione
		float manaDrain{ 6.0f };    // consumo al secondo mentre la modalita' e' accesa
		float manaMin{ 5.0f };      // sotto questo valore la modalita' si spegne
		bool  drainOnlyInCombat{ false };

		// --- comportamento
		bool offOnUnequip{ true };
		bool offOnSheathe{ false };

		// --- diagnostica
		bool logEvents{ true };
	};

	extern Settings   S;
	extern std::mutex gBindMx;  // protegge bindOn / bindOff (modificati dal thread dei tasti)

	void LoadSettings();
	void SaveSettings();
	void RequestSave();     // salva a breve (dal thread principale)
	bool SaveIfDirty();     // chiamata periodica

	std::string BindingToString(const Binding& a_b);
	bool        BindingFromString(const std::string& a_s, Binding& a_out);
	std::string BindingLabel(const Binding& a_b);  // testo leggibile per il menu

	// nomi dei tasti (implementati nel file che usa le API di Windows)
	std::string InputName(InputId a_id);
	bool        InputFromName(const std::string& a_name, InputId& a_out);
}
