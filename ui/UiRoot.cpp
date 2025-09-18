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
            // Host a full-window "Workspace" that fills the main viewport work area
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGuiWindowFlags hostFlags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings;

            if (ImGui::Begin("##WorkspaceHost", nullptr, hostFlags)) {
                if (mode_ == LayoutMode::Tabs) {
                    if (ImGui::BeginTabBar("MainTabBar",
                        ImGuiTabBarFlags_Reorderable |
                        ImGuiTabBarFlags_TabListPopupButton |
                        ImGuiTabBarFlags_AutoSelectNewTabs |
                        ImGuiTabBarFlags_FittingPolicyResizeDown |
                        ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
                    {
                        // Trailing "+" to add a tab
                        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
                            ImGui::OpenPopup("AddPanelPopup");

                        if (ImGui::BeginPopup("AddPanelPopup")) {
                            for (auto& p : ui_->panels)
                                if (ImGui::MenuItem(p.first.c_str()))
                                    OpenTabByName(p.first);
                            ImGui::EndPopup();
                        }

                        // Render each open tab
                        for (size_t i = 0; i < openTabs_.size(); ++i) {
                            bool open = true; // passing &open shows the "x" close button
                            const std::string& name = openTabs_[i];
                            if (ImGui::BeginTabItem(name.c_str(), &open)) {
                                activeTab_ = (int)i;
                                // draw the content-only callback
                                for (auto& p : ui_->panels) {
                                    if (p.first == name && p.second) { p.second(); break; }
                                }
                                ImGui::EndTabItem();
                            }
                            if (!open) { // user clicked close 'x'
                                CloseTabAt(i);
                                i = (i == 0) ? 0 : i - 1;
                            }
                        }

                        ImGui::EndTabBar();
                    }
                }
                else {
                    // Legacy: each panel in its own floating window
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
            ImGui::End(); // Workspace host window
        }

    }
} // namespace
