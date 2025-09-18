#pragma once
#include <functional>
#include <string>
#include <vector>
#include <set>

namespace REKit {
    namespace UI {

        class UiRoot {
        public:
            struct IUiRegistry {
                using PanelDrawFn = std::function<void()>;
                virtual void AddPanel(const char* menuPath, PanelDrawFn fn) = 0;
                virtual ~IUiRegistry() = default;
            };

            enum class LayoutMode { Windows, Tabs };
            void SetLayoutMode(LayoutMode m) { mode_ = m; }

            UiRoot();
            ~UiRoot();

            IUiRegistry& GetUiRegistry();
            void Draw();

        private:
            struct UiRegistryImpl;
            UiRegistryImpl* ui_;                // stores (name, draw-fn)

            std::vector<std::string> openTabs_; // opened tabs in order
            int  activeTab_ = -1;               // active tab index
            LayoutMode mode_ = LayoutMode::Tabs;

            int  FindOpenTab(const std::string& name) const;
            void OpenTabByName(const std::string& name);
            void CloseTabAt(size_t idx);
        };

    }
} // namespace
