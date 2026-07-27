#include "Endpoint.hpp"
#include "SrvResolver.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FText.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UFunctionStructs.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace PalworldSrvConnect
{
    namespace LogLevel = RC::LogLevel;
    namespace Output = RC::Output;
    using RC::Unreal::FText;
    using RC::Unreal::FString;
    using RC::Unreal::UFunction;
    using RC::Unreal::UObject;
    using RC::Unreal::UnrealScriptFunctionCallableContext;

    namespace
    {
        struct JoinByIpParams
        {
            FString address{};
        };

        struct ConnectByAddressParams
        {
            FString address{};
            std::int32_t port{};
        };

        struct SetTextParams
        {
            FText text{};
        };

        struct InstalledHook
        {
            UFunction* function{};
            std::pair<int, int> ids{};
        };

        struct PendingAddressRewrite
        {
            UObject* owner{};
            UObject* text_box{};
            std::wstring original{};
            std::wstring rewritten{};
        };

        auto ToWide(const FString& value) -> std::wstring
        {
            return std::wstring{*value, static_cast<std::size_t>(value.Len())};
        }

        auto FindFunction(const wchar_t* path) -> UFunction*
        {
            return RC::Unreal::UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
        }
    }

    class PalworldSrvConnectMod final : public RC::CppUserModBase
    {
      public:
        PalworldSrvConnectMod()
        {
            ModName = STR("PalworldSrvConnect");
            ModVersion = STR("0.1.0");
            ModDescription = STR("Adds DNS SRV support to Palworld direct connect.");
        }

        ~PalworldSrvConnectMod() override
        {
            for (auto iterator = m_hooks.rbegin(); iterator != m_hooks.rend(); ++iterator)
            {
                try
                {
                    RC::Unreal::UObjectGlobals::UnregisterHook(iterator->function, iterator->ids);
                }
                catch (const std::exception& error)
                {
                    Output::send<LogLevel::Warning>(STR("[PalworldSrvConnect] Failed to remove hook: {}\n"), RC::ensure_str(error.what()));
                }
            }
        }

        auto on_unreal_init() -> void override
        {
            m_unreal_initialized = true;
            m_save_config_function = FindFunction(STR("/Script/Pal.PalUIJoinGameBase:SaveConfigValue"));
            InstallInputValidationHook();
            InstallRawInputHook();
            InstallConnectHook();

            if (!m_input_validation_hook_installed)
            {
                Output::send<LogLevel::Verbose>(
                        STR("[PalworldSrvConnect] Waiting for the title UI; connection hooks will be installed automatically.\n"));
            }
            else
            {
                LogReady();
            }
        }

        auto on_update() -> void override
        {
            if (!m_unreal_initialized || m_input_validation_hook_installed) return;

            const auto now = std::chrono::steady_clock::now();
            if (now < m_next_ui_hook_attempt) return;
            m_next_ui_hook_attempt = now + std::chrono::seconds{3};

            InstallInputValidationHook();
            InstallRawInputHook();
            if (m_input_validation_hook_installed) LogReady();
        }

      private:
        void InstallInputValidationHook()
        {
            if (m_input_validation_hook_installed) return;

            m_set_text_function = FindFunction(STR("/Script/Pal.PalEditableTextBox:SetText"));
            if (!m_set_text_function) return;

            auto* function = FindFunction(
                    STR("/Game/Pal/Blueprint/UI/UserInterface/Title/WBP_Title_WorldSelect.WBP_Title_WorldSelect_C:BndEvt__WBP_Title_WorldSelect_WBP_JoinByIPButton_K2Node_ComponentBoundEvent_5_OnClickedSingleButton__DelegateSignature"));
            if (!function) return;

            m_input_validation_hook_installed = Register(
                    function,
                    [](UnrealScriptFunctionCallableContext& context, void* custom_data) {
                        static_cast<PalworldSrvConnectMod*>(custom_data)->RewriteAddressTextBox(context);
                    },
                    [](UnrealScriptFunctionCallableContext& context, void* custom_data) {
                        static_cast<PalworldSrvConnectMod*>(custom_data)->RestoreAddressTextBox(context);
                    });
        }

        void InstallRawInputHook()
        {
            if (m_raw_input_hook_installed) return;

            auto* function = FindFunction(
                    STR("/Game/Pal/Blueprint/UI/Title/WBP_JoinGame.WBP_JoinGame_C:OnClicked_JoinByIPButton"));
            if (!function) return;

            m_raw_input_hook_installed = Register(
                    function,
                    [](UnrealScriptFunctionCallableContext& context, void* custom_data) {
                        static_cast<PalworldSrvConnectMod*>(custom_data)->RewriteRawInput(context);
                    },
                    [](UnrealScriptFunctionCallableContext& context, void* custom_data) {
                        static_cast<PalworldSrvConnectMod*>(custom_data)->RestoreSavedInput(context);
                    });
        }

        void InstallConnectHook()
        {
            auto* function = FindFunction(STR("/Script/Pal.PalUIJoinGameBase:ConnectServerByAddress"));
            if (!function)
            {
                Output::send<LogLevel::Warning>(STR("[PalworldSrvConnect] PalUIJoinGameBase.ConnectServerByAddress was not found.\n"));
                return;
            }

            Register(function, [](UnrealScriptFunctionCallableContext& context, void* custom_data) {
                static_cast<PalworldSrvConnectMod*>(custom_data)->RewriteConnectParams(context);
            });
        }

        auto Register(
                UFunction* function,
                RC::Unreal::UnrealScriptFunctionCallable pre_callback,
                RC::Unreal::UnrealScriptFunctionCallable post_callback) -> bool
        {
            try
            {
                auto ids = RC::Unreal::UObjectGlobals::RegisterHook(
                        function,
                        std::move(pre_callback),
                        std::move(post_callback),
                        this);
                m_hooks.push_back({function, ids});
                Output::send<LogLevel::Verbose>(STR("[PalworldSrvConnect] Hooked {}\n"), function->GetFullName());
                return true;
            }
            catch (const std::exception& error)
            {
                Output::send<LogLevel::Warning>(STR("[PalworldSrvConnect] Could not hook {}: {}\n"), function->GetFullName(), RC::ensure_str(error.what()));
                return false;
            }
        }

        auto Register(UFunction* function, RC::Unreal::UnrealScriptFunctionCallable pre_callback) -> bool
        {
            return Register(
                    function,
                    std::move(pre_callback),
                    [](UnrealScriptFunctionCallableContext&, void*) {});
        }

        void LogReady()
        {
            if (m_ready_logged) return;
            m_ready_logged = true;
            Output::send<LogLevel::Verbose>(STR("[PalworldSrvConnect] Ready. Installed {} connection hook(s).\n"), m_hooks.size());
        }

        void RewriteRawInput(UnrealScriptFunctionCallableContext& context)
        {
            auto& params = context.GetParams<JoinByIpParams>();
            const auto original = ToWide(params.address);
            const auto resolution = m_resolver.ResolveInput(original);
            if (resolution.status == ResolutionStatus::Resolved)
            {
                const auto rewritten = FormatEndpoint(resolution.target, resolution.port);
                params.address = FString{rewritten.c_str()};
                LogResolution(original, resolution, rewritten);
            }
            else if (resolution.status == ResolutionStatus::ServiceUnavailable)
            {
                Output::send<LogLevel::Warning>(STR("[PalworldSrvConnect] SRV record {} explicitly marks the service unavailable.\n"), resolution.query);
            }
        }

        void RewriteAddressTextBox(UnrealScriptFunctionCallableContext& context)
        {
            if (!context.Context || !m_set_text_function) return;

            m_pending_rewrite.reset();

            auto** text_box = context.Context->GetValuePtrByPropertyNameInChain<UObject*>(STR("PalEditableTextBox_IP"));
            if (!text_box || !*text_box) return;

            auto* text = (*text_box)->GetValuePtrByPropertyNameInChain<FText>(STR("Text"));
            if (!text) return;

            const std::wstring original{text->ToString()};
            const auto resolution = m_resolver.ResolveInput(original);
            if (resolution.status == ResolutionStatus::Resolved)
            {
                const auto rewritten = FormatEndpoint(resolution.target, resolution.port);
                m_pending_rewrite = PendingAddressRewrite{context.Context, *text_box, original, rewritten};
                SetTextParams params{FText{rewritten.c_str()}};
                (*text_box)->ProcessEvent(m_set_text_function, &params);
                LogResolution(original, resolution, rewritten);
            }
            else if (resolution.status == ResolutionStatus::ServiceUnavailable)
            {
                Output::send<LogLevel::Warning>(STR("[PalworldSrvConnect] SRV record {} explicitly marks the service unavailable.\n"), resolution.query);
            }
        }

        void RestoreAddressTextBox(UnrealScriptFunctionCallableContext& context)
        {
            if (!m_pending_rewrite || context.Context != m_pending_rewrite->owner || !m_set_text_function) return;

            SetTextParams params{FText{m_pending_rewrite->original.c_str()}};
            m_pending_rewrite->text_box->ProcessEvent(m_set_text_function, &params);
            m_pending_rewrite.reset();
        }

        void RestoreSavedInput(UnrealScriptFunctionCallableContext& context)
        {
            if (!m_pending_rewrite || !context.Context) return;

            const auto& params = context.GetParams<JoinByIpParams>();
            if (ToWide(params.address) != m_pending_rewrite->rewritten) return;

            auto* saved_input = context.Context->GetValuePtrByPropertyNameInChain<FString>(STR("InputIPAddress"));
            if (!saved_input) return;

            *saved_input = FString{m_pending_rewrite->original.c_str()};
            if (m_save_config_function) context.Context->ProcessEvent(m_save_config_function, nullptr);
        }

        void RewriteConnectParams(UnrealScriptFunctionCallableContext& context)
        {
            auto& params = context.GetParams<ConnectByAddressParams>();
            const auto original = ToWide(params.address);
            const auto parsed = ParseEndpoint(original);
            if (!parsed) return;

            if (parsed->port.has_value() || params.port > 0) return;

            const auto resolution = m_resolver.ResolveHost(parsed->host);
            if (resolution.status == ResolutionStatus::Resolved)
            {
                params.address = FString{resolution.target.c_str()};
                params.port = resolution.port;
                LogResolution(original, resolution, FormatEndpoint(resolution.target, resolution.port));
            }
            else if (resolution.status == ResolutionStatus::ServiceUnavailable)
            {
                Output::send<LogLevel::Warning>(STR("[PalworldSrvConnect] SRV record {} explicitly marks the service unavailable.\n"), resolution.query);
            }
        }

        static void LogResolution(std::wstring_view original, const Resolution& resolution, std::wstring_view rewritten)
        {
            Output::send<LogLevel::Verbose>(
                    STR("[PalworldSrvConnect] {} resolved through {} -> {}\n"),
                    std::wstring{original},
                    resolution.query,
                    std::wstring{rewritten});
        }

        SrvResolver m_resolver{};
        UFunction* m_set_text_function{};
        UFunction* m_save_config_function{};
        std::vector<InstalledHook> m_hooks{};
        std::optional<PendingAddressRewrite> m_pending_rewrite{};
        std::chrono::steady_clock::time_point m_next_ui_hook_attempt{};
        bool m_unreal_initialized{};
        bool m_input_validation_hook_installed{};
        bool m_raw_input_hook_installed{};
        bool m_ready_logged{};
    };
}

#define PALWORLD_SRV_CONNECT_API __declspec(dllexport)

extern "C"
{
    PALWORLD_SRV_CONNECT_API RC::CppUserModBase* start_mod()
    {
        return new PalworldSrvConnect::PalworldSrvConnectMod();
    }

    PALWORLD_SRV_CONNECT_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
