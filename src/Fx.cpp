#include "pch.h"

#include "Fx.h"
#include "Log.h"

namespace bf::fx
{
	namespace
	{
		struct Target
		{
			RE::NiPointer<RE::NiAVObject>          group;
			RE::NiPointer<RE::NiControllerManager> mgr;
			RE::NiPointer<RE::NiControllerSequence> sBegin, sLoop, sEnd;
		};

		std::vector<Target> gT;
		Phase               gPhase{ Phase::Hidden };
		bool                gChanged{ false };
		std::string         gWhy;

		RE::NiControllerManager* FindManager(RE::NiAVObject* a_from)
		{
			for (auto* n = a_from; n; n = n->parent) {
				for (auto* c = n->GetControllers(); c; c = c->next.get()) {
					if (auto* m = c->AsNiControllerManager()) {
						return m;
					}
				}
			}
			return nullptr;
		}

		void Collect(RE::NiAVObject* a_root, std::vector<Target>& a_out, const char* a_tag)
		{
			if (!a_root) return;
			auto* g = a_root->GetObjectByName("BoundAxeEnchEffects");
			if (!g) return;
			auto* m = FindManager(g);
			if (!m) {
				gWhy = std::format("{}: gruppo trovato ma senza NiControllerManager", a_tag);
				return;
			}
			Target t;
			t.group.reset(g);
			t.mgr.reset(m);
			t.sBegin.reset(m->GetSequenceByName("mBegin"));
			t.sLoop.reset(m->GetSequenceByName("mLoop"));
			t.sEnd.reset(m->GetSequenceByName("mEnd"));
			if (!t.sBegin || !t.sLoop || !t.sEnd) {
				gWhy = std::format("{}: sequenze mancanti (mBegin {} mLoop {} mEnd {})", a_tag, t.sBegin ? 1 : 0, t.sLoop ? 1 : 0, t.sEnd ? 1 : 0);
				return;
			}
			a_out.push_back(std::move(t));
		}

		void Stop(RE::NiControllerSequence* a_s, float a_ease)
		{
			if (a_s && !a_s->Inactive()) {
				a_s->Deactivate(a_ease, false);
			}
		}

		void Play(Target& a_t, RE::NiControllerSequence* a_s, float a_freq, float a_ease)
		{
			if (!a_s) return;
			if (!a_s->Inactive()) {
				a_s->Deactivate(0.0f, false);
			}
			a_s->frequency = a_freq;
			a_s->Activate(0, true, 1.0f, a_ease, nullptr, false);  // true = riparte dall'inizio
			(void)a_t;
		}

		void EnsureRunning(Target& a_t)
		{
			if (a_t.mgr && !a_t.mgr->flags.any(RE::NiTimeController::Flag::kActive)) {
				a_t.mgr->Start(0.0f);
			}
		}
	}

	bool Resolve(RE::PlayerCharacter* a_pc)
	{
		if (!a_pc) return false;

		std::vector<Target> now;
		gWhy.clear();
		Collect(a_pc->Get3D(false), now, "terza persona");
		Collect(a_pc->Get3D(true), now, "prima persona");

		// ricreato?
		bool same = now.size() == gT.size();
		if (same) {
			for (std::size_t i = 0; i < now.size(); ++i) {
				if (now[i].group.get() != gT[i].group.get()) {
					same = false;
					break;
				}
			}
		}
		if (!same) {
			if (!gT.empty() || !now.empty()) {
				gChanged = true;
			}
			gT = std::move(now);
		}
		return !gT.empty();
	}

	bool Present()
	{
		if (gT.empty()) return false;
		for (auto& t : gT) {
			if (!t.group || !t.group->parent) return false;
		}
		return true;
	}

	bool InstanceChanged()
	{
		return std::exchange(gChanged, false);
	}

	void Forget()
	{
		gT.clear();
		gPhase = Phase::Hidden;
		gChanged = false;
	}

	void Begin(float a_rampSeconds)
	{
		const float freq = 2.3333f / std::max(0.3f, a_rampSeconds);
		for (auto& t : gT) {
			t.group->SetAppCulled(false);
			EnsureRunning(t);
			Stop(t.sLoop.get(), 0.0f);
			Stop(t.sEnd.get(), 0.0f);
			Play(t, t.sBegin.get(), freq, 0.0f);
		}
		gPhase = Phase::Begin;
	}

	void Loop()
	{
		for (auto& t : gT) {
			t.group->SetAppCulled(false);
			EnsureRunning(t);
			Stop(t.sEnd.get(), 0.0f);
			Play(t, t.sLoop.get(), 1.0f, 0.25f);
			Stop(t.sBegin.get(), 0.25f);
		}
		gPhase = Phase::Loop;
	}

	void End()
	{
		for (auto& t : gT) {
			EnsureRunning(t);
			Play(t, t.sEnd.get(), 1.0f, 0.1f);
			Stop(t.sLoop.get(), 0.1f);
			Stop(t.sBegin.get(), 0.1f);
		}
		gPhase = Phase::End;
	}

	void Hide()
	{
		for (auto& t : gT) {
			if (!t.group) continue;
			Stop(t.sBegin.get(), 0.0f);
			Stop(t.sLoop.get(), 0.0f);
			Stop(t.sEnd.get(), 0.0f);
			t.group->SetAppCulled(true);
		}
		gPhase = Phase::Hidden;
	}

	Phase Current() { return gPhase; }

	int Targets() { return static_cast<int>(gT.size()); }

	std::string Describe()
	{
		if (!gT.empty()) {
			return std::format("effetti trovati su {} modello/i", gT.size());
		}
		return gWhy.empty() ? "nessun effetto sulla spada equipaggiata" : gWhy;
	}
}
