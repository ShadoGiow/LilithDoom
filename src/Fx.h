#pragma once

// Effetto grafico sulla spada: nodo "BoundAxeEnchEffects" + sequenze mBegin / mLoop / mEnd
// (NiControllerManager dentro la mesh). Il plugin le avvia direttamente.

namespace bf::fx
{
	enum class Phase { Hidden, Begin, Loop, End };

	// cerca sul personaggio (terza e prima persona) il gruppo effetti della spada
	bool Resolve(RE::PlayerCharacter* a_pc);
	bool Present();        // il gruppo esiste ancora ed e' agganciato
	bool InstanceChanged();  // il modello e' stato ricreato dall'ultima chiamata (ripristinare lo stato)

	void Forget();

	void Begin(float a_rampSeconds);  // mostra e avvia l'accensione graduale
	void Loop();                      // fiamma continua (con dissolvenza dalla fase Begin)
	void End();                       // spegnimento (mEnd)
	void Hide();                      // spento e nascosto
	Phase Current();

	// diagnostica per il menu
	int         Targets();
	std::string Describe();
}
