#pragma once
#include "../imgui/imgui.h"
#include "../include/EnumProcessInfo.h"
#include "../include/utils.h"
#include "../include/injector.h"

namespace app {
	void RenderUI();

	static void RenderProcessFilter();

	static void RenderProcessList();

	static void RenderDllSelectorW();

	extern std::wstring g_dllPathW;

	extern char processKeyword[256];

	extern int g_selectedProcess;
}