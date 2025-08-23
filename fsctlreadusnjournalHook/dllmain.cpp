#include <windows.h>
#include <winioctl.h>
#include <string>
#include <algorithm>
#include <wctype.h>
#include <detours.h>

#pragma comment(lib, "detours.lib")

static BOOL(WINAPI* OriginalDeviceIoControl)(
    HANDLE hDevice,
    DWORD dwIoControlCode,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned,
    LPOVERLAPPED lpOverlapped) = DeviceIoControl;

#define FSCTL_READ_USN_JOURNAL CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 46, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef struct _MY_USN_RECORD { 
    DWORD RecordLength; WORD MajorVersion; WORD MinorVersion;
} 

MY_USN_RECORD, * PMY_USN_RECORD;

std::wstring FILTERED_NAME = L"fabric-api-0.119.2+1.21.4.jar"; // example name to filter

std::wstring getNameFromUSN(BYTE* recordData, DWORD recordLength) {
    if (!recordData || recordLength < sizeof(MY_USN_RECORD)) return L"";

    PMY_USN_RECORD baseRecord = reinterpret_cast<PMY_USN_RECORD>(recordData);

    if (baseRecord->MajorVersion == 2 && recordLength >= 60) {
        WORD fileNameLength = *reinterpret_cast<WORD*>(recordData + 56);
        WORD fileNameOffset = *reinterpret_cast<WORD*>(recordData + 58);

        if (fileNameOffset < recordLength && fileNameOffset + fileNameLength <= recordLength && fileNameLength > 0 && fileNameLength <= 512) {
            WCHAR* fileName = reinterpret_cast<WCHAR*>(recordData + fileNameOffset);
            return std::wstring(fileName, fileNameLength / sizeof(WCHAR));
        }
    }
    else if (baseRecord->MajorVersion == 3 && recordLength >= 76) {
        WORD fileNameLength = *reinterpret_cast<WORD*>(recordData + 72);
        WORD fileNameOffset = *reinterpret_cast<WORD*>(recordData + 74);

        if (fileNameOffset < recordLength && fileNameOffset + fileNameLength <= recordLength && fileNameLength > 0 && fileNameLength <= 512) {
            WCHAR* fileName = reinterpret_cast<WCHAR*>(recordData + fileNameOffset);
            return std::wstring(fileName, fileNameLength / sizeof(WCHAR));
        }
    }

    return L"";
}

bool ShouldFilterRecord(const std::wstring& fileName) {
    if (fileName.empty()) return false;

    std::wstring nameLower = fileName;
    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);

    return nameLower.find(FILTERED_NAME) != std::wstring::npos;
}

DWORD FilterUsnBuffer(LPVOID lpOutBuffer, DWORD originalBytes) {
    if (!lpOutBuffer || originalBytes < sizeof(LONGLONG)) return originalBytes;

    BYTE* buffer = static_cast<BYTE*>(lpOutBuffer);

    BYTE* currentPos = buffer + sizeof(LONGLONG);
    BYTE* writePos = currentPos;
    DWORD remainingBytes = originalBytes - sizeof(LONGLONG);
    DWORD newSize = sizeof(LONGLONG);

    while (remainingBytes >= sizeof(MY_USN_RECORD)) {
        PMY_USN_RECORD record = reinterpret_cast<PMY_USN_RECORD>(currentPos);

        if (record->RecordLength == 0 || 
            record->RecordLength > remainingBytes || record->RecordLength < sizeof(MY_USN_RECORD) || record->RecordLength > 8192) {
            break;
        }

        std::wstring fileName = getNameFromUSN(currentPos, record->RecordLength);

        bool shouldFilter = ShouldFilterRecord(fileName);

        if (!shouldFilter) {
            if (writePos != currentPos) {
                memmove(writePos, currentPos, record->RecordLength);
            }
            writePos += record->RecordLength;
            newSize += record->RecordLength;
        }

        DWORD alignedLength = (record->RecordLength + 7) & ~7;
        currentPos += alignedLength;
        remainingBytes -= alignedLength;
    }

    return newSize;
}

BOOL WINAPI HookedDeviceIoControl(
    HANDLE hDevice,
    DWORD dwIoControlCode,
    LPVOID lpInBuffer,
    DWORD nInBufferSize,
    LPVOID lpOutBuffer,
    DWORD nOutBufferSize,
    LPDWORD lpBytesReturned,
    LPOVERLAPPED lpOverlapped) {

    BOOL result = OriginalDeviceIoControl(
        hDevice,
        dwIoControlCode,
        lpInBuffer,
        nInBufferSize,
        lpOutBuffer,
        nOutBufferSize,
        lpBytesReturned,
        lpOverlapped);

    if (result && dwIoControlCode == FSCTL_READ_USN_JOURNAL && lpOutBuffer && lpBytesReturned && *lpBytesReturned > sizeof(LONGLONG)) {
        DWORD newSize = FilterUsnBuffer(lpOutBuffer, *lpBytesReturned);
        *lpBytesReturned = newSize;
    }

    return result;
}

void InstallHooks() {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)OriginalDeviceIoControl, HookedDeviceIoControl);
    DetourTransactionCommit();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            InstallHooks();
            break;
        }
    return TRUE;
}

extern "C" {
    __declspec(dllexport) void SetFilterName(const wchar_t* name) {
        if (name) {
            FILTERED_NAME = name;
            std::transform(FILTERED_NAME.begin(), FILTERED_NAME.end(), FILTERED_NAME.begin(), ::towlower);
        }
    }
}