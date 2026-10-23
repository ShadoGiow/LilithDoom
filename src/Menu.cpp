#include "pch.h"

#include "Anim.h"
#include "Input.h"
#include "Log.h"
#include "Menu.h"
#include "Mode.h"
#include "Settings.h"

// Il menu usa le funzioni cimgui esportate da SKSEMenuFramework.dll, collegate a runtime
// (nessuna dipendenza in fase di compilazione). Testi senza accenti per sicurezza sul font.

namespace bf::menu
{
	namespace
	{
		struct V2 { float x, y; };
		struct V4 { float x, y, z, w; };

		template <class F>
		struct Fn
		{
			F fn{ nullptr };
			bool Bind(REX::W32::HMODULE a_m, const char* a_name)
			{
				fn = reinterpret_cast<F>(REX::W32::GetProcAddress(a_m, a_name));
				if (!fn) {
					Say("menu: funzione mancante nel framework: {}", a_name);
				}
				return fn != nullptr;
			}
		};

		// varianti "V" (va_list), come usa l'header ufficiale del framework
		Fn<void (*)(const char*, va_list)>                     igTextV, igTextWrappedV, igTextDisabledV, igSetTooltipV;
		Fn<void (*)(V4, const char*, va_list)>                 igTextColoredV;
		Fn<void (*)()>                                         igSeparator, igSpacing, igPopID, igEndDisabled, igNewLine;
		Fn<void (*)(const char*)>                              igSeparatorText, igPushID_Str;
		Fn<void (*)(float, float)>                             igSameLine;
		Fn<bool (*)(const char*, V2)>                          igButton;
		Fn<bool (*)(const char*, bool*)>                       igCheckbox;
		Fn<bool (*)(const char*, float*, float, float, const char*, int)> igSliderFloat;
		Fn<bool (*)(const char*, char*, std::size_t, int, void*, void*)> igInputText;
		Fn<bool (*)(const char*, int)>                         igCollapsingHeader;
		Fn<void (*)(bool)>                                     igBeginDisabled;
		Fn<void (*)(float)>                                    igSetNextItemWidth;
		Fn<bool (*)(int)>                                      igIsItemHovered;
		Fn<void (*)(float, V2, const char*)>                   igProgressBar;

		bool gBound{ false };

		constexpr int kSliderClamp = 1 << 4;
		constexpr int kOpen = 1 << 5;

		constexpr V4 kGreen{ 0.35f, 0.95f, 0.45f, 1.0f };
		constexpr V4 kRed{ 1.0f, 0.4f, 0.3f, 1.0f };
		constexpr V4 kOrange{ 1.0f, 0.65f, 0.2f, 1.0f };
		constexpr V4 kGrey{ 0.7f, 0.7f, 0.7f, 1.0f };

		bool BindAll()
		{
			const auto m = REX::W32::GetModuleHandleA("SKSEMenuFramework.dll");
			if (!m) {
				Say("menu: SKSE Menu Framework non trovato (il plugin funziona lo stesso, con INI)");
				return false;
			}
			bool ok = true;
			ok &= igTextV.Bind(m, "igTextV");
			ok &= igTextWrappedV.Bind(m, "igTextWrappedV");
			ok &= igTextDisabledV.Bind(m, "igTextDisabledV");
			ok &= igTextColoredV.Bind(m, "igTextColoredV");
			ok &= igSeparator.Bind(m, "igSeparator");
			ok &= igSpacing.Bind(m, "igSpacing");
			ok &= igNewLine.Bind(m, "igNewLine");
			ok &= igPopID.Bind(m, "igPopID");
			ok &= igEndDisabled.Bind(m, "igEndDisabled");
			ok &= igSeparatorText.Bind(m, "igSeparatorText");
			ok &= igPushID_Str.Bind(m, "igPushID_Str");
			ok &= igSameLine.Bind(m, "igSameLine");
			ok &= igButton.Bind(m, "igButton");
			ok &= igCheckbox.Bind(m, "igCheckbox");
			ok &= igSliderFloat.Bind(m, "igSliderFloat");
			ok &= igInputText.Bind(m, "igInputText");
			ok &= igCollapsingHeader.Bind(m, "igCollapsingHeader_TreeNodeFlags");
			ok &= igBeginDisabled.Bind(m, "igBeginDisabled");
			ok &= igSetNextItemWidth.Bind(m, "igSetNextItemWidth");
			ok &= igIsItemHovered.Bind(m, "igIsItemHovered");
			ok &= igSetTooltipV.Bind(m, "igSetTooltipV");
			ok &= igProgressBar.Bind(m, "igProgressBar");
			return ok;
		}

		void TextF(const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igTextV.fn(a_fmt, a);
			va_end(a);
		}
		void TextWrappedF(const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igTextWrappedV.fn(a_fmt, a);
			va_end(a);
		}
		void TextDisabledF(const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igTextDisabledV.fn(a_fmt, a);
			va_end(a);
		}
		void TooltipF(const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igSetTooltipV.fn(a_fmt, a);
			va_end(a);
		}
		void TextColoredF(V4 a_col, const char* a_fmt, ...)
		{
			va_list a;
			va_start(a, a_fmt);
			igTextColoredV.fn(a_col, a_fmt, a);
			va_end(a);
		}

		// ----------------------------------------------------------- helper di interfaccia
		void Tip(const char* a_text)
		{
			if (igIsItemHovered.fn(0)) {
				TooltipF("%s", a_text);
			}
		}

		bool Check(const char* a_label, bool& a_v, const char* a_tip = nullptr)
		{
			const bool ch = igCheckbox.fn(a_label, &a_v);
			if (a_tip) Tip(a_tip);
			if (ch) RequestSave();
			return ch;
		}

		bool Slider(const char* a_label, float& a_v, float a_min, float a_max, const char* a_fmt, const char* a_tip = nullptr)
		{
			igSetNextItemWidth.fn(320.0f);
			const bool ch = igSliderFloat.fn(a_label, &a_v, a_min, a_max, a_fmt, kSliderClamp);
			if (a_tip) Tip(a_tip);
			if (ch) RequestSave();
			return ch;
		}

		bool Text(const char* a_label, char* a_buf, std::size_t a_size, const char* a_tip = nullptr)
		{
			igSetNextItemWidth.fn(420.0f);
			const bool ch = igInputText.fn(a_label, a_buf, a_size, 0, nullptr, nullptr);
			if (a_tip) Tip(a_tip);
			if (ch) RequestSave();
			return ch;
		}

		bool Button(const char* a_label) { return igButton.fn(a_label, V2{ 0.0f, 0.0f }); }

		void BindingRow(const char* a_title, Binding& a_b, input::Target a_target, const char* a_id)
		{
			igPushID_Str.fn(a_id);
			TextF("%s", a_title);

			std::string label;
			{
				std::lock_guard l(gBindMx);
				label = BindingLabel(a_b);
			}
			const bool capturing = input::CaptureTarget() == a_target;
			if (capturing) {
				TextColoredF(kOrange, "In attesa... premi la combinazione (pad o tastiera) e rilasciala. Esc = annulla");
				const auto live = input::CaptureLive();
				TextF("  adesso: %s", live.empty() ? "(niente)" : live.c_str());
				if (Button("Annulla")) input::CancelCapture();
			} else {
				TextF("  attuale: %s", label.c_str());
				if (Button("Registra nuova combinazione")) input::BeginCapture(a_target);
				igSameLine.fn(0.0f, 8.0f);
				if (Button("Cancella")) {
					std::lock_guard l(gBindMx);
					a_b.keys.clear();
					RequestSave();
				}
			}

			float hold;
			{
				std::lock_guard l(gBindMx);
				hold = a_b.hold;
			}
			if (Slider("Tempo di pressione (s)##hold", hold, 0.0f, 2.0f, "%.2f", "Per quanti secondi vanno tenuti premuti i tasti. 0 = subito")) {
				std::lock_guard l(gBindMx);
				a_b.hold = hold;
			}
			igPopID.fn();
			igSpacing.fn();
		}

		// ----------------------------------------------------------- pagine
		void __stdcall PageState()
		{
			const auto s = mode::GetSnapshot();

			igSeparatorText.fn("Stato");
			TextF("Modalita: %s  (da %.1f s)", s.state.c_str(), s.stateTime);
			if (s.clipTime >= 0.0f) {
				TextF("Animazione: %.2f s dall'inizio%s", s.clipTime, s.anchored ? " (agganciata)" : " (stimata)");
			}

			if (s.swordOk) {
				TextColoredF(kGreen, "Spada riconosciuta: %s", s.weapon.c_str());
			} else {
				TextColoredF(kOrange, "Spada NON riconosciuta (mano destra: %s)", s.weapon.empty() ? "nessuna arma" : s.weapon.c_str());
			}
			if (!s.model.empty()) TextDisabledF("modello: %s", s.model.c_str());
			TextF("Effetti sulla lama: %s", s.fx.c_str());
			if (s.espOk) {
				TextColoredF(kGreen, "BladeFireMode.esp: caricato");
			} else {
				TextColoredF(kRed, "BladeFireMode.esp: NON trovato (l'animazione non puo' essere sostituita)");
			}
			if (s.manaMax > 0.0f) {
				igProgressBar.fn(std::clamp(s.mana / s.manaMax, 0.0f, 1.0f), V2{ 320.0f, 0.0f }, nullptr);
				igSameLine.fn(0.0f, 8.0f);
				TextF("Mana %.0f / %.0f", s.mana, s.manaMax);
			}
			TextF("Colpi con fuoco extra: %lld   ultimo extra: %.1f   in bruciatura: %d", s.hits, s.lastExtra, s.burning);

			igSpacing.fn();
			if (Button(mode::IsActive() ? "Spegni adesso" : "Accendi adesso")) {
				mode::RequestToggle();
			}
			igSpacing.fn();

			igSeparatorText.fn("Tasti");
			TextDisabledF(input::PadConnected() ? "Pad rilevato" : "Pad non rilevato (solo tastiera)");
			BindingRow("Accendi (se non c'e' 'Spegni' fa anche da interruttore)", S.bindOn, input::Target::On, "bindon");
			BindingRow("Spegni (facoltativo)", S.bindOff, input::Target::Off, "bindoff");

			igSeparatorText.fn("Spada");
			Text("Pezzo del percorso del modello##match", S.swordMatch, sizeof(S.swordMatch),
				"La modalita' funziona solo se il modello dell'arma in mano destra contiene questo testo");
		}

		void __stdcall PageAnim()
		{
			igSeparatorText.fn("Animazione di accensione");
			Check("Usa l'animazione##animon", S.animEnabled, "Se spento la lama si accende subito, senza animazione");
			Text("Evento di avvio##trigger", S.triggerEvent, sizeof(S.triggerEvent),
				"Evento Nemesis dedicato alla singola clip. Valore predefinito: BFM_Ignite");
			if (S.badEvents[0]) {
				TextColoredF(kOrange, "Eventi scartati (hanno fatto rinfoderare l'arma): %s", S.badEvents);
				if (Button("Azzera elenco scartati")) { S.badEvents[0] = 0; RequestSave(); }
			}
			Check("Accendi comunque se l'animazione non parte##fallback", S.fallbackIgnite);
			Check("Blocca movimento e salto durante l'animazione##lock", S.lockControls,
				"Evita di interrompere la clip camminando. Se l'arma viene rinfoderata mentre e' attivo lo spengo da solo");
			Check("Blocca anche attacchi e guardia (sconsigliato)##lockatk", S.lockAttack,
				"Disattivare i comandi di combattimento fa RINFODERARE l'arma e interrompe la clip: lascia spento");
			Check("Estrai l'arma se e' rinfoderata##draw", S.drawIfSheathed);

			igSeparatorText.fn("Tempi (secondi)");
			Slider("Contatto mano-lama##offset", S.ignitionOffset, 0.0f, 8.0f, "%.2f",
				"Istante della clip in cui la mano sinistra tocca la lama: da qui parte l'accensione");
			Slider("Durata accensione graduale##ramp", S.rampSeconds, 0.5f, 6.0f, "%.2f",
				"Quanto dura la crescita del fuoco (l'effetto originale dura 2.33 s)");
			Slider("Durata totale della clip##cliplen", S.clipLength, 1.0f, 15.0f, "%.2f", "Dopo questo tempo i comandi tornano liberi");
			Slider("Attesa massima partenza##startto", S.startTimeout, 0.2f, 5.0f, "%.2f", "Se non arriva nessuna annotazione stimo l'inizio dopo questo tempo");

			igSeparatorText.fn("Sincronia con la clip");
			TextWrappedF("Le annotazioni SoundPlay.* non arrivano ai plugin: mi aggancio a un evento 'Pie' della clip (si vede nel registro con il suo contenuto).");
			Text("Evento di riferimento##anchor", S.anchorTag, sizeof(S.anchorTag),
				"Nome dell'evento della clip (nel registro compare come 'evento: Pie [contenuto]')");
			Text("Il contenuto deve contenere##anchorp", S.anchorPayload, sizeof(S.anchorPayload),
				"Parte del testo dell'evento (es. @APPLYSPELL). Vuoto = il primo evento di quel nome");
			Slider("Secondo della clip in cui compare##anchort", S.anchorTime, 0.0f, 10.0f, "%.2f", "A che secondo della clip compare quell'evento (nella clip fornita: 0.20)");

			igSeparatorText.fn("Suoni");
			Check("Suoni attivi##sndon", S.soundEnabled);
			Text("All'accensione (editor ID)##sndign", S.soundIgnite, sizeof(S.soundIgnite),
				"Nome (editor ID) di un suono di Skyrim. Come default: quello della mod originale (arma evocata). Vuoto = nessuno");
			igSameLine.fn(0.0f, 8.0f);
			if (Button("Prova##t1")) mode::RequestSoundTest(S.soundIgnite);
			Text("Allo spegnimento (editor ID)##sndoff", S.soundOff, sizeof(S.soundOff));
			igSameLine.fn(0.0f, 8.0f);
			if (Button("Prova##t2")) mode::RequestSoundTest(S.soundOff);

			igSeparatorText.fn("Installa un'altra animazione");
			TextWrappedF("Scrivi il percorso del file .hkx e premi Installa: viene sostituita solo la clip standalone Ignite.hkx.");
			Text("File .hkx##animsrc", S.animSource, sizeof(S.animSource));
			static std::string result;
			if (Button("Installa")) {
				result = anim::Install(S.animSource);
			}
			if (!result.empty()) {
				TextColoredF(result.rfind("OK", 0) == 0 ? kGreen : kOrange, "%s", result.c_str());
			}
			TextDisabledF("Clip: %s/Ignite.hkx  (presente: %s)", anim::OarRelative(), anim::InstalledCount() ? "si" : "no");

			igSeparatorText.fn("Prova eventi");
			TextWrappedF("Testa l'evento registrato nella patch Nemesis. Se viene rifiutato, controlla che la patch sia selezionata e che Nemesis abbia generato i behavior.");
			if (Button("Test BFM_Ignite")) mode::RequestEvent("BFM_Ignite");
		}

		void __stdcall PageDamage()
		{
			igSeparatorText.fn("Danni di fuoco extra");
			Check("Danni di fuoco extra attivi##fire", S.fireDamage);
			Slider("Moltiplicatore sul danno dell'arma##mult", S.fireMult, 0.0f, 20.0f, "%.2f",
				"Extra = danno base della spada x questo valore + valore fisso");
			Slider("Valore fisso per colpo##flat", S.fireFlat, 0.0f, 500.0f, "%.0f");
			Slider("Moltiplicatore attacchi potenti##power", S.powerMult, 0.0f, 5.0f, "%.2f");
			Check("Tieni conto della resistenza al fuoco del bersaglio##resist", S.respectResist);

			igSeparatorText.fn("Bruciatura nel tempo");
			Check("Bruciatura attiva##burn", S.burn);
			Slider("Danno al secondo##dps", S.burnDps, 0.0f, 200.0f, "%.1f");
			Slider("Durata (s)##burnsec", S.burnSeconds, 0.0f, 20.0f, "%.1f");

			igSeparatorText.fn("Mana");
			Slider("Costo all'accensione##mstart", S.manaStart, 0.0f, 300.0f, "%.0f");
			Slider("Consumo al secondo a lama accesa##mdrain", S.manaDrain, 0.0f, 60.0f, "%.1f",
				"Consumo lordo: la rigenerazione normale del mana lo riduce");
			Slider("Sotto questo mana si spegne##mmin", S.manaMin, 0.0f, 100.0f, "%.0f");
			Check("Consuma solo in combattimento##mcombat", S.drainOnlyInCombat);

			igSeparatorText.fn("Comportamento");
			Check("Spegni se cambio arma##unequip", S.offOnUnequip);
			Check("Spegni se rinfoderi##sheathe", S.offOnSheathe);
		}

		void __stdcall PageDiag()
		{
			igSeparatorText.fn("Registro");
			Check("Scrivi gli eventi di animazione nel registro##logev", S.logEvents,
				"Utile per capire quali annotazioni arrivano durante l'animazione");
			if (Button("Svuota registro")) LogClear();
			igSameLine.fn(0.0f, 8.0f);
			if (Button("Salva impostazioni ora")) SaveSettings();
			igSpacing.fn();
			for (auto& l : LogSnapshot()) {
				TextWrappedF("%s", l.c_str());
			}
			igSpacing.fn();
			TextDisabledF("File completo: Documents/My Games/Skyrim Special Edition/SKSE/BladeFireMode.log");
		}
	}

	bool Register()
	{
		if (gBound) return true;
		if (!BindAll()) {
			return false;
		}
		const auto add = reinterpret_cast<void (*)(const char*, void (*)())>(
			REX::W32::GetProcAddress(REX::W32::GetModuleHandleA("SKSEMenuFramework.dll"), "AddSectionItem"));
		if (!add) {
			Say("menu: AddSectionItem non trovato");
			return false;
		}
		add("Fiamma Lama/Stato e Tasti", PageState);
		add("Fiamma Lama/Animazione", PageAnim);
		add("Fiamma Lama/Danni e Mana", PageDamage);
		add("Fiamma Lama/Diagnostica", PageDiag);
		gBound = true;
		Say("menu F1: pagine registrate");
		return true;
	}
}
