#include <Windows.h>
#include "include/ntapi.h"
#include "include/utils.h"

typedef DWORD(WINAPI* QUERYUSERAPC)(
    PAPCFUNC pfnAPC,
    HANDLE hThread,
    ULONG_PTR dwData
    );

typedef HMODULE(WINAPI* LOADLIBRARYW)(
    LPCWSTR lpLibFileName
	);

typedef BOOL(WINAPI* WRITEPROCESSMEMORY)(
    HANDLE hProcess,
    PVOID lpBaseAddress,
    LPCVOID lpBuffer,
    SIZE_T nSize,
    SIZE_T* lpNumberOfBytesWritten
);

BOOL ApcInject(DWORD pid, LPCWSTR dwDLLPath);

BOOL RtlThreadInject(DWORD pid, LPCWSTR dwDLLPath);