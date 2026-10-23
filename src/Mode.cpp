#include "pch.h"

#include "Mode.h"
#include "Fx.h"
#include "Input.h"
#include "Log.h"
#include "Settings.h"

namespace bf::mode
{
	namespace
	{
		using clk = std::chrono::steady_clock;

		constexpr auto kEsp = "BladeFireMode.esp";
		constexpr std::uint32_t kGlobalAnim = 0x800;  // 1 = sostituisci l'idle di combattimento con la mia animazione
		constexpr std::uint32_t kGlobalOn = 0x801;    // 1 = modalita' accesa

		// clip e tempi noti dell'animazione fornita (annotazioni -> secondi dall'inizio)
		struct Anchor { const char* tag; float t; };

		// ---------------------------------------------------------------- stato
		std::atomic<int> gSt{ static_cast<int>(St::Off) };  // letto anche da altri thread (colpi)
		St   St_() { return static_cast<St>(gSt.load()); }
		float gStateTime{ 0.0f };

		std::atomic<double>       gClock{ 0.0 };        // tempo di gioco (fermo in pausa)
		std::atomic<std::int64_t> gLastTickNs{ 0 };
		std::atomic<bool>         gPaused{ false };

		std::atomic<double> gReqClock{ 0.0 };
		double gT0{ 0.0 };            // istante (tempo di gioco) in cui la clip e' partita
		bool   gHasT0{ false };

		std::atomic<bool>   gSheathEv{ false };    // l'animazione ha segnalato che l'arma viene rinfoderata
		std::atomic<bool>   gIdleStopEv{ false };  // la clip si e' fermata (IdleStop)
		std::uint32_t       gLockFlags{ 0 };
		double              gNotDrawnSince{ -1.0 };
		float               gDiagTimer{ 0.0f };

		std::atomic<bool>   gAnchored{ false };
		std::atomic<double> gAnchorT0{ 0.0 };

		std::atomic<int> gReqOn{ 0 }, gReqOff{ 0 };
		std::mutex       gEvMx;
		std::string      gReqEvent;
		std::string      gReqSound;

		bool   gLocked{ false };
		double gLockUntil{ 0.0 };
		bool   gAnimWindow{ false };
		double gAnimWindowUntil{ 0.0 };
		double gDrawStart{ 0.0 };
		bool   gDrawn{ false };
		double gDrawnAt{ 0.0 };

		float gFxCheck{ 0.0f };
		float gSnapTimer{ 0.0f };
		float gSaveTimer{ 0.0f };
		bool  gMsgLowMana{ false };

		std::atomic<int> gLogBudget{ 0 };

		std::mutex gSnapMx;
		Snapshot   gSnap;

		// ---------------------------------------------------------------- colpi
		std::atomic<long long> gHits{ 0 };
		std::atomic<float>     gLastExtra{ 0.0f };

		struct Pending { RE::ActorHandle h; float dmg; };
		std::mutex           gPendMx;
		std::vector<Pending> gPend;

		struct Burn { RE::ActorHandle h; float left; float acc; };
		std::vector<Burn> gBurn;  // solo thread di gioco

		bool gUseRestore{ false };
		int  gNoEffect{ 0 };

		// ---------------------------------------------------------------- utilita'
		std::string Lower(std::string s)
		{
			for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			return s;
		}

		double ClockNow()
		{
			double c = gClock.load();
			if (!gPaused.load()) {
				const auto now = clk::now().time_since_epoch().count();
				const auto last = gLastTickNs.load();
				if (last > 0) {
					c += std::min(0.25, static_cast<double>(now - last) * 1e-9);
				}
			}
			return c;
		}

		void Hud(const std::string& a_msg)
		{
			Say("{}", a_msg);
			RE::SendHUDMessage::ShowHUDMessage(a_msg.c_str());
		}

		RE::TESGlobal* Glob(std::uint32_t a_id)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			return dh ? dh->LookupForm<RE::TESGlobal>(a_id, kEsp) : nullptr;
		}

		void SetGlobal(std::uint32_t a_id, float a_v)
		{
			if (auto* g = Glob(a_id)) {
				g->value = a_v;
			}
		}

		float Mana(RE::PlayerCharacter* a_pc)
		{
			return a_pc->AsActorValueOwner()->GetActorValue(RE::ActorValue::kMagicka);
		}

		void DrainMana(RE::PlayerCharacter* a_pc, float a_x)
		{
			if (a_x <= 0.0f) return;
			a_pc->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, -a_x);
		}

		void Enter(St a_s)
		{
			gSt = static_cast<int>(a_s);
			gStateTime = 0.0f;
		}

		const char* Name(St a_s)
		{
			switch (a_s) {
			case St::Off: return "spenta";
			case St::Drawing: return "estrazione arma";
			case St::Requested: return "avvio animazione";
			case St::Playing: return "animazione in corso";
			case St::Igniting: return "accensione graduale";
			case St::On: return "ACCESA";
			case St::Extinguishing: return "spegnimento";
			}
			return "?";
		}

		RE::TESObjectWEAP* EquippedSword(RE::PlayerCharacter* a_pc)
		{
			if (!a_pc) return nullptr;
			auto* o = a_pc->GetEquippedObject(false);
			auto* w = o ? o->As<RE::TESObjectWEAP>() : nullptr;
			if (!w) return nullptr;
			const auto match = Lower(S.swordMatch);
			if (match.empty()) return w;
			const char* m = w->GetModel();
			if (!m) return nullptr;
			return Lower(m).find(match) != std::string::npos ? w : nullptr;
		}

		// ---------------------------------------------------------------- suoni
		// Suona un suono di Skyrim dal suo "editor ID" (lo stesso meccanismo delle annotazioni SoundPlay.* delle animazioni).
		bool PlayByName(const char* a_name, bool a_log = true)
		{
			if (!a_name || !a_name[0]) return false;
			auto* am = RE::BSAudioManager::GetSingleton();
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!am || !pc) return false;
			RE::BSSoundHandle h;
			am->GetSoundHandleByName(h, a_name, 0x1A);
			if (!h.IsValid()) {
				if (a_log) Say("suono '{}': non trovato", a_name);
				return false;
			}
			h.SetPosition(pc->GetPosition());
			if (auto* n = pc->Get3D()) h.SetObjectToFollow(n);
			const bool ok = h.Play();
			if (a_log) Say("suono '{}': {}", a_name, ok ? "avviato" : "non avviato");
			return ok;
		}

		void PlayIgnite() { if (S.soundEnabled) PlayByName(S.soundIgnite); }
		void PlayOff() { if (S.soundEnabled) PlayByName(S.soundOff); }

		// ---- eventi scartati (hanno fatto rinfoderare l'arma): lista separata da virgole, salvata nel file INI
		std::string gLastEvent;

		std::vector<std::string> BadList()
		{
			std::vector<std::string> v;
			std::string              cur;
			for (const char* c = S.badEvents; *c; ++c) {
				if (*c == ',') { if (!cur.empty()) v.push_back(cur); cur.clear(); }
				else cur.push_back(*c);
			}
			if (!cur.empty()) v.push_back(cur);
			return v;
		}
		bool IsBad(const std::string& a_e)
		{
			for (auto& b : BadList()) if (Lower(b) == Lower(a_e)) return true;
			return false;
		}
		void MarkBad(const std::string& a_e)
		{
			if (a_e.empty() || IsBad(a_e)) return;
			std::string t = S.badEvents;
			if (!t.empty()) t += ",";
			t += a_e;
			if (t.size() < sizeof(S.badEvents)) {
				std::memcpy(S.badEvents, t.c_str(), t.size() + 1);
				RequestSave();
			}
		}

		bool IsDrawn(RE::PlayerCharacter* a_pc)
		{
			auto* st = a_pc ? a_pc->AsActorState() : nullptr;
			return st && st->IsWeaponDrawn();
		}

		// ---------------------------------------------------------------- comandi
		// Di proposito NON blocco mai attacchi/guardia/arma a meno che lo chieda l'utente: disattivare i comandi di combattimento
		// fa rinfoderare l'arma e interrompe la clip (e' successo nella prova).
		void Lock(bool a_on)
		{
			auto* cm = RE::ControlMap::GetSingleton();
			if (!cm) return;
			using F = RE::UserEvents::USER_EVENT_FLAG;
			if (a_on && !gLocked) {
				std::uint32_t f = 0;
				if (S.lockControls) {
					f |= static_cast<std::uint32_t>(F::kMovement) | static_cast<std::uint32_t>(F::kSneaking) |
					     static_cast<std::uint32_t>(F::kJumping) | static_cast<std::uint32_t>(F::kActivate);
				}
				if (S.lockAttack) {
					f |= static_cast<std::uint32_t>(F::kFighting) | static_cast<std::uint32_t>(F::kMainFour);
				}
				if (f == 0) return;
				gLockFlags = f;
				cm->ToggleControls(static_cast<F>(f), false, false);
				gLocked = true;
				Say("comandi bloccati (maschera {:#x})", f);
			} else if (!a_on && gLocked) {
				cm->ToggleControls(static_cast<F>(gLockFlags), true, false);
				gLocked = false;
				Say("comandi sbloccati");
			}
		}

		// ---------------------------------------------------------------- animazione
		class AnimSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_ev, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
			{
				if (!a_ev || !a_ev->holder || !a_ev->holder->IsPlayerRef()) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const auto st = St_();
				if (st != St::Requested && st != St::Playing && st != St::Drawing && !gAnimWindow) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const std::string tag = a_ev->tag.c_str() ? a_ev->tag.c_str() : "";
				const std::string payload = a_ev->payload.c_str() ? a_ev->payload.c_str() : "";
				const double      now = ClockNow();
				if (S.logEvents && gLogBudget.fetch_sub(1) > 0) {
					Say("+{:.2f}s evento: {}{}", now - gReqClock.load(), tag, payload.empty() ? std::string() : " [" + payload.substr(0, 90) + "]");
				}
				const auto lt = Lower(tag);
				if (!tag.empty() && lt == Lower(S.anchorTag) && !gAnchored.load()) {
					const std::string want = Lower(S.anchorPayload);
					if (want.empty() || Lower(payload).find(want) != std::string::npos) {
						gAnchorT0 = now - static_cast<double>(S.anchorTime);
						gAnchored = true;
					}
				}
				// l'arma sta venendo rinfoderata / la clip e' stata interrotta: lo gestisce il tick (solo dopo l'avvio della richiesta)
				if (st == St::Requested || st == St::Playing) {
					if (now - gReqClock.load() > 0.1) {
						if (lt == "beginweaponsheathe" || lt == "weaponsheathe" || lt == "unequip") gSheathEv = true;
						else if (lt == "idlestop" && now - gReqClock.load() > 0.5) gIdleStopEv = true;
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		AnimSink gAnimSink;

		void AttachSink(RE::PlayerCharacter* a_pc)
		{
			a_pc->AddAnimationGraphEventSink(&gAnimSink);
		}

		// ---------------------------------------------------------------- danni
		class HitSink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_ev, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				using R = RE::BSEventNotifyControl;
				const auto st = St_();
				if (!S.fireDamage || (st != St::On && st != St::Igniting)) return R::kContinue;
				if (!a_ev || !a_ev->cause || !a_ev->target) return R::kContinue;
				if (!a_ev->cause->IsPlayerRef()) return R::kContinue;
				if (a_ev->projectile != 0) return R::kContinue;
				if (a_ev->flags.any(RE::TESHitEvent::Flag::kBashAttack, RE::TESHitEvent::Flag::kHitBlocked)) return R::kContinue;

				auto* pc = RE::PlayerCharacter::GetSingleton();
				auto* w = EquippedSword(pc);
				if (!w || w->GetFormID() != a_ev->source) return R::kContinue;

				auto* tgt = a_ev->target->As<RE::Actor>();
				if (!tgt || tgt->IsDead() || tgt == pc) return R::kContinue;

				// quando la lama non e' ancora del tutto accesa il danno cresce con l'accensione
				float k = 1.0f;
				if (st == St::Igniting) {
					k = std::clamp(gStateTime / std::max(0.3f, S.rampSeconds), 0.0f, 1.0f);
				}

				float extra = static_cast<float>(w->GetAttackDamage()) * S.fireMult + S.fireFlat;
				if (a_ev->flags.any(RE::TESHitEvent::Flag::kPowerAttack)) {
					extra *= S.powerMult;
				}
				extra *= k;

				float resist = 0.0f;
				if (S.respectResist) {
					resist = std::clamp(tgt->AsActorValueOwner()->GetActorValue(RE::ActorValue::kResistFire), 0.0f, 100.0f) / 100.0f;
					extra *= (1.0f - resist);
				}

				++gHits;
				gLastExtra = extra;

				{
					std::lock_guard l(gPendMx);
					if (gPend.size() < 64) {
						gPend.push_back({ tgt->GetHandle(), extra });
					}
				}
				if (S.burn && S.burnDps > 0.0f && S.burnSeconds > 0.0f && resist < 0.999f) {
					std::lock_guard l(gPendMx);
					gPend.push_back({ tgt->GetHandle(), -1.0f });  // segnale: avvia/rinnova bruciatura
				}
				return R::kContinue;
			}
		};
		HitSink gHitSink;

		void ApplyDamage(RE::PlayerCharacter* a_pc, RE::Actor* a_t, float a_dmg)
		{
			if (!a_t || a_t->IsDead() || a_dmg < 0.5f) return;
			auto*       av = a_t->AsActorValueOwner();
			const float h0 = av->GetActorValue(RE::ActorValue::kHealth);
			if (!gUseRestore) {
				a_t->HandleHealthDamage(a_pc, a_dmg);
				const float h1 = av->GetActorValue(RE::ActorValue::kHealth);
				if (h1 > h0 + 0.5f) {
					// segno invertito: correggo e passo all'altro metodo
					gUseRestore = true;
					Say("danno di fuoco: HandleHealthDamage ha curato, passo al metodo alternativo");
					av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -((h1 - h0) + a_dmg));
				} else if (std::abs(h1 - h0) < 0.01f && h0 > a_dmg + 1.0f) {
					if (++gNoEffect >= 3) {
						gUseRestore = true;
						Say("danno di fuoco: HandleHealthDamage senza effetto, passo al metodo alternativo");
						av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -a_dmg);
					}
				} else {
					gNoEffect = 0;
				}
			} else {
				av->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, -a_dmg);
			}
		}

		void ProcessDamage(RE::PlayerCharacter* a_pc, float a_dt)
		{
			std::vector<Pending> take;
			{
				std::lock_guard l(gPendMx);
				take.swap(gPend);
			}
			for (auto& p : take) {
				auto sp = p.h.get();
				if (!sp) continue;
				if (p.dmg < 0.0f) {
					bool found = false;
					for (auto& b : gBurn) {
						if (b.h == p.h) {
							b.left = S.burnSeconds;
							found = true;
							break;
						}
					}
					if (!found && gBurn.size() < 48) {
						gBurn.push_back({ p.h, S.burnSeconds, 0.0f });
					}
				} else {
					ApplyDamage(a_pc, sp.get(), p.dmg);
				}
			}

			for (auto& b : gBurn) {
				auto sp = b.h.get();
				if (!sp || sp->IsDead()) {
					b.left = 0.0f;
					continue;
				}
				b.left -= a_dt;
				b.acc += S.burnDps * a_dt;
				if (b.acc >= 2.0f) {
					float resist = 0.0f;
					if (S.respectResist) {
						resist = std::clamp(sp->AsActorValueOwner()->GetActorValue(RE::ActorValue::kResistFire), 0.0f, 100.0f) / 100.0f;
					}
					ApplyDamage(a_pc, sp.get(), b.acc * (1.0f - resist));
					b.acc = 0.0f;
				}
			}
			gBurn.erase(std::remove_if(gBurn.begin(), gBurn.end(), [](const Burn& b) { return b.left <= 0.0f; }), gBurn.end());
		}

		// ---------------------------------------------------------------- percorso di avvio
		void ResetAll(bool a_hideFx)
		{
			Lock(false);
			gAnimWindow = false;
			gHasT0 = false;
			gAnchored = false;
			SetGlobal(kGlobalAnim, 0.0f);
			SetGlobal(kGlobalOn, 0.0f);
			if (a_hideFx) {
				fx::Hide();
			}
			Enter(St::Off);
		}

		void Abort(const std::string& a_why)
		{
			Say("accensione annullata: {}", a_why);
			ResetAll(true);
		}

		void BeginIgnite(RE::PlayerCharacter* a_pc)
		{
			DrainMana(a_pc, S.manaStart);
			if (!fx::Resolve(a_pc)) {
				Say("attenzione: {}", fx::Describe());
			}
			fx::Begin(S.rampSeconds);
			PlayIgnite();
			SetGlobal(kGlobalOn, 1.0f);
			gMsgLowMana = false;
			Enter(St::Igniting);
			Say("accensione graduale (durata {:.2f}s)", S.rampSeconds);
		}

		void FireTrigger(RE::PlayerCharacter* a_pc)
		{
			AttachSink(a_pc);
			gAnchored = false;
			gHasT0 = false;
			gSheathEv = false;
			gIdleStopEv = false;
			gNotDrawnSince = -1.0;
			gDiagTimer = 0.0f;
			gLogBudget = 90;
			gReqClock = ClockNow();

			SetGlobal(kGlobalAnim, 1.0f);
			gAnimWindow = true;
			gAnimWindowUntil = ClockNow() + 12.0;

			// L'evento BFM_Ignite entra nello stato Nemesis dedicato e riproduce
			// una sola clip. Non provare gli idle vanilla: altre mod li sostituiscono.
			const std::string event = S.triggerEvent[0] ? S.triggerEvent : "BFM_Ignite";
			const bool ok = a_pc->NotifyAnimationGraph(event);
			Say("evento standalone '{}' -> {}", event, ok ? "accettato" : "rifiutato");
			gLastEvent = event;

			gReqClock = ClockNow();
			if (!ok) {
				// Non chiudere la modalita' solo per il valore di ritorno del dispatcher:
				// mantieni il sink in ascolto per Pie e usa il timer stimato se non arriva.
				Say("evento non confermato: mantengo la finestra di ascolto e uso il timer se Pie non arriva");
				if (S.fallbackIgnite) {
					Say("fallback immediato configurato: accendo senza attendere l'animazione");
					gAnimWindow = false;
					SetGlobal(kGlobalAnim, 0.0f);
					BeginIgnite(a_pc);
					return;
				}
			}

			if (S.lockControls || S.lockAttack) {
				Lock(true);
				gLockUntil = gReqClock.load() + static_cast<double>(S.clipLength) + 3.0;
			}
			Say("arma estratta: {}", (IsDrawn(a_pc) ? "si" : "NO"));
			Enter(St::Requested);
			return;
		}

		// true = accensione annullata (arma rinfoderata o clip interrotta prima del contatto)
		bool CheckInterrupt(RE::PlayerCharacter* a_pc, double a_now)
		{
			if (!IsDrawn(a_pc)) {
				if (gNotDrawnSince < 0.0) gNotDrawnSince = a_now;
			} else {
				gNotDrawnSince = -1.0;
			}
			const bool sheathed = gSheathEv.load() || (gNotDrawnSince >= 0.0 && a_now - gNotDrawnSince > 0.25);
			if (sheathed) {
				std::string msg = "arma rinfoderata durante l'animazione: accensione annullata";
				if (gLocked) {
					S.lockControls = false;
					S.lockAttack = false;
					RequestSave();
					msg += " (ho disattivato il blocco dei comandi, riprova)";
				}
				if (!gLastEvent.empty() && (a_now - gReqClock.load()) < 3.0) {
					msg += " Evento standalone '" + gLastEvent + "' interrotto; al prossimo uso verra' riprovato";
				}
				Hud(msg);
				Abort("arma rinfoderata durante l'animazione");
				return true;
			}
			if (gIdleStopEv.load() && gHasT0 && (a_now - gT0) < static_cast<double>(S.ignitionOffset) - 0.3) {
				Hud("Animazione interrotta: accensione annullata");
				Abort("animazione interrotta prima del contatto");
				return true;
			}
			return false;
		}

		void StartRequest()
		{
			if (St_() != St::Off) return;
			auto* pc = RE::PlayerCharacter::GetSingleton();
			if (!pc || !pc->Is3DLoaded() || pc->IsDead()) return;

			auto* ui = RE::UI::GetSingleton();
			if (ui && ui->GameIsPaused()) return;

			if (!EquippedSword(pc)) {
				Hud("Equipaggia la spada nella mano destra");
				return;
			}
			if (pc->IsOnMount() || pc->IsInKillMove() || pc->IsInRagdollState() || pc->IsInMidair() || pc->IsStaggering()) {
				Say("richiesta ignorata: stato del personaggio non adatto");
				return;
			}
			if (Mana(pc) < S.manaStart + 0.5f || Mana(pc) <= S.manaMin) {
				Hud("Mana insufficiente");
				return;
			}
			fx::Resolve(pc);
			fx::InstanceChanged();

			if (!S.animEnabled) {
				BeginIgnite(pc);
				return;
			}

			auto* state = pc->AsActorState();
			if (state && !state->IsWeaponDrawn()) {
				if (!S.drawIfSheathed) {
					Hud("Estrai l'arma");
					return;
				}
				pc->DrawWeaponMagicHands(true);
				gDrawStart = ClockNow();
				gDrawn = false;
				Enter(St::Drawing);
				Say("estraggo l'arma prima dell'animazione");
				return;
			}
			FireTrigger(pc);
		}

		void StartExtinguish(const char* a_why)
		{
			const auto st = St_();
			if (st == St::Off || st == St::Extinguishing) return;
			Say("spegnimento: {}", a_why);
			if (st == St::Igniting || st == St::On) {
				fx::End();
				PlayOff();
				Enter(St::Extinguishing);
				SetGlobal(kGlobalOn, 0.0f);
				Lock(false);
			} else {
				ResetAll(true);
			}
		}

		void HandleRequests()
		{
			int on = input::TakeOn();
			int off = input::TakeOff();
			on += gReqOn.exchange(0);
			off += gReqOff.exchange(0);

			const auto st = St_();
			if (off > 0) {
				if (st != St::Off) StartExtinguish("richiesta");
				return;
			}
			if (on > 0) {
				// senza tasto "spegni" lo stesso tasto fa da interruttore
				bool hasOff;
				{
					std::lock_guard l(gBindMx);
					hasOff = !S.bindOff.empty();
				}
				if (st == St::Off) {
					StartRequest();
				} else if (!hasOff && (st == St::On || st == St::Igniting)) {
					StartExtinguish("interruttore");
				}
			}

			std::string ev, snd;
			{
				std::lock_guard l(gEvMx);
				ev.swap(gReqEvent);
				snd.swap(gReqSound);
			}
			if (!snd.empty()) {
				const bool ok = PlayByName(snd.c_str());
				Hud(ok ? "Suono '" + snd + "': ok" : "Suono '" + snd + "': NON trovato");
			}
			if (!ev.empty()) {
				if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
					AttachSink(pc);
					gLogBudget = 40;
					const bool ok = pc->NotifyAnimationGraph(ev);
					Say("prova evento '{}' -> {}", ev, ok ? "accettato" : "rifiutato");
				}
			}
		}

		void UpdateSnapshot(RE::PlayerCharacter* a_pc)
		{
			Snapshot s;
			s.state = Name(St_());
			s.stateTime = gStateTime;
			s.anchored = gAnchored.load();
			s.clipTime = gHasT0 ? static_cast<float>(gClock.load() - gT0) : -1.0f;
			if (a_pc) {
				auto* o = a_pc->GetEquippedObject(false);
				if (auto* w = o ? o->As<RE::TESObjectWEAP>() : nullptr) {
					s.weapon = w->GetName() ? w->GetName() : "";
					s.model = w->GetModel() ? w->GetModel() : "";
				}
				s.swordOk = EquippedSword(a_pc) != nullptr;
				s.mana = Mana(a_pc);
				s.manaMax = a_pc->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kMagicka);
			}
			s.fx = fx::Describe();
			s.espOk = Glob(kGlobalAnim) != nullptr;
			s.hits = gHits.load();
			s.lastExtra = gLastExtra.load();
			s.burning = static_cast<int>(gBurn.size());
			std::lock_guard l(gSnapMx);
			gSnap = std::move(s);
		}
	}

	// ====================================================================== API pubblica
	void Install()
	{
		if (auto* src = RE::ScriptEventSourceHolder::GetSingleton()) {
			src->AddEventSink<RE::TESHitEvent>(&gHitSink);
		}
		Say("modalita': ascoltatori collegati");
	}

	void OnPreLoad()
	{
		ResetAll(false);
		fx::Forget();
		gBurn.clear();
		std::lock_guard l(gPendMx);
		gPend.clear();
	}

	void OnGameLoaded()
	{
		ResetAll(false);
		fx::Forget();
		gBurn.clear();
		if (auto* pc = RE::PlayerCharacter::GetSingleton()) {
			AttachSink(pc);
		}
		if (!Glob(kGlobalAnim)) {
			Say("ATTENZIONE: {} non trovato tra i plugin attivi: l'animazione non puo' essere sostituita", kEsp);
		}
	}

	void RequestOn() { ++gReqOn; }
	void RequestOff() { ++gReqOff; }
	void RequestToggle()
	{
		if (St_() == St::Off) ++gReqOn; else ++gReqOff;
	}
	void RequestEvent(const std::string& a_event)
	{
		std::lock_guard l(gEvMx);
		gReqEvent = a_event;
	}
	void RequestSoundTest(const std::string& a_name)
	{
		std::lock_guard l(gEvMx);
		gReqSound = a_name;
	}

	bool IsActive()
	{
		const auto s = St_();
		return s == St::On || s == St::Igniting;
	}

	Snapshot GetSnapshot()
	{
		std::lock_guard l(gSnapMx);
		return gSnap;
	}

	void Tick()
	{
		auto* pc = RE::PlayerCharacter::GetSingleton();
		if (!pc) return;

		// ----- orologio
		const auto  nowNs = clk::now().time_since_epoch().count();
		const auto  lastNs = gLastTickNs.load();
		float       dt = lastNs > 0 ? static_cast<float>(static_cast<double>(nowNs - lastNs) * 1e-9) : 0.0f;
		dt = std::clamp(dt, 0.0f, 0.25f);
		auto* ui = RE::UI::GetSingleton();
		const bool paused = ui && ui->GameIsPaused();
		gPaused = paused;
		if (paused) dt = 0.0f;
		gLastTickNs = nowNs;
		gClock = gClock.load() + static_cast<double>(dt);
		const double now = gClock.load();
		gStateTime += dt;

		gSaveTimer += dt;
		if (gSaveTimer > 1.0f) {
			gSaveTimer = 0.0f;
			SaveIfDirty();
		}

		if (!pc->Is3DLoaded()) return;

		HandleRequests();

		// morte o attesa: tutto spento
		if (pc->IsDead() && St_() != St::Off) {
			ResetAll(true);
		}

		// finestra animazione (OAR) e blocco comandi: sempre sotto controllo
		if (gAnimWindow && (now > gAnimWindowUntil || (gHasT0 && now > gT0 + S.clipLength + 0.4))) {
			gAnimWindow = false;
			SetGlobal(kGlobalAnim, 0.0f);
		}
		if (gLocked && (now > gLockUntil || (gHasT0 && now > gT0 + S.clipLength))) {
			Lock(false);
		}

		switch (St_()) {
		case St::Off:
			if (gLocked) Lock(false);
			break;

		case St::Drawing: {
			auto* state = pc->AsActorState();
			if (!EquippedSword(pc)) {
				Abort("spada non piu' equipaggiata");
				break;
			}
			if (state && state->IsWeaponDrawn()) {
				if (!gDrawn) {
					gDrawn = true;
					gDrawnAt = now;
				}
				if (now - gDrawnAt > 0.45) {
					FireTrigger(pc);
				}
			} else if (now - gDrawStart > 3.5) {
				Abort("l'arma non e' stata estratta");
			}
			break;
		}

		case St::Requested:
			if (!EquippedSword(pc)) {
				Abort("spada non piu' equipaggiata");
				break;
			}
			if (CheckInterrupt(pc, now)) break;
			gDiagTimer += dt;
			if (gDiagTimer > 0.5f && S.logEvents) {
				gDiagTimer = 0.0f;
				Say("+{:.2f}s in attesa della clip: arma estratta={} ancorata={}", now - gReqClock.load(), IsDrawn(pc) ? "si" : "NO", gAnchored.load() ? "si" : "no");
			}
			if (gAnchored.load()) {
				gT0 = gAnchorT0.load();
				gHasT0 = true;
				Say("animazione agganciata all'evento di riferimento (inizio clip {:.2f}s fa)", now - gT0);
				Enter(St::Playing);
			} else if (now - gReqClock.load() >= static_cast<double>(S.startTimeout)) {
				gT0 = gReqClock.load() + 0.15;
				gHasT0 = true;
				Say("evento di riferimento non arrivato: tempi stimati dall'avvio della richiesta");
				Enter(St::Playing);
			}
			break;

		case St::Playing: {
			if (!EquippedSword(pc)) {
				Abort("spada non piu' equipaggiata");
				break;
			}
			if (CheckInterrupt(pc, now)) break;
			gDiagTimer += dt;
			if (gDiagTimer > 0.5f && S.logEvents) {
				gDiagTimer = 0.0f;
				Say("clip a {:.2f}s: arma estratta={}", now - gT0, IsDrawn(pc) ? "si" : "NO");
			}
			if (gAnchored.load()) {
				gT0 = gAnchorT0.load();  // riallineo finche' non accendo
			}
			const double t = now - gT0;
			if (t >= static_cast<double>(S.ignitionOffset)) {
				Say("contatto mano-lama a {:.2f}s: accendo", t);
				BeginIgnite(pc);
			}
			break;
		}

		case St::Igniting:
		case St::On: {
			if (S.offOnUnequip && !EquippedSword(pc)) {
				fx::Hide();
				ResetAll(true);
				Say("spenta: spada non piu' equipaggiata");
				break;
			}
			if (S.offOnSheathe) {
				auto* state = pc->AsActorState();
				if (state && !state->IsWeaponDrawn()) {
					StartExtinguish("arma rinfoderata");
					break;
				}
			}

			// modello ricreato (cambio equipaggiamento / ricarica 3D): riprendo l'effetto
			gFxCheck += dt;
			if (gFxCheck > 0.2f) {
				gFxCheck = 0.0f;
				if (!fx::Present()) {
					fx::Resolve(pc);
				}
				if (fx::InstanceChanged() && fx::Targets() > 0) {
					Say("modello ricreato: ripristino l'effetto");
					if (St_() == St::On) fx::Loop(); else fx::Begin(S.rampSeconds);
				}
			}

			// mana
			const bool inCombat = pc->IsInCombat();
			if (!S.drainOnlyInCombat || inCombat) {
				DrainMana(pc, S.manaDrain * dt);
			}
			if (Mana(pc) <= S.manaMin) {
				if (!gMsgLowMana) {
					gMsgLowMana = true;
					Hud("Mana esaurito: la lama si spegne");
				}
				StartExtinguish("mana");
				break;
			}

			if (St_() == St::Igniting && gStateTime >= S.rampSeconds) {
				fx::Loop();
				Enter(St::On);
				Say("lama completamente accesa");
			}
			break;
		}

		case St::Extinguishing:
			if (gStateTime >= 0.75f) {
				fx::Hide();
				Enter(St::Off);
				Say("lama spenta");
			}
			break;
		}

		ProcessDamage(pc, dt);

		gSnapTimer += dt;
		if (gSnapTimer > 0.15f || paused) {
			gSnapTimer = 0.0f;
			UpdateSnapshot(pc);
		}
	}
}
