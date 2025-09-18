#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <iomanip>

#include "plugins/IModule.h"
#include "ui/UiRoot.h"
#include "imgui/imgui.h"
#include "include/SelectedPidProvider.h"

#ifdef _WIN32
#  include <windows.h>
#endif

#include "include/REKit/memsearch/MemSearchEngine.h"

namespace REKit { namespace Plugins {
    using ScanType = REKit::MemSearch::ScanType;
    using CompareMode = REKit::MemSearch::CompareMode;
    using ScanOptions = REKit::MemSearch::ScanOptions;

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

struct Region { uintptr_t base; size_t size; };

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
        if (cur < rb) break;
    }
#else
    (void)h; (void)out; (void)clipBase; (void)clipEnd;
#endif
}

static void SearchBufferMasked(const uint8_t* buf, size_t n, const uint8_t* pat, const uint8_t* mask, size_t m, size_t alignment, uintptr_t baseAddr, std::vector<uintptr_t>& out) {
    if (m == 0 || n < m) return;
    size_t i = 0;
    const size_t step = (alignment > 0 ? alignment : 1);
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
        const std::u16string s16((const char16_t*)opt.strExpr.c_str(), (const char16_t*)(opt.strExpr.c_str()+opt.strExpr.size()));
        const uint8_t* pat = reinterpret_cast<const uint8_t*>(s16.data());
        size_t m = s16.size() * sizeof(char16_t);
        for (size_t i=0; i + m <= n; i += step) {
            if (memcmp(buf + i, pat, m) == 0) out.push_back(baseAddr + i);
        }
    }
}

class MemSearchModule final : public IModule {
public:
    const char* Name() const override { return "MemSearch"; }

    void OnLoad(ModuleContext& ctx) override {
        ctx.ui.AddPanel("View/Memory Search", [this](){
            DrawUI();
        });
    }
    void OnUnload(ModuleContext&) override {}

private:
    std::vector<uintptr_t> results_;
    std::vector<uintptr_t> prevResults_;
    std::atomic<bool> cancel_{false};
    std::atomic<float> progress_ = 0.f;
    std::string status_;

    ScanOptions opt_;
    char hexBuf_[512] = {0};
    char strBuf_[256] = {0};
    char baseBuf_[64] = {0};
    char lenBuf_[64]  = {0};

    void DrawUI() {
        int selPid = GetSelectedPidOrFallback((int)opt_.pid);
        ImGui::Text("Selected PID: %d", selPid);
        ImGui::InputInt("Manual PID (fallback)", (int*)&opt_.pid);

        ImGui::Checkbox("Auto pages", &opt_.autoPages);
        ImGui::SameLine();
        ImGui::InputText("Base (hex)", baseBuf_, sizeof(baseBuf_));
        ImGui::SameLine();
        ImGui::InputText("Length (hex)", lenBuf_, sizeof(lenBuf_));

        ImGui::InputScalar("Alignment", ImGuiDataType_U64, &opt_.alignment);
        const char* types[] = {"Bytes","ASCII","UTF-16LE","Int32","Float","Double"};
        int t = (int)opt_.type;
        if (ImGui::Combo("Type", &t, types, IM_ARRAYSIZE(types))) {
            opt_.type = (ScanType)t;
        }

        if (opt_.type == ScanType::Bytes) {
            ImGui::InputText("Hex pattern", hexBuf_, sizeof(hexBuf_));
            ImGui::SameLine(); ImGui::TextDisabled("(supports space and '?')");
        } else if (opt_.type == ScanType::Ascii) {
            ImGui::InputText("String (ASCII/UTF-8)", strBuf_, sizeof(strBuf_));
        } else if (opt_.type == ScanType::Utf16) {
            ImGui::InputText("String (UTF-16 text)", strBuf_, sizeof(strBuf_));
        } else if (opt_.type == ScanType::Int32) {
            ImGui::InputInt("Value (int32)", &opt_.int32Val);
        } else if (opt_.type == ScanType::Float) {
            ImGui::InputFloat("Value (float)", &opt_.floatVal);
        } else if (opt_.type == ScanType::Double) {
            ImGui::InputDouble("Value (double)", &opt_.doubleVal);
        }

        const char* cmps[] = {"Exact","Increased","Decreased","Changed","Unchanged"};
        int c = (int)opt_.cmp;
        ImGui::Combo("Compare mode (next scan)", &c, cmps, IM_ARRAYSIZE(cmps));
        opt_.cmp = (CompareMode)c;

        // buttons
        if (ImGui::Button("First Scan")) {
            cancel_ = false;
            results_.clear();
            prevResults_.clear();
            PrepareOptions(selPid);
            LaunchFirstScan();
        }
        ImGui::SameLine();
        if (ImGui::Button("Next Scan")) {
            cancel_ = false;
            if (!results_.empty()) {
                prevResults_ = results_;
                results_.clear();
                PrepareOptions(selPid);
                LaunchNextScan();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            cancel_ = true;
        }

        ImGui::Text("Status: %s", status_.c_str());
        ImGui::ProgressBar(progress_.load(), ImVec2(-FLT_MIN, 0.0f));

        ImGui::Separator();
        ImGui::Text("Results: %d", (int)results_.size());
        ImGui::BeginChild("res", ImVec2(0, 200), true);
        for (auto addr : results_) {
            char line[64];
            snprintf(line, sizeof(line), "0x%p", (void*)addr);
            if (ImGui::Selectable(line)) {
#ifdef _WIN32
                HANDLE h = OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION, FALSE, (DWORD)selPid);
                unsigned char buf[64] = {0};
                SIZE_T br = 0;
                if (h && ReadProcessMemory(h, (LPCVOID)addr, buf, sizeof(buf), &br)) {
                    std::string ascii;
                    ascii.reserve(br);
                    for (size_t i=0;i<br;++i){
                        char ch = (char)buf[i];
                        ascii.push_back((ch>=32 && ch<127)? ch : '.');
                    }
                    status_ = "Preview: " + ascii;
                } else {
                    status_ = "Preview failed";
                }
                if (h) CloseHandle(h);
#endif
            }
        }
        ImGui::EndChild();
    }

    void PrepareOptions(int selPid) {
        if (selPid > 0) opt_.pid = (unsigned)selPid;
        opt_.base = 0; opt_.length = 0;
        {
            std::stringstream ss; ss << std::hex << baseBuf_; ss >> opt_.base;
        }
        {
            std::stringstream ss; ss << std::hex << lenBuf_;  ss >> opt_.length;
        }
        opt_.hexExpr = hexBuf_;
        opt_.strExpr = strBuf_;
    }

    void LaunchFirstScan() {
        status_ = "Scanning...";
        progress_.store(0.0f);
        REKit::MemSearch::StartFirstScan(opt_, results_, cancel_, progress_, status_);
    }

    void LaunchNextScan() {
        status_ = "Filtering...";
        progress_.store(0.0f);
        REKit::MemSearch::StartNextScan(opt_, prevResults_, results_, cancel_, progress_, status_);
    }
};

std::unique_ptr<IModule> CreateMemSearch() { return std::make_unique<MemSearchModule>(); }

}} // namespace
