// plugins/Injector/Module.cpp
#include <Windows.h>
#include <string>
#include <memory>
#include <cstdio>

#include "plugins/IModule.h"
#include "imgui/imgui.h"
#include "include/SelectedPidProvider.h"
#include "include/injector.h"
#include "include/utils.h"

namespace REKit {
    namespace Plugins {

        class InjectorModule final : public IModule {
        public:
            const char* Name() const override { return "Injector"; }

            void OnLoad(ModuleContext& ctx) override {
                ctx.ui.AddPanel("Tools/Injector", [this] { Draw(); });
            }
            void OnUnload(ModuleContext&) override {}

        private:
            int          method_ = 0;
            std::wstring dllPathW_;
            char         dllPathA_[MAX_PATH] = { 0 };
            int          pidInput_ = 0;
            int          lastProviderPid_ = -1;

            void SyncPidFromProviderOnce() {
                const int providerPid = GetSelectedPidOrFallback(0);
                if (providerPid > 0 && providerPid != lastProviderPid_) {
                    pidInput_ = providerPid;
                    lastProviderPid_ = providerPid;
                }
            }

            void DrawPidInput() {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("PID:");
                ImGui::SameLine(0.0f, 8.0f);

                ImGui::SetNextItemWidth(160.0f);
                ImGui::InputInt("##pid_input", &pidInput_, 0, 0);
                if (pidInput_ < 0) pidInput_ = 0;

                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::Button("使用选中的PID")) {
                    int providerPid = GetSelectedPidOrFallback(0);
                    if (providerPid > 0) {
                        pidInput_ = providerPid;
                        lastProviderPid_ = providerPid;
                    }
                }
            }


            void DrawDllSelector() {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("DLL:");
                ImGui::SameLine(0.0f, 8.0f);

                ImGui::SetNextItemWidth(360.0f);
                if (!dllPathW_.empty()) {
                    std::string a = WStringToUTF8(dllPathW_);
                    std::snprintf(dllPathA_, sizeof(dllPathA_), "%s", a.c_str());
                }
                if (ImGui::InputText("##dllpath", dllPathA_, sizeof(dllPathA_))) {
                    dllPathW_ = UTF8ToWString(std::string(dllPathA_));
                }

                ImGui::SameLine(0.0f, 8.0f);
                if (ImGui::Button("浏览...")) {
                    std::wstring path;
                    if (OpenDllFileDialogW(path)) {
                        dllPathW_ = path;
                        std::string a = WStringToUTF8(dllPathW_);
                        std::snprintf(dllPathA_, sizeof(dllPathA_), "%s", a.c_str());
                    }
                }
            }


            void DrawInjectControls() {
                const char* methods[] = { "APC", "RtlThread" };
                ImGui::Text("注入方式:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(160);
                ImGui::Combo("##method", &method_, methods, IM_ARRAYSIZE(methods));

                const bool can = (pidInput_ > 0) && !dllPathW_.empty();
                if (!can) ImGui::BeginDisabled();

                if (ImGui::Button("注入", ImVec2(120, 0))) {
#ifdef _WIN32
                    BOOL ok = FALSE;
                    if (method_ == 0) ok = ApcInject((DWORD)pidInput_, dllPathW_.c_str());
                    else              ok = RtlThreadInject((DWORD)pidInput_, dllPathW_.c_str());

                    ImGui::OpenPopup("注入结果");
                    ImGui::SetNextWindowSize(ImVec2(220, 0));
                    if (ImGui::BeginPopupModal("注入结果", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("%s", ok ? "注入成功" : "注入失败");
                        ImGui::Separator();
                        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
#else
                    (void)methods;
#endif
                }

                if (!can) ImGui::EndDisabled();
            }

            void Draw() {
                SyncPidFromProviderOnce();

                DrawPidInput();
                DrawDllSelector();
                DrawInjectControls();

                if (pidInput_ <= 0) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("可手动输入 PID，或在 View/Processes 页面选中一个进程。");
                }
            }
        };

        std::unique_ptr<IModule> CreateInjector() { return std::make_unique<InjectorModule>(); }

    }
} // namespace
