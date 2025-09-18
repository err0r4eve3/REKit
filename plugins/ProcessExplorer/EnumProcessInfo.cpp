#include "../../include/EnumProcessInfo.h"

std::vector<ProcessInfo> g_processes;
std::mutex g_processesMutex;
std::atomic<bool> g_backgroundThreadRunning(false);
std::thread g_backgroundThread;

static std::wstring GetProcessImagePath(ULONG pid) {
    if (pid == 0) return L"";

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return L"";

    WCHAR path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
        CloseHandle(hProcess);
        return std::wstring(path);
    }

    CloseHandle(hProcess);
    return L"";
}

static bool EnumerateProcesses(std::vector<ProcessInfo>& processes) {
    processes.clear();

    HMODULE hNtDll = GetModuleHandle(L"ntdll.dll");
    if (!hNtDll) return FALSE;

    NTQUERYSYSTEMINFORMATION NtQuerySystemInformation =
        (NTQUERYSYSTEMINFORMATION)GetProcAddress(hNtDll, "NtQuerySystemInformation");
    if (!NtQuerySystemInformation) return FALSE;

    ULONG cbBuffer = 0x100000;
    PVOID pBuffer = malloc(cbBuffer);
    if (!pBuffer) return FALSE;

    NTSTATUS status;
    do {
        status = NtQuerySystemInformation(
            SystemProcessInformation,
            pBuffer,
            cbBuffer,
            NULL
        );
        if (status == STATUS_INFO_LENGTH_MISMATCH) {
            cbBuffer *= 2;
            pBuffer = realloc(pBuffer, cbBuffer);
            if (!pBuffer) {
				free(pBuffer);
                return FALSE;
            }
        }
        else {
            break;
        }
    } while (TRUE);

    if (status != 0) {
        free(pBuffer);
        return FALSE;
    }

    PSYSTEM_PROCESS_INFORMATION pInfo = (PSYSTEM_PROCESS_INFORMATION)pBuffer;
    while (pInfo) {
        ProcessInfo proc;
        if (pInfo->ImageName.Buffer && pInfo->ImageName.Length > 0) {
            proc.name = std::wstring(pInfo->ImageName.Buffer, pInfo->ImageName.Length / sizeof(WCHAR));
        }
        else {
            proc.name = L"(System Idle Process)";
        }

        proc.pid = (ULONG)(ULONG_PTR)pInfo->UniqueProcessId;
        proc.threads = pInfo->NumberOfThreads;
        proc.handles = pInfo->HandleCount;
        proc.sessionId = pInfo->SessionId;
        proc.basePriority = pInfo->BasePriority;

        proc.workingSet = pInfo->WorkingSetSize;
        proc.virtualSize = pInfo->VirtualSize;
        proc.privatePages = pInfo->PrivatePageCount;

        proc.createTime = pInfo->CreateTime;
        proc.userTime = pInfo->UserTime;
        proc.kernelTime = pInfo->KernelTime;

        proc.readTransferCount = pInfo->ReadTransferCount;
        proc.writeTransferCount = pInfo->WriteTransferCount;
        proc.otherTransferCount = pInfo->OtherTransferCount;
        //thread
        proc.imagePath = GetProcessImagePath(proc.pid);

        processes.push_back(proc);

        if (pInfo->NextEntryOffset == 0) break;
        pInfo = (PSYSTEM_PROCESS_INFORMATION)((BYTE*)pInfo + pInfo->NextEntryOffset);
    }

    free(pBuffer);
    return TRUE;
}

static void BackgroundProcessUpdateThread() {
    g_backgroundThreadRunning = true;
    while (g_backgroundThreadRunning) {
        std::vector<ProcessInfo> newProcesses;
        if (EnumerateProcesses(newProcesses)) {
            std::lock_guard<std::mutex> lock(g_processesMutex);
            g_processes = std::move(newProcesses);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void StartProcessMonitoring() {
    if (!g_backgroundThreadRunning.load()) {
        g_backgroundThread = std::thread(BackgroundProcessUpdateThread);
    }
}

void StopProcessMonitoring() {
    g_backgroundThreadRunning = false;
    if (g_backgroundThread.joinable()) {
        g_backgroundThread.join();
    }
}

std::vector<ProcessInfo> GetProcessList() {
    std::lock_guard<std::mutex> lock(g_processesMutex);
    return g_processes;
}

size_t GetProcessCount() {
    std::lock_guard<std::mutex> lock(g_processesMutex);
    return g_processes.size();
}

ProcessInfo* FindProcessByPID(ULONG pid) {
    std::lock_guard<std::mutex> lock(g_processesMutex);
    for (auto& proc : g_processes) {
        if (proc.pid == pid) {
            return &proc;
        }
    }
    return nullptr;
}

std::vector<ProcessInfo> FilterProcesses(const std::string& nameFilter,
    const std::string& pidFilter,
    const std::string& pathFilter) {
    std::vector<ProcessInfo> filtered;

    std::lock_guard<std::mutex> lock(g_processesMutex);

    for (const auto& proc : g_processes) {
        bool match = TRUE;

        if (!nameFilter.empty()) {
            std::string procName = WStringToString(proc.name);
            std::string filter = nameFilter;
            std::transform(procName.begin(), procName.end(), procName.begin(), ::tolower);
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
            if (procName.find(filter) == std::string::npos) {
                match = FALSE;
            }
        }

        if (match && !pidFilter.empty()) {
            ULONG filterPid = std::stoul(pidFilter);
            if (proc.pid != filterPid) {
                match = FALSE;
            }
        }

        if (match && !pathFilter.empty()) {
            std::string procPath = WStringToString(proc.imagePath);
            std::string filter = pathFilter;
            std::transform(procPath.begin(), procPath.end(), procPath.begin(), ::tolower);
            std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
            if (procPath.find(filter) == std::string::npos) {
                match = FALSE;
            }
        }

        if (match) {
            filtered.push_back(proc);
        }
    }

    return filtered;
}

void SortProcessList(std::vector<ProcessInfo>& processes, const ImGuiTableSortSpecs* sort_specs) {
    if (sort_specs->SpecsCount == 0) return;
    std::sort(processes.begin(), processes.end(),
        [sort_specs](const ProcessInfo& a, const ProcessInfo& b) -> bool {
            for (int n = 0; n < sort_specs->SpecsCount; n++) {
                const ImGuiTableColumnSortSpecs* sort_spec = &sort_specs->Specs[n];
                int delta = 0;
                switch (sort_spec->ColumnIndex) {
                case 0: // 
                    delta = _wcsicmp(a.name.c_str(), b.name.c_str());
                    break;
                case 1: // PID
                    delta = (a.pid < b.pid) ? -1 : (a.pid > b.pid) ? 1 : 0;
                    break;
                case 2: // 
                    delta = (a.threads < b.threads) ? -1 : (a.threads > b.threads) ? 1 : 0;
                    break;
                case 3: // 
                    delta = (a.handles < b.handles) ? -1 : (a.handles > b.handles) ? 1 : 0;
                    break;
                case 4: // 
                    delta = _wcsicmp(a.imagePath.c_str(), b.imagePath.c_str());
                    break;
                }
                if (delta != 0) {
                    return (sort_spec->SortDirection == ImGuiSortDirection_Ascending) ? (delta < 0) : (delta > 0);
                }
            }
            return FALSE;
        });
}