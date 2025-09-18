#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "plugins/IModule.h"
#include "ui/UiRoot.h"
#include "imgui/imgui.h"
#include "include/SelectedPidProvider.h"

#ifdef _WIN32
#  include <windows.h>
#  include <tlhelp32.h>
#endif

namespace REKit { namespace Plugins {

    static std::string Utf8FromWide(const wchar_t* w) {
#ifdef _WIN32
        if (!w) return {};
        int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string s; s.resize(n - 1);
        ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
        return s;
#else
        return {};
#endif
    }

    struct ModRow {
        uintptr_t base = 0;
        size_t    size = 0;
        std::string name;
        std::string path;
    };

    static std::vector<ModRow> EnumModules(unsigned int pid) {
        std::vector<ModRow> out;
#ifdef _WIN32
        HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, (DWORD)pid);
        if (snap == INVALID_HANDLE_VALUE) return out;

        MODULEENTRY32W me;
        me.dwSize = sizeof(me);
        if (::Module32FirstW(snap, &me)) {
            do {
                ModRow r;
                r.base = (uintptr_t)me.modBaseAddr;
                r.size = (size_t)me.modBaseSize;
                r.name = Utf8FromWide(me.szModule);
                r.path = Utf8FromWide(me.szExePath);
                out.push_back(std::move(r));
            } while (::Module32NextW(snap, &me));
        }
        ::CloseHandle(snap);
#endif
        return out;
    }

class ModuleEnumModule final : public IModule {
public:
    const char* Name() const override { return "ModuleEnum"; }
    void OnLoad(ModuleContext& ctx) override {
        ctx.ui.AddPanel("View/Modules", [](){
            ImGui::TextUnformatted("Modules of selected process");
            static int manualPid = 0;
            int pid = GetSelectedPidOrFallback(manualPid);
            ImGui::InputInt("Manual PID (fallback)", &manualPid);
            ImGui::SameLine();
            ImGui::Text("Selected PID: %d", pid);
            if (pid <= 0) {
                ImGui::TextColored(ImVec4(1,0.6f,0,1), "Select a process in ProcessExplorer or input PID.");
                return;
            }

            static int lastPid = 0;
            static std::vector<ModRow> rows;
            if (lastPid != pid) {
                rows = EnumModules((unsigned int)pid);
                lastPid = pid;
            }

            ImGui::Separator();
            ImGui::Text("Count: %d", (int)rows.size());

            ImGuiTableFlags flags =
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersV |
                ImGuiTableFlags_BordersOuter |
                ImGuiTableFlags_NoHostExtendX;
            if (ImGui::BeginTable("modtbl", 4, flags, ImVec2(0,0))) {
                ImGui::TableSetupScrollFreeze(0,1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                ImGui::TableSetupColumn("Base", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 0.65f);
                ImGui::TableHeadersRow();
                char buf[64];
                for (auto &r : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(r.name.c_str());
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", r.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    snprintf(buf, sizeof(buf), "0x%p", (void*)r.base);
                    ImGui::TextUnformatted(buf);

                    ImGui::TableSetColumnIndex(2);
                    snprintf(buf, sizeof(buf), "%zu", r.size);
                    ImGui::TextUnformatted(buf);

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(r.path.c_str());
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", r.path.c_str());
                }
                ImGui::EndTable();
            }
        });
    }
    void OnUnload(ModuleContext&) override {}
};

std::unique_ptr<IModule> CreateModuleEnum() { return std::make_unique<ModuleEnumModule>(); }

}} // namespace
