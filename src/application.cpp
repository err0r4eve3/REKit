#include "../include/application.h"

char app::processKeyword[256] = "";
int app::g_selectedProcess = -1;
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

void app::RenderDllSelectorW() {
    if (ImGui::BeginTable("DllSelector", 2, ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("DLL路径:");

        ImGui::TableNextColumn();

        static std::string utf8Path;
        utf8Path = WStringToUTF8(app::g_dllPathW);

        static char pathBuffer[512] = "";

        if (!utf8Path.empty() && strlen(pathBuffer) == 0) {
            strcpy_s(pathBuffer, sizeof(pathBuffer), utf8Path.c_str());
        }

        ImGui::SetNextItemWidth(-80.0f);

        if (ImGui::InputTextWithHint("##DllPathW", "选择要注入的DLL文件", pathBuffer, sizeof(pathBuffer))) {
            std::string inputStr = pathBuffer;
            app::g_dllPathW = UTF8ToWString(inputStr);
        }

        ImGui::SameLine();

        if (ImGui::Button("浏览...")) {
            std::wstring selectedPathW;
            if (OpenDllFileDialogW(selectedPathW)) {
                app::g_dllPathW = selectedPathW;
                std::string newUtf8Path = WStringToUTF8(selectedPathW);
                strcpy_s(pathBuffer, sizeof(pathBuffer), newUtf8Path.c_str());
            }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();

        const char* injectionMethods[] = {
            "APC注入",
            "RtlThread注入"
        };

        static int currentMethod = 0;

        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("##InjectionMethod", &currentMethod, injectionMethods, IM_ARRAYSIZE(injectionMethods));

        ImGui::SameLine();

        std::vector<ProcessInfo> filteredProcesses = GetFilteredProcessList();

        bool canInject = !app::g_dllPathW.empty() &&
            app::g_selectedProcess >= 0 &&
            app::g_selectedProcess < filteredProcesses.size();

        if (!canInject) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("注入DLL")) {
            if (canInject) {
                DWORD targetPID = filteredProcesses[app::g_selectedProcess].pid;

                if (currentMethod == 0) {
                    ApcInject(targetPID, app::g_dllPathW.c_str());
                }
                else if (currentMethod == 1) {
                    RtlThreadInject(targetPID, app::g_dllPathW.c_str());
                }
            }
        }

        if (!canInject) {
            ImGui::EndDisabled();
        }

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

        ImGui::TableSetupColumn("进程名", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 180.0f, 0);
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 80.0f, 1);
        ImGui::TableSetupColumn("线程数", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 80.0f, 2);
        ImGui::TableSetupColumn("句柄数", ImGuiTableColumnFlags_PreferSortDescending | ImGuiTableColumnFlags_WidthFixed, 80.0f, 3);
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

void app::RenderUI() {
    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoScrollbar;

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::Begin("Full Screen Window", nullptr, window_flags);

    app::RenderProcessFilter();

    ImGui::Spacing();

    app::RenderDllSelectorW();

    ImGui::Spacing();

    app::RenderProcessList();

    ImGui::End();
}