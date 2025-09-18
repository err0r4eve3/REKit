#include "../../include/SelectedPidProvider.h"
#include "../../include/EnumProcessInfo.h"
#include "../../include/utils.h"
#include "imgui/imgui.h"
#include <vector>
#include <string>
﻿#include "../../include/SelectedPidProvider.h"

char app::processKeyword[256] = "";
int app::g_selectedProcess = -1;
int app::g_selectedPid = 0;
std::wstring app::g_dllPathW = L"";

static std::vector<ProcessInfo> GetFilteredProcessList() {
    std::vector<ProcessInfo> currentProcesses = GetProcessList();
    std::vector<ProcessInfo> filteredProcesses;

    if (strlen(app::processKeyword) == 0) {
        filteredProcesses = currentProcesses;
    }
    else {
        std::string keyword = app::processKeyword;
        std::transform(keyword.begin(), keyword.end(), keyword.begin(), ::tolower);
        for (const auto& proc : currentProcesses) {
            bool match = false;
            std::string processName = WStringToString(proc.name);
            std::transform(processName.begin(), processName.end(), processName.begin(), ::tolower);
            if (processName.find(keyword) != std::string::npos) match = true;
            if (!match) {
                std::string pidStr = std::to_string(proc.pid);
                if (pidStr.find(keyword) != std::string::npos) match = true;
            }
            if (!match) {
                std::string imagePath = WStringToString(proc.imagePath);
                std::transform(imagePath.begin(), imagePath.end(), imagePath.begin(), ::tolower);
                if (imagePath.find(keyword) != std::string::npos) match = true;
            }
            if (match) filteredProcesses.push_back(proc);
        }
    }

    return filteredProcesses;
}

void app::RenderProcessFilter() {
    if (ImGui::BeginTable("ProcessDetails", 2, ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("进程名:");

        ImGui::TableNextColumn();
        ImGui::InputTextWithHint("##processname", "输入进程 名称/pid/路径 进行搜索", app::processKeyword, sizeof(processKeyword));

        ImGui::EndTable();
    }
}

void app::RenderProcessList() {
    static char lastKeyword[256] = "";
    static ImGuiTableSortSpecs lastSortSpecs = {};
    static bool hasSortSpecs = false;

    bool filterChanged = false;
    if (strcmp(lastKeyword, app::processKeyword) != 0) {
        strcpy_s(lastKeyword, sizeof(lastKeyword), app::processKeyword);
        filterChanged = true;
    }

    std::vector<ProcessInfo> filteredProcesses = GetFilteredProcessList();
    std::vector<ProcessInfo> currentProcesses = GetProcessList();

    if (hasSortSpecs) {
        SortProcessList(filteredProcesses, &lastSortSpecs);
    }

    ImGui::Text("显示: %zu / %zu 个进程", filteredProcesses.size(), currentProcesses.size());
    ImGui::Separator();

    if (strlen(app::processKeyword) > 0 && filteredProcesses.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "未找到匹配的进程");
        return;
    }

    if (ImGui::BeginTable("ProcessTable", 5,
        ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Hideable)) {

        ImGui::TableSetupColumn("进程名", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 600.0f, 0);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 100.0f, 1);
        ImGui::TableSetupColumn("线程数", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 100.0f, 2);
        ImGui::TableSetupColumn("句柄数", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 100.0f, 3);
        ImGui::TableSetupColumn("进程路径", ImGuiTableColumnFlags_WidthStretch, 0.0f, 4);

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

        for (int i = 0; i < filteredProcesses.size(); i++) {
            const ProcessInfo& proc = filteredProcesses[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            bool is_selected = (g_selectedProcess == i);
            std::string processName = WStringToString(proc.name);
            std::string uniqueLabel = processName + "##" + std::to_string(proc.pid);

            ImGui::PushID(static_cast<int>(proc.pid));

            if (ImGui::Selectable(uniqueLabel.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
                g_selectedProcess = i;
                g_selectedPid = proc.pid;
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

            ImGui::PopID();

            ImGui::TableNextColumn();
            ImGui::Text("%lu", proc.pid);
            ImGui::TableNextColumn();
            ImGui::Text("%lu", proc.threads);
            ImGui::TableNextColumn();
            ImGui::Text("%lu", proc.handles);
            ImGui::TableNextColumn();
            ImGui::Text("%s", WStringToString(proc.imagePath).c_str());
        }

        ImGui::EndTable();
    }
}

void app::RenderUIContent() {
    static bool s_provider_inited = false;
    if (!s_provider_inited) { SetSelectedPidProvider([]() -> int { return app::g_selectedPid; }); s_provider_inited = true; }

    app::RenderProcessFilter();

    ImGui::Spacing();

    app::RenderProcessList();
}

void app::RenderUI() {
    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoScrollbar;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    if (ImGui::Begin("Processes")) {
        RenderUIContent();
    }
    ImGui::End();

}