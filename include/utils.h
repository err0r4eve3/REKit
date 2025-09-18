#pragma once
#include <Windows.h>
#include <string>

std::string WStringToString(const std::wstring& wstr);
std::string WStringToUTF8(const std::wstring& wstr);
std::wstring UTF8ToWString(const std::string& str);

BOOL EnableDebugPrivilege();
BOOL TerminateProcessByPID(ULONG pid);
BOOL OpenDllFileDialogW(std::wstring& filePath);
