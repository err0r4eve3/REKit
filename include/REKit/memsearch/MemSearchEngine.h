#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <atomic>

namespace REKit { namespace MemSearch {
enum class ScanType { Bytes, Ascii, Utf16, Int32, Float, Double };

enum class CompareMode { Exact, Increased, Decreased, Changed, Unchanged };

struct ScanOptions {
    unsigned int pid = 0;
    uintptr_t base = 0;
    size_t    length = 0;
    bool      autoPages = true;     // use VirtualQueryEx to auto enumerate readable regions
    size_t    alignment = 1;
    ScanType  type = ScanType::Bytes;
    CompareMode cmp = CompareMode::Exact; // used for next-scan
    // inputs:
    std::string hexExpr;
    std::string strExpr;
    int         int32Val = 0;
    float       floatVal = 0.f;
    double      doubleVal = 0.0;
};

struct Region { uintptr_t base; size_t size; };

void StartFirstScan(const ScanOptions& opt,
                          std::vector<uintptr_t>& results,
                          std::atomic<bool>& cancel,
                          std::atomic<float>& progress,
                          std::string& status);
void StartNextScan(const ScanOptions& opt,
                         const std::vector<uintptr_t>& prev,
                         std::vector<uintptr_t>& results,
                         std::atomic<bool>& cancel,
                         std::atomic<float>& progress,
                         std::string& status);

}} // namespace
