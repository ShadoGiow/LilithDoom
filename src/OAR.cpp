#include "pch.h"

#include "Mode.h"
#include "OAR.h"
#include "Log.h"
#include "OAR_API/OpenAnimationReplacerAPI-Conditions.h"

OAR_API::Conditions::IConditionsInterface* g_oarConditionsInterface = nullptr;

namespace OAR_API::Conditions
{
    IConditionsInterface* GetAPI(const InterfaceVersion a_version)
    {
        if (g_oarConditionsInterface) {
            return g_oarConditionsInterface;
        }
        const auto module = ::GetModuleHandleA("OpenAnimationReplacer.dll");
        if (!module) {
            return nullptr;
        }
        const auto request = reinterpret_cast<_RequestPluginAPI_Conditions>(
            ::GetProcAddress(module, "RequestPluginAPI_Conditions"));
        if (!request) {
            return nullptr;
        }
        const auto plugin = SKSE::PluginDeclaration::GetSingleton();
        g_oarConditionsInterface = request(a_version, plugin->GetName().data(), plugin->GetVersion());
        return g_oarConditionsInterface;
    }
}

namespace
{
    class BladeFireModeIgnitingCondition final : public Conditions::CustomCondition
    {
    public:
        inline static constexpr std::string_view CONDITION_NAME = "BladeFireModeIgniting"sv;

        RE::BSString GetName() const override { return CONDITION_NAME.data(); }
        RE::BSString GetDescription() const override
        {
            return "True only for the player while BladeFireMode is starting Ignite.";
        }
        constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

    protected:
        bool EvaluateImpl(RE::TESObjectREFR* a_refr, RE::hkbClipGenerator*, void*) const override
        {
            return a_refr && a_refr->IsPlayerRef() && bf::mode::IsOARIgniting();
        }
    };
}

namespace bf::oar
{
    bool Register()
    {
        const auto result = OAR_API::Conditions::AddCustomCondition<BladeFireModeIgnitingCondition>();
        using enum OAR_API::Conditions::APIResult;
        switch (result) {
        case OK:
            Say("OAR: condizione BladeFireModeIgniting registrata");
            return true;
        case AlreadyRegistered:
            Say("OAR: condizione BladeFireModeIgniting gia' registrata");
            return true;
        case Invalid:
            Say("OAR: condizione BladeFireModeIgniting non valida");
            return false;
        case Failed:
            Say("OAR: API non disponibile; Ignite OAR non puo' avviarsi");
            return false;
        }
        return false;
    }
}

