#include "UiRoot.h"
#include <algorithm>
#include "imgui/imgui.h"

namespace REKit {
    namespace UI {

        struct UiRoot::UiRegistryImpl : UiRoot::IUiRegistry {
            using PanelDrawFn = std::function<void()>;
            std::vector<std::pair<std::string, PanelDrawFn>> panels;
            void AddPanel(const char* menuPath, PanelDrawFn fn) override {
                panels.emplace_back(menuPath, std::move(fn));
            }
        };

        UiRoot::UiRoot() : ui_(new UiRegistryImpl{}) {}
        UiRoot::~UiRoot() { delete ui_; }
        UiRoot::IUiRegistry& UiRoot::GetUiRegistry() { return *ui_; }

        int UiRoot::FindOpenTab(const std::string& name) const {
            for (size_t i = 0; i < openTabs_.size(); ++i)
                if (openTabs_[i] == name) return (int)i;
            return -1;
        }

        void UiRoot::OpenTabByName(const std::string& name) {
            int idx = FindOpenTab(name);
            if (idx < 0) {
                openTabs_.push_back(name);
                activeTab_ = (int)openTabs_.size() - 1;
            }
            else {
                activeTab_ = idx;
            }
        }

        void UiRoot::CloseTabAt(size_t idx) {
            if (idx >= openTabs_.size()) return;
            openTabs_.erase(openTabs_.begin() + (long long)idx);
            if (openTabs_.empty()) { activeTab_ = -1; return; }
            if ((int)idx <= activeTab_) activeTab_ = std::max(0, activeTab_ - 1);
        }

        void UiRoot::Draw() {
            {
                ImGuiIO& io = ImGui::GetIO();

                if (!openTabs_.empty()) {
                    if (activeTab_ < 0) activeTab_ = 0;

                    const bool ctrl = io.KeyCtrl;
                    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Tab, /*repeat=*/false)) {
                        const int delta = io.KeyShift ? -1 : +1;
                        const int n = (int)openTabs_.size();
                        activeTab_ = (activeTab_ + delta + n) % n;
                    }
                    // 可选：支持 Ctrl+PageUp/Down
                    // if (ctrl && ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) activeTab_ = (activeTab_ + 1) % (int)openTabs_.size();
                    // if (ctrl && ImGui::IsKeyPressed(ImGuiKey_PageUp,   false)) activeTab_ = (activeTab_ - 1 + (int)openTabs_.size()) % (int)openTabs_.size();
                }
            }

            // Host a full-window "Workspace"……
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGuiWindowFlags hostFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoNav; // 避免该窗口参与导航

            if (ImGui::Begin("##WorkspaceHost", nullptr, hostFlags)) {
                if (mode_ == LayoutMode::Tabs) {
                    if (ImGui::BeginTabBar("MainTabBar",
                        ImGuiTabBarFlags_Reorderable |
                        ImGuiTabBarFlags_TabListPopupButton |
                        ImGuiTabBarFlags_AutoSelectNewTabs |
                        ImGuiTabBarFlags_FittingPolicyResizeDown |
                        ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
                    {
                        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
                            ImGui::OpenPopup("AddPanelPopup");

                        if (ImGui::BeginPopup("AddPanelPopup")) {
                            for (auto& p : ui_->panels)
                                if (ImGui::MenuItem(p.first.c_str()))
                                    OpenTabByName(p.first);
                            ImGui::EndPopup();
                        }

                        for (size_t i = 0; i < openTabs_.size(); ++i) {
                            bool open = true;
                            const std::string& name = openTabs_[i];

                            ImGuiTabItemFlags tab_flags =
                                (activeTab_ == (int)i) ? ImGuiTabItemFlags_SetSelected : 0;

                            if (ImGui::BeginTabItem(name.c_str(), &open, tab_flags)) {
                                activeTab_ = (int)i;
                                for (auto& p : ui_->panels) {
                                    if (p.first == name && p.second) { p.second(); break; }
                                }
                                ImGui::EndTabItem();
                            }
                            if (!open) { CloseTabAt(i); i = (i == 0) ? 0 : i - 1; }
                        }
                        ImGui::EndTabBar();
                    }
                }
                else {
                    for (size_t i = 0; i < openTabs_.size();) {
                        bool open = true;
                        const std::string& name = openTabs_[i];
                        if (ImGui::Begin(name.c_str(), &open)) {
                            for (auto& p : ui_->panels)
                                if (p.first == name && p.second) { p.second(); break; }
                        }
                        ImGui::End();
                        if (!open) CloseTabAt(i); else ++i;
                    }
                }
            }
            ImGui::End();
        }
    }
} // namespace
