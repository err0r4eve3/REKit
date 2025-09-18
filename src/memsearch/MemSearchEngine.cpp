#include <vector>
#include <string>
#include <atomic>
#include <algorithm>
#include <sstream>
#include <iomanip>
#ifdef _WIN32
#include <windows.h>
#endif

#include <memory>
#include <thread>
#include <mutex>
#include <cctype>
#include <cstring>

#include "include/REKit/memsearch/MemSearchEngine.h"
#include "plugins/IModule.h"
#include "ui/UiRoot.h"
#include "imgui/imgui.h"
#include "include/SelectedPidProvider.h"


namespace REKit { namespace MemSearch {

// Utility: parse hex with optional spaces and '?' wildcards into pattern+mask.
static bool ParseHexWithMask(const std::string& src, std::vector<uint8_t>& pat, std::vector<uint8_t>& mask) {
    pat.clear(); mask.clear();
    std::string s;
    s.reserve(src.size());
    for (char c : src) { if (!isspace((unsigned char)c)) s.push_back(c); }
    if (s.size() == 0) return false;
    if (s.size() % 2 != 0) return false;
    for (size_t i = 0; i < s.size(); i += 2) {
        auto cvt = [](char c)->int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            if (c == '?') return -1;
            return -2;
        };
        int hi = cvt(s[i]);
        int lo = cvt(s[i+1]);
        if (hi == -2 || lo == -2) return false;
        uint8_t m = 0xFF, v = 0;
        if (hi >= 0) { v = (uint8_t)(hi << 4); } else { m &= 0x0F; }
        if (lo >= 0) { v |= (uint8_t)lo; }      else { m &= 0xF0; }
        pat.push_back(v);
        mask.push_back(m);
    }
    return true;
}

// scan modes
static void EnumReadableRegions(HANDLE h, std::vector<Region>& out, uintptr_t clipBase, uintptr_t clipEnd) {
#ifdef _WIN32
    out.clear();
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t cur = 0;
    while (VirtualQueryEx(h, (LPCVOID)cur, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        uintptr_t rb = (uintptr_t)mbi.BaseAddress;
        size_t    rs = (size_t)mbi.RegionSize;
        uintptr_t re = rb + rs;
        bool committed = (mbi.State == MEM_COMMIT);
        bool readable =
            (mbi.Protect & (PAGE_READONLY|PAGE_READWRITE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE)) != 0
            && !(mbi.Protect & (PAGE_GUARD));
        if (committed && readable) {
            uintptr_t b = rb, e = re;
            if (clipEnd > clipBase) {
                if (e <= clipBase || b >= clipEnd) { /*skip*/ }
                else {
                    b = (std::max)(b, clipBase); e = (std::min)(e, clipEnd);
                    if (e > b) out.push_back({ b, (size_t)(e - b) });
                }
            } else {
                out.push_back({ b, (size_t)(e - b) });
            }
        }
        cur = re;
        if (cur < rb) break; // overflow safety
    }
#else
    (void)h; (void)out; (void)clipBase; (void)clipEnd;
#endif
}

// BMH for bytes with mask and alignment
static void SearchBufferMasked(const uint8_t* buf, size_t n, const uint8_t* pat, const uint8_t* mask, size_t m, size_t alignment, uintptr_t baseAddr, std::vector<uintptr_t>& out) {
    if (m == 0 || n < m) return;
    size_t i = 0;
    const size_t step = (alignment > 0 ? alignment : 1);
    // naive masked compare; can be optimized with skip table if needed
    for (; i + m <= n; i += step) {
        size_t k = 0;
        for (; k < m; ++k) {
            if ((buf[i+k] & mask[k]) != (pat[k] & mask[k])) break;
        }
        if (k == m) out.push_back(baseAddr + i);
    }
}

static void SearchBufferValue(const uint8_t* buf, size_t n, ScanType t, const ScanOptions& opt, uintptr_t baseAddr, std::vector<uintptr_t>& out) {
    size_t step = (opt.alignment > 0 ? opt.alignment : 1);
    if (t == ScanType::Int32) {
        for (size_t i=0; i + sizeof(int32_t) <= n; i += step) {
            int32_t v; memcpy(&v, buf + i, sizeof(v));
            if (v == opt.int32Val) out.push_back(baseAddr + i);
        }
    } else if (t == ScanType::Float) {
        for (size_t i=0; i + sizeof(float) <= n; i += step) {
            float v; memcpy(&v, buf + i, sizeof(v));
            if (v == opt.floatVal) out.push_back(baseAddr + i);
        }
    } else if (t == ScanType::Double) {
        for (size_t i=0; i + sizeof(double) <= n; i += step) {
            double v; memcpy(&v, buf + i, sizeof(v));
            if (v == opt.doubleVal) out.push_back(baseAddr + i);
        }
    } else if (t == ScanType::Ascii) {
        const std::string& s = opt.strExpr;
        if (s.empty()) return;
        size_t m = s.size();
        for (size_t i=0; i + m <= n; i += step) {
            if (memcmp(buf + i, s.data(), m) == 0) out.push_back(baseAddr + i);
        }
    } else if (t == ScanType::Utf16) {
        // naive UTF-16LE match
        const std::u16string s16((const char16_t*)opt.strExpr.c_str(), (const char16_t*)(opt.strExpr.c_str()+opt.strExpr.size()));
        const uint8_t* pat = reinterpret_cast<const uint8_t*>(s16.data());
        size_t m = s16.size() * sizeof(char16_t);
        for (size_t i=0; i + m <= n; i += step) {
            if (memcmp(buf + i, pat, m) == 0) out.push_back(baseAddr + i);
        }
    }
}
    void StartFirstScan(const ScanOptions& opt, std::vector<uintptr_t>& results, std::atomic<bool>& cancel, std::atomic<float>& progress, std::string& status) {

#ifdef _WIN32
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, (DWORD)opt.pid);
        if (!h) { status = "OpenProcess failed"; return; }
        std::vector<Region> regs;
        if (opt.autoPages) {
            uintptr_t end = 0;
            if (opt.length > 0) end = opt.base + opt.length;
            EnumReadableRegions(h, regs, opt.length > 0 ? opt.base : 0, end);
        }
        else {
            if (opt.length == 0) { status = "Length is zero"; CloseHandle(h); return; }
            regs.push_back({ opt.base, opt.length });
        }
        if (regs.empty()) { status = "No readable regions"; CloseHandle(h); return; }

        std::vector<uint8_t> pat, mask;
        if (opt.type == ScanType::Bytes) {
            if (!ParseHexWithMask(opt.hexExpr, pat, mask)) { status = "Invalid hex pattern"; CloseHandle(h); return; }
        }
        status = "Scanning...";
        progress = 0.f;

        const size_t chunk = 1 << 16; // 64KB
        size_t total = 0, done = 0;
        for (auto& r : regs) total += r.size;

        std::vector<uint8_t> buf; buf.resize(chunk + 64);
        for (auto& r : regs) {
            if (cancel) break;
            uintptr_t cur = r.base;
            uintptr_t end = r.base + r.size;
            while (cur < end) {
                if (cancel) break;
                size_t toRead = (size_t)std::min<uintptr_t>(chunk, end - cur);
                SIZE_T br = 0;
                if (!ReadProcessMemory(h, (LPCVOID)cur, buf.data(), toRead, &br)) {
                    cur += toRead;
                    done += toRead;
                    progress = (float)done / (float)total;
                    continue;
                }
                if (br > 0) {
                    if (opt.type == ScanType::Bytes) {
                        SearchBufferMasked(buf.data(), (size_t)br, pat.data(), mask.data(), pat.size(), opt.alignment, cur, results);
                    }
                    else {
                        SearchBufferValue(buf.data(), (size_t)br, opt.type, opt, cur, results);
                    }
                }
                cur += br;
                done += (size_t)br;
                progress = (float)done / (float)total;
            }
        }
        CloseHandle(h);
        status = cancel ? "Canceled" : "Done";
#else
        status = "Windows only";
#endif
}

void StartNextScan(const ScanOptions& opt, const std::vector<uintptr_t>& prev, std::vector<uintptr_t>& results, std::atomic<bool>& cancel, std::atomic<float>& progress, std::string& status) {

#ifdef _WIN32
        HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, (DWORD)opt.pid);
        if (!h) { status = "OpenProcess failed"; return; }
        status = "Filtering...";
        progress.store(0.0f);
        size_t total = prev.size(), done = 0;

        // For Increased/Decreased/Changed/Unchanged, we need a previous snapshot of values.
        // Minimal implementation: treat "Exact" as re-check equals; others fallback to "Changed" by re-read and compare cached map.
        // Here we keep it simple and do Exact re-check; extend as you wish.

        size_t valueSize = 1;
        if (opt.type == ScanType::Int32) valueSize = sizeof(int32_t);
        else if (opt.type == ScanType::Float) valueSize = sizeof(float);
        else if (opt.type == ScanType::Double) valueSize = sizeof(double);
        else if (opt.type == ScanType::Ascii) valueSize = opt.strExpr.size();
        else if (opt.type == ScanType::Utf16) valueSize = opt.strExpr.size()*2;
        // Bytes pattern
        std::vector<uint8_t> pat, mask;
        if (opt.type == ScanType::Bytes) { ParseHexWithMask(opt.hexExpr, pat, mask); valueSize = pat.size(); }

        std::vector<uint8_t> buf; buf.resize(std::max<size_t>(valueSize, 16));
        for (auto addr : prev) {
            if (cancel) break;
            SIZE_T br = 0;
            if (!ReadProcessMemory(h, (LPCVOID)addr, buf.data(), valueSize, &br) || br < valueSize) {
                done++; progress.store((float)done / (float)total); continue;
            }
            bool keep = false;
            if (opt.type == ScanType::Bytes) {
                keep = true;
                for (size_t k=0;k<pat.size();++k) {
                    if ((buf[k] & mask[k]) != (pat[k] & mask[k])) { keep = false; break; }
                }
            } else if (opt.type == ScanType::Ascii) {
                keep = (memcmp(buf.data(), opt.strExpr.data(), valueSize) == 0);
            } else if (opt.type == ScanType::Utf16) {
                // naive widen check
                std::vector<uint8_t> s; s.resize(valueSize);
                for (size_t i=0;i<opt.strExpr.size();++i){ s[i*2] = (uint8_t)opt.strExpr[i]; s[i*2+1] = 0; }
                keep = (memcmp(buf.data(), s.data(), valueSize) == 0);
            } else if (opt.type == ScanType::Int32) {
                int32_t v; memcpy(&v, buf.data(), sizeof(v));
                keep = (opt.cmp == CompareMode::Exact) ? (v == opt.int32Val) : true;
            } else if (opt.type == ScanType::Float) {
                float v; memcpy(&v, buf.data(), sizeof(v));
                keep = (opt.cmp == CompareMode::Exact) ? (v == opt.floatVal) : true;
            } else if (opt.type == ScanType::Double) {
                double v; memcpy(&v, buf.data(), sizeof(v));
                keep = (opt.cmp == CompareMode::Exact) ? (v == opt.doubleVal) : true;
            }
            if (keep) results.push_back(addr);
            done++; progress = (float)done / (float)total;
        }
        CloseHandle(h);
        status = cancel ? "Canceled" : "Filtered";
#else
        status = "Windows only";
#endif
    
}

} } // namespace
