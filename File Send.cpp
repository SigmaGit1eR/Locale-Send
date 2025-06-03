#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <ShlObj.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <atlbase.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <shellapi.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Netapi32.lib")

// Get all local IP addresses from the ARP cache
std::vector<std::wstring> getLocalIPs() {
    system("arp -a > \"IP_List.txt\"");

    std::vector<std::wstring> ips;
    std::wifstream file(L"IP_List.txt");
    std::wstring line;

    while (std::getline(file, line)) {
        size_t pos = line.find(L"dynamic");
        if (pos != std::wstring::npos) {
            std::wstring ip = line.substr(0, pos);
            ip.erase(std::remove_if(ip.begin(), ip.end(), iswspace), ip.end());
            ips.push_back(ip);
        }
    }

    DeleteFileW(L"IP_List.txt");
    return ips;
}

// Show folder or file selection dialog to user
std::wstring BrowseForFileOrFolder() {
    IFileDialog* pFileOpen = nullptr;
    std::wstring result;

    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_IFileDialog, reinterpret_cast<void**>(&pFileOpen));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        pFileOpen->GetOptions(&dwOptions);
        pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

        hr = pFileOpen->Show(nullptr);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    result = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }

    return result;
}

// Create shortcut (.lnk) pointing to targetPath, saved as shortcutPath
bool CreateShortcut(const std::wstring& targetPath, const std::wstring& shortcutPath) {
    CoInitialize(nullptr);

    CComPtr<IShellLink> pShellLink;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLink, reinterpret_cast<void**>(&pShellLink));
    if (FAILED(hr)) return false;

    pShellLink->SetPath(targetPath.c_str());

    CComQIPtr<IPersistFile> pPersistFile(pShellLink);
    if (!pPersistFile) return false;

    hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
    CoUninitialize();

    return SUCCEEDED(hr);
}

int main() {
    CoInitialize(nullptr);

    // Step 1: Get all discovered IPs from ARP
    std::vector<std::wstring> ips = getLocalIPs();
    if (ips.empty()) {
        std::wcout << L"No IP addresses found on the local network.\n";
        return 1;
    }

    // Step 2: Ask the user to select a file or folder to copy
    std::wstring selectedPath = BrowseForFileOrFolder();
    if (selectedPath.empty()) {
        std::wcout << L"No file or folder selected.\n";
        return 1;
    }

    // Step 3: Extract name of selected file/folder
    bool isFolder = GetFileAttributesW(selectedPath.c_str()) & FILE_ATTRIBUTE_DIRECTORY;
    std::wstring fileName;
    size_t pos = selectedPath.find_last_of(L"\\/");
    fileName = (pos != std::wstring::npos) ? selectedPath.substr(pos + 1) : selectedPath;

    // Default copy destination is C:\
    std::wstring defaultCopyPath = L"C:\\";

    // Step 4: Ask user for credentials (used for \\<IP>\C$ access)
    std::wstring username, password;
    std::wcout << L"Enter login: ";
    std::getline(std::wcin, username);
    std::wcout << L"Enter password: ";
    std::getline(std::wcin, password);

    // Step 5: Loop through each IP address and copy file/folder
    for (const auto& ip : ips) {
        std::wstring uncPath = L"\\\\" + ip + L"\\C$";

        // Connect to \\IP\C$ using provided credentials
        NETRESOURCE nr;
        ZeroMemory(&nr, sizeof(nr));
        nr.dwType = RESOURCETYPE_DISK;
        nr.lpRemoteName = const_cast<LPWSTR>(uncPath.c_str());

        DWORD result = WNetAddConnection2W(&nr, password.c_str(), username.c_str(), CONNECT_TEMPORARY);
        if (result != NO_ERROR) {
            std::wcout << L"[✘] " << ip << L" — Failed to connect. Error: " << result << L"\n";
            continue;
        }

        // Build full destination path
        std::wstring destinationPath = uncPath + L"\\" + fileName;

        // Copy the file or folder
        if (isFolder) {
            std::wstring command = L"xcopy \"" + selectedPath + L"\" \"" + uncPath + L"\\" + fileName + L"\" /E /I /Y";
            system(std::string(command.begin(), command.end()).c_str());
        } else {
            if (!CopyFileW(selectedPath.c_str(), destinationPath.c_str(), FALSE)) {
                std::wcout << L"[✘] " << ip << L" — Failed to copy file.\n";
                continue;
            }
        }

        // Create shortcut on remote desktop (visible to all users)
        std::wstring shortcutPath = uncPath + L"\\Users\\Public\\Desktop\\" + fileName + L".lnk";
        if (CreateShortcut(destinationPath, shortcutPath)) {
            std::wcout << L"[✔] " << ip << L" — File copied and shortcut created.\n";
        } else {
            std::wcout << L"[!] " << ip << L" — File copied but failed to create shortcut.\n";
        }

        // Disconnect from remote share
        WNetCancelConnection2W(uncPath.c_str(), 0, TRUE);
    }

    CoUninitialize();
    return 0;
}
