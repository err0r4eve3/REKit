#include "include/injector.h"

BOOL ApcInject(DWORD pid, LPCWSTR dwDLLPath)
{
    EnableDebugPrivilege();

    BOOL injected = FALSE;

    HMODULE hKernel32 = GetModuleHandle(L"kernel32.dll");
    if (!hKernel32) return FALSE;

    QUERYUSERAPC QueueUserAPC = (QUERYUSERAPC)GetProcAddress(hKernel32, "QueueUserAPC");
    LOADLIBRARYW LoadLibraryW = (LOADLIBRARYW)GetProcAddress(hKernel32, "LoadLibraryW");
    WRITEPROCESSMEMORY WriteProcessMemory = (WRITEPROCESSMEMORY)GetProcAddress(hKernel32, "WriteProcessMemory");

    if (!QueueUserAPC || !LoadLibraryW || !WriteProcessMemory) return FALSE;

    HMODULE hNtDll = GetModuleHandle(L"ntdll.dll");
    if (!hNtDll) return FALSE;

    NTQUERYSYSTEMINFORMATION NtQuerySystemInformation =
        (NTQUERYSYSTEMINFORMATION)GetProcAddress(hNtDll, "NtQuerySystemInformation");
    if (!NtQuerySystemInformation) return FALSE;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, (DWORD)(ULONG_PTR)pid);
    if (!hProcess) return FALSE;

    SIZE_T dllPathSize = (wcslen(dwDLLPath) + 1) * sizeof(WCHAR);

    PVOID lpBaseAddress = VirtualAllocEx(
        hProcess,
        NULL,
        dllPathSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!lpBaseAddress) {
        CloseHandle(hProcess);
        return FALSE;
    }

    if (!WriteProcessMemory(hProcess, lpBaseAddress, dwDLLPath, dllPathSize, NULL)) {
        VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    ULONG cbBuffer = 0x100000;
    PVOID pBuffer = malloc(cbBuffer);

    if (!pBuffer) {
        VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    NTSTATUS status;

    do {
        status = NtQuerySystemInformation(
            SystemProcessInformation,
            pBuffer,
            cbBuffer,
            NULL
        );

        if (status == STATUS_INFO_LENGTH_MISMATCH) {
            cbBuffer *= 2;
            PVOID pNewBuffer = realloc(pBuffer, cbBuffer);
            if (!pNewBuffer) {
                free(pBuffer);
                VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);
                CloseHandle(hProcess);
                return FALSE;
            }
            pBuffer = pNewBuffer;
        }
        else {
            break;
        }
    } while (TRUE);

    if (status != 0) {
        free(pBuffer);
        VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    PSYSTEM_PROCESS_INFORMATION pInfo = (PSYSTEM_PROCESS_INFORMATION)pBuffer;

    while (pInfo) {
        if ((DWORD)(ULONG_PTR)pInfo->UniqueProcessId == pid) {
            for (ULONG i = 0; i < pInfo->NumberOfThreads; i++) {
                PSYSTEM_THREAD_INFORMATION pThread = &pInfo->Threads[i];
                HANDLE hThread = OpenThread(THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, (DWORD)(ULONG_PTR)pThread->ClientId.UniqueThread);
                if (hThread) {
                    DWORD result = QueueUserAPC((PAPCFUNC)LoadLibraryW, hThread, (ULONG_PTR)lpBaseAddress);
                    if (result != 0) {
                        injected = TRUE;
                    }
                    CloseHandle(hThread);
                }
            }
            break;
        }

        if (pInfo->NextEntryOffset == 0) break;
        pInfo = (PSYSTEM_PROCESS_INFORMATION)((BYTE*)pInfo + pInfo->NextEntryOffset);
    }

    free(pBuffer);
    if (!injected) {
        VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);
    }
    CloseHandle(hProcess);
    return injected;
}

static HANDLE RtlCreateUserThread(HANDLE hProcess, LPVOID lpBaseAddress, LPVOID lpSpace) {
    typedef DWORD(WINAPI* functypeRtlCreateUserThread)(
        HANDLE ProcessHandle,
        PSECURITY_DESCRIPTOR SecurityDescriptor,
        BOOL CreateSuspended,
        ULONG StackZeroBits,
        PULONG StackReserved,
        PULONG StackCommit,
        LPVOID StartAddress,
        LPVOID StartParameter,
        HANDLE ThreadHandle,
        LPVOID ClientID
        );

    HANDLE hRemoteThread = NULL;
    HMODULE hNtDllModule = GetModuleHandle(L"ntdll.dll");
    if (hNtDllModule == NULL) return NULL;

    functypeRtlCreateUserThread funcRtlCreateUserThread = (functypeRtlCreateUserThread)GetProcAddress(hNtDllModule, "RtlCreateUserThread");
    if (!funcRtlCreateUserThread) return NULL;

    funcRtlCreateUserThread(hProcess, NULL, 0, 0, 0, 0, lpBaseAddress, lpSpace, &hRemoteThread, NULL);
    return hRemoteThread;
}


BOOL RtlThreadInject(DWORD pid, LPCWSTR dwDLLPath) {
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, pid);
    if (hProcess == NULL) return FALSE;

    HMODULE hKernel32 = GetModuleHandle(L"kernel32.dll");
    if (!hKernel32) {
        CloseHandle(hProcess);
        return FALSE;
	}

    LPVOID lpLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryW");
    if (!lpLoadLibrary) {
        CloseHandle(hProcess);
        return FALSE;
    }

    SIZE_T dllPathLen = (wcslen(dwDLLPath) + 1) * sizeof(WCHAR);
    LPVOID lpRemoteMemory = VirtualAllocEx(hProcess, NULL, dllPathLen, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!lpRemoteMemory) {
        CloseHandle(hProcess);
        return FALSE;
    }

    if (!WriteProcessMemory(hProcess, lpRemoteMemory, dwDLLPath, dllPathLen, NULL)) {
        VirtualFreeEx(hProcess, lpRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    HANDLE hThread = RtlCreateUserThread(hProcess, lpLoadLibrary, lpRemoteMemory);
    if (hThread == NULL) {
        VirtualFreeEx(hProcess, lpRemoteMemory, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, lpRemoteMemory, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    return TRUE;
}