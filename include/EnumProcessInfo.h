#pragma once
#include <windows.h>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

#include "../include/ntapi.h"
#include "../include/application.h"
#include "../include/utils.h"

struct ProcessInfo {
    std::wstring name;
    std::wstring imagePath;
    ULONG pid;
    ULONG threads;
    ULONG handles;
    ULONG sessionId;
    SIZE_T workingSet;
    SIZE_T virtualSize;
    SIZE_T privatePages;
    LARGE_INTEGER createTime;
    KPRIORITY basePriority;
    LARGE_INTEGER userTime;
    LARGE_INTEGER kernelTime;
    LARGE_INTEGER readTransferCount;
    LARGE_INTEGER writeTransferCount;
    LARGE_INTEGER otherTransferCount;

    ProcessInfo() : pid(0), threads(0), handles(0), sessionId(0),
        workingSet(0), virtualSize(0), privatePages(0),
        basePriority(0) {
        createTime.QuadPart = 0;
        userTime.QuadPart = 0;
        kernelTime.QuadPart = 0;
        readTransferCount.QuadPart = 0;
        writeTransferCount.QuadPart = 0;
        otherTransferCount.QuadPart = 0;
    }
};

extern std::vector<ProcessInfo> g_processes;
extern std::mutex g_processesMutex;

std::wstring GetProcessImagePath(ULONG pid);
void StartProcessMonitoring();
void StopProcessMonitoring();
std::vector<ProcessInfo> GetProcessList();
size_t GetProcessCount();
ProcessInfo* FindProcessByPID(ULONG pid);
std::vector<ProcessInfo> FilterProcesses(const std::string& nameFilter, const std::string& pidFilter, const std::string& pathFilter);
void SortProcessList(std::vector<ProcessInfo>& processes, const ImGuiTableSortSpecs* sort_specs);
