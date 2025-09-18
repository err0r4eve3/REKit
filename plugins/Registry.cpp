#include "Registry.h"
#include <memory>
#include <utility>

namespace REKit { namespace Plugins {

//  Module.cpp 
    std::unique_ptr<IModule> CreateProcessExplorer();
    std::unique_ptr<IModule> CreateModuleEnum();
    std::unique_ptr<IModule> CreateMemSearch();
    std::unique_ptr<IModule> CreateInjector();
}} // namespace
