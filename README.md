# DeviceIoControl USN Journal Filter

This project demonstrates a DLL hook on `DeviceIoControl` to filter USN journal records returned by `FSCTL_READ_USN_JOURNAL`.  
It allows you to hide specific files from file system monitoring tools.

## Overview

The DLL intercepts calls to `DeviceIoControl` and examines the returned USN journal buffer.  
Records matching a specified file name are removed before the data is returned to the calling process.  

Key points:
- Uses [Microsoft Detours](https://github.com/microsoft/Detours) to hook `DeviceIoControl`.
- Targets FSCTL code `FSCTL_READ_USN_JOURNAL`.
- Can dynamically filter any file name using the exported `SetFilterName` function.
- Useful for demonstrating low-level API hooking and buffer manipulation.

## Code Example

```cpp
// Set the file name you want to hide
SetFilterName(L"fabric-api-0.119.2+1.21.4.jar");
```

The hook inspects USN records (versions 2 and 3) and removes entries that match the filter from the output buffer.

## Build Instructions

1. Install Visual Studio (x64, C++17 or higher).
2. Include and link **Detours** (`detours.lib`).
3. Compile the project as a DLL (x32 for JournalTrace).

## Usage

Inject the DLL into a target process using any injector (for example via `LoadLibrary`).
Once loaded, the hook will filter USN journal records in real time.

This makes file system monitoring and memory tools such as **JournalTrace** display incomplete or truncated results for filtered files.

You can change the filtered file dynamically at runtime:

```cpp
SetFilterName(L"example-file-to-hide.txt");
```
