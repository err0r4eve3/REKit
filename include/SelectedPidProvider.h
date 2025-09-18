#pragma once
// Lightweight selected PID provider.
// Your ProcessExplorer can set a provider function so other plugins can read the current PID.
// Usage from ProcessExplorer (when selection changes):
//   SetSelectedPidProvider([](){ return current_selected_pid; });
//
// If no provider is set, plugins may fall back to a manual PID input.

#include <functional>

inline int REKit_GetSelectedPid() {
    // default stub: no selection
    return 0;
}

// Allow setting a custom provider at runtime.
inline std::function<int(void)>& REKit_SelectedPidProvider() {
    static std::function<int(void)> prov;
    return prov;
}

inline void SetSelectedPidProvider(std::function<int(void)> prov) {
    REKit_SelectedPidProvider() = std::move(prov);
}

inline int GetSelectedPidOrFallback(int fallbackPid) {
    auto &p = REKit_SelectedPidProvider();
    if (p) return p();
    return fallbackPid;
}
