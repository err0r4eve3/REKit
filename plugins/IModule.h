#pragma once
#include <functional>

namespace REKit {
    namespace Plugins {

        struct ModuleContext {
            struct IUiRegistry {
                using PanelDrawFn = std::function<void()>;
                virtual void AddPanel(const char* menuPath, PanelDrawFn fn) = 0;
                virtual ~IUiRegistry() = default;
            };
            IUiRegistry& ui;
        };

        struct IModule {
            virtual ~IModule() = default;
            virtual const char* Name() const = 0;
            virtual void OnLoad(ModuleContext& ctx) = 0;
            virtual void OnUnload(ModuleContext& ctx) = 0;
        };

    }
} // namespace
