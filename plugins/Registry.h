#pragma once
#include <memory>
#include <vector>
#include "IModule.h"

namespace REKit {
    namespace Plugins {

        class ModuleRegistry {
        public:
            void Register(std::unique_ptr<IModule> m) { modules_.emplace_back(std::move(m)); }
            void LoadAll(ModuleContext& ctx) { for (auto& m : modules_) m->OnLoad(ctx); }
            void UnloadAll(ModuleContext& ctx) { for (auto& m : modules_) m->OnUnload(ctx); }
        private:
            std::vector<std::unique_ptr<IModule>> modules_;
        };

        std::unique_ptr<IModule> CreateProcessExplorer();
        std::unique_ptr<IModule> CreateModuleEnum();
        std::unique_ptr<IModule> CreateMemSearch();
        std::unique_ptr<IModule> CreateInjector();
    }
} // namespace
