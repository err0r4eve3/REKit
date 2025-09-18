#include <Windows.h>
#include <shellapi.h>
#include <string>
#include <memory>
#include <vector>
#include <algorithm>
#include <cstring>

#include "plugins/IModule.h"
#include "ui/UiRoot.h"
#include "imgui/imgui.h"
#include "include/SelectedPidProvider.h"
#include "include/ntapi.h"
#include "include/utils.h"
#include "include/EnumProcessInfo.h"
#include "include/injector.h"

extern char processKeyword[256];
extern int g_selectedProcess;
extern int g_selectedPid;
extern std::wstring g_dllPathW;
static std::vector<ProcessInfo> GetFilteredProcessList();

namespace REKit {
    namespace Plugins {

        class ProcessExplorerModule final : public IModule {
        public:
            const char* Name() const override { return "ProcessExplorer"; }
            void OnLoad(ModuleContext& ctx) override {
                ctx.ui.AddPanel("View/Processes", []() {
                    static bool s_provider_inited = false;
                    if (!s_provider_inited) {
                        SetSelectedPidProvider([]() -> int { return g_selectedPid; });
                        s_provider_inited = true;
                    }

                    if (ImGui::BeginTable("ProcessDetails", 2, ImGuiTableFlags_NoBordersInBody)) {
                        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("进程名:");

                        ImGui::TableNextColumn();
                        ImGui::InputTextWithHint("##processname",
                            "输入进程 名称/pid/路径 进行搜索",
                            processKeyword, sizeof(processKeyword));
                        ImGui::EndTable();
                    }

                    ImGui::Spacing();

                    static char lastKeyword[256] = "";
                    static ImGuiTableSortSpecs lastSortSpecs = {};
                    static bool hasSortSpecs = false;

                    if (std::strcmp(lastKeyword, processKeyword) != 0) {
                        strcpy_s(lastKeyword, processKeyword);
                    }

                    std::vector<ProcessInfo> filteredProcesses = GetFilteredProcessList();
                    std::vector<ProcessInfo> currentProcesses = GetProcessList();

                    ImGui::Text("显示: %zu / %zu 个进程",
                        filteredProcesses.size(), currentProcesses.size());
                    ImGui::Separator();

                    const bool emptyAfterFilter =
                        (std::strlen(processKeyword) > 0) && filteredProcesses.empty();

                    if (emptyAfterFilter) {
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "未找到匹配的进程");
                    }
                    else {
                        if (hasSortSpecs) {
                            SortProcessList(filteredProcesses, &lastSortSpecs);
                        }

                        if (ImGui::BeginTable("ProcessTable", 5,
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Hideable)) {

                            ImGui::TableSetupColumn("进程名",
                                ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 600.0f, 0);
                            ImGui::TableSetupColumn("PID",
                                ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 100.0f, 1);
                            ImGui::TableSetupColumn("线程数",
                                ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 100.0f, 2);
                            ImGui::TableSetupColumn("句柄数",
                                ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 100.0f, 3);
                            ImGui::TableSetupColumn("进程路径",
                                ImGuiTableColumnFlags_WidthStretch, 0.0f, 4);

                            ImGui::TableSetupScrollFreeze(0, 1);
                            ImGui::TableHeadersRow();

                            if (ImGuiTableSortSpecs* sort_specs = ImGui::TableGetSortSpecs()) {
                                if (sort_specs->SpecsDirty) {
                                    SortProcessList(filteredProcesses, sort_specs);
                                    lastSortSpecs = *sort_specs;
                                    hasSortSpecs = true;
                                    sort_specs->SpecsDirty = false;
                                }
                            }

                            for (int i = 0; i < (int)filteredProcesses.size(); ++i) {
                                const ProcessInfo& proc = filteredProcesses[i];

                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();

                                const bool is_selected = (g_selectedProcess == i);
                                std::string processName = WStringToString(proc.name);
                                std::string uniqueLabel = processName + "##" + std::to_string(proc.pid);

                                if (ImGui::Selectable(uniqueLabel.c_str(), is_selected,
                                    ImGuiSelectableFlags_SpanAllColumns)) {
                                    g_selectedProcess = i;
                                    g_selectedPid = (int)proc.pid;
                                }

                                if (ImGui::BeginPopupContextItem()) {
                                    ImGui::Text("进程: %s (PID: %lu)", processName.c_str(), proc.pid);
                                    ImGui::Separator();
                                    if (ImGui::MenuItem("结束进程")) {
                                        TerminateProcessByPID(proc.pid);
                                    }
                                    if (ImGui::MenuItem("打开文件位置")) {
                                        std::wstring wImagePath = proc.imagePath;
                                        if (!wImagePath.empty()) {
                                            ShellExecuteW(NULL, L"open", L"explorer",
                                                (L"/select,\"" + wImagePath + L"\"").c_str(),
                                                NULL, SW_SHOWNORMAL);
                                        }
                                    }
                                    ImGui::EndPopup();
                                }

                                ImGui::TableNextColumn(); ImGui::Text("%lu", proc.pid);
                                ImGui::TableNextColumn(); ImGui::Text("%lu", proc.threads);
                                ImGui::TableNextColumn(); ImGui::Text("%lu", proc.handles);
                                ImGui::TableNextColumn(); ImGui::Text("%s", WStringToString(proc.imagePath).c_str());
                            }

                            ImGui::EndTable();
                        }
                    }
                    });
            }

            void OnUnload(ModuleContext&) override {}
        };

        std::unique_ptr<IModule> CreateProcessExplorer() {
            return std::make_unique<ProcessExplorerModule>();
        }

    } // namespace Plugins
} // namespace REKit

char processKeyword[256] = "";
int g_selectedProcess = -1;
int g_selectedPid = 0;
std::wstring g_dllPathW = L"";

static std::vector<ProcessInfo> GetFilteredProcessList() {
    std::vector<ProcessInfo> currentProcesses = GetProcessList();
    std::vector<ProcessInfo> filteredProcesses;

    if (std::strlen(processKeyword) == 0) {
        filteredProcesses = currentProcesses;
    }
    else {
        std::string keyword = processKeyword;
        std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::tolower);

        for (const auto& proc : currentProcesses) {
            bool match = false;

            std::string name = WStringToString(proc.name);
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find(keyword) != std::string::npos) match = true;

            if (!match) {
                std::string pidStr = std::to_string(proc.pid);
                if (pidStr.find(keyword) != std::string::npos) match = true;
            }

            if (!match) {
                std::string path = WStringToString(proc.imagePath);
                std::transform(path.begin(), path.end(), path.begin(), ::tolower);
                if (path.find(keyword) != std::string::npos) match = true;
            }

            if (match) filteredProcesses.push_back(proc);
        }
    }
    return filteredProcesses;
}
