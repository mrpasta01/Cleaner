#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <strsafe.h>
#include <shlwapi.h>
#include <vector>
#include <string>
#include <filesystem>
#include <shobjidl.h>
#pragma comment(lib, "Shlwapi.lib")



// Функция запуска системных утилит
void RunCommand(LPCWSTR file, LPCWSTR params) {
    SHELLEXECUTEINFO sei = { sizeof(sei) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = file;
    sei.lpParameters = params;
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteEx(&sei)) {
        CloseHandle(sei.hProcess);
    }
}

// Вспомогательная функция для удаления файлов внутри конкретной папки
void DeleteFilesInside(LPCWSTR szDir) {
    WIN32_FIND_DATA ffd;
    WCHAR szSearchPath[MAX_PATH];

    // Формируем путь для поиска: "C:\Path\Temp\*"
    PathCombine(szSearchPath, szDir, L"*");

    HANDLE hFind = FindFirstFile(szSearchPath, &ffd);

    if (INVALID_HANDLE_VALUE == hFind) return;

    do {
        // Пропускаем ссылки на саму папку (.) и родительскую (..)
        if (lstrcmp(ffd.cFileName, L".") == 0 || lstrcmp(ffd.cFileName, L"..") == 0)
            continue;

        WCHAR szFullFilePath[MAX_PATH];
        PathCombine(szFullFilePath, szDir, ffd.cFileName);

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Если это вложенная папка, пробуем зайти в неё (рекурсия)
            DeleteFilesInside(szFullFilePath);
            // После очистки файлов внутри, пробуем удалить саму папку
            RemoveDirectory(szFullFilePath);
        }
        else {
            // 1. Снимаем атрибут "Только для чтения", если он мешает
            SetFileAttributes(szFullFilePath, FILE_ATTRIBUTE_NORMAL);

            // 2. Пытаемся удалить файл
            // Если файл занят другой программой, DeleteFile вернет FALSE — это нормально.
            DeleteFile(szFullFilePath);
        }
    } while (FindNextFile(hFind, &ffd) != 0);

    FindClose(hFind);
}

// Очистка Temp
void CleanTempFiles() {
    WCHAR szPath[MAX_PATH];

    // 1. Очистка Temp (AppData\Local\Temp)
    if (GetTempPath(MAX_PATH, szPath) > 0) {
        DeleteFilesInside(szPath);
    }

    // 2. Temp (C:\Windows\Temp)
    UINT len = GetWindowsDirectory(szPath, MAX_PATH);
    if (len > 0) {
        PathAppend(szPath, L"Temp");
        DeleteFilesInside(szPath);
    }
}

// Функция перезапуска Проводника
void RestartExplorer() {
    // 1. Находим и убиваем процесс explorer.exe
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(pe);
        if (Process32First(hSnap, &pe)) {
            do {
                if (lstrcmpi(pe.szExeFile, L"explorer.exe") == 0) {
                    HANDLE hExp = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hExp) {
                        TerminateProcess(hExp, 0);
                        CloseHandle(hExp);
                    }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    // 2. Запускаем проводник заново
    ShellExecute(NULL, L"open", L"explorer.exe", NULL, NULL, SW_SHOWNORMAL);
}

// Вспомогательная функция: проверяет, пуст ли ключ СОВСЕМ (включая все подпапки)
bool IsRegistryKeyEmpty(HKEY hKeyRoot, LPCWSTR subKey) {
    HKEY hKey;
    if (RegOpenKeyEx(hKeyRoot, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;

    DWORD subKeys = 0, values = 0;
    RegQueryInfoKey(hKey, NULL, NULL, NULL, &subKeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);

    // Если есть значения (файлы) — он не пуст
    if (values > 0) {
        RegCloseKey(hKey);
        return false;
    }

    // Если есть подпапки — проверяем каждую из них
    if (subKeys > 0) {
        WCHAR name[256];
        for (DWORD i = 0; i < subKeys; i++) {
            DWORD size = 256;
            if (RegEnumKeyEx(hKey, i, name, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
                if (!IsRegistryKeyEmpty(hKey, name)) { // Если хотя бы одна подпапка не пуста
                    RegCloseKey(hKey);
                    return false;
                }
            }
        }
    }

    RegCloseKey(hKey);
    return true; // Если дошли сюда — всё чисто
}

std::vector<std::wstring> CleanInvalidSoftwareReg() {
    std::vector<std::wstring> deletedApps;
    HKEY hSoftware;
    if (RegOpenKeyEx(HKEY_CURRENT_USER, L"Software", 0, KEY_ALL_ACCESS, &hSoftware) != ERROR_SUCCESS)
        return deletedApps;

    WCHAR subKeyName[256];
    DWORD index = 0;
    DWORD size = 256;
    const LPCWSTR pathValues[] = { L"InstallDir", L"Path", L"InstallLocation", L"AppPath" };

    while (RegEnumKeyEx(hSoftware, index, subKeyName, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
        bool deleted = false;

        // ИСПОЛЬЗУЕМ ВАШУ ФУНКЦИЮ ПРОВЕРКИ НА ПУСТОТУ
        if (IsRegistryKeyEmpty(hSoftware, subKeyName)) {
            if (RegDeleteTree(hSoftware, subKeyName) == ERROR_SUCCESS) {
                deletedApps.push_back(subKeyName);
                deleted = true;
            }
        }
        else {
            // Если не пуст, проверяем на битые пути
            HKEY hAppKey;
            if (RegOpenKeyEx(hSoftware, subKeyName, 0, KEY_READ, &hAppKey) == ERROR_SUCCESS) {
                WCHAR path[MAX_PATH];
                DWORD pSize = sizeof(path);
                bool pathFound = false;

                for (int i = 0; i < 4; i++) {
                    if (RegQueryValueEx(hAppKey, pathValues[i], NULL, NULL, (LPBYTE)path, &pSize) == ERROR_SUCCESS) {
                        pathFound = true; break;
                    }
                    pSize = sizeof(path);
                }

                if (pathFound && !PathFileExists(path)) {
                    RegCloseKey(hAppKey);
                    if (RegDeleteTree(hSoftware, subKeyName) == ERROR_SUCCESS) {
                        deletedApps.push_back(subKeyName);
                        deleted = true;
                    }
                }
                else {
                    RegCloseKey(hAppKey);
                }
            }
        }

        if (deleted) {
            size = 256; // Не инкрементируем индекс, так как список сдвинулся
        }
        else {
            index++;
            size = 256;
        }
    }
    RegCloseKey(hSoftware);
    return deletedApps;
}

namespace fs = std::filesystem;

// Функция для выбора папки пользователем
std::wstring SelectFolder(HWND owner) {
    std::wstring folderPath = L"";
    IFileOpenDialog* pFileOpen;

    // Инициализируем COM-интерфейс для диалога
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

    if (SUCCEEDED(hr)) {
        // Устанавливаем режим выбора папок
        DWORD dwOptions;
        pFileOpen->GetOptions(&dwOptions);
        pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);

        if (SUCCEEDED(pFileOpen->Show(owner))) {
            IShellItem* pItem;
            if (SUCCEEDED(pFileOpen->GetResult(&pItem))) {
                PWSTR pszFilePath;
                if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath))) {
                    folderPath = pszFilePath;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
    return folderPath;
}


void SmartOrganize(HWND hwnd) {
    std::wstring pathStr = SelectFolder(hwnd);
    if (pathStr.empty()) return;

    fs::path targetPath = pathStr;
    int movedCount = 0;

    try {
        for (const auto& entry : fs::directory_iterator(targetPath)) {
            if (!entry.is_regular_file()) continue;

            fs::path filePath = entry.path();
            std::wstring ext = filePath.extension().wstring();
            if (ext.empty()) continue;

            // Приводим расширение к нижнему регистру
            std::wstring extLower = ext;
            for (auto& c : extLower) c = towlower(c);

            std::wstring folderName = L"";
            bool isDocument = false;

            // 1. КАТЕГОРИЯ: ИЗОБРАЖЕНИЯ
            if (extLower == L".png" || extLower == L".ico" || extLower == L".jpeg" ||
                extLower == L".jpg" || extLower == L".iweb" || extLower == L".bmp") {
                folderName = L"Изображения";
            }
            // 2. КАТЕГОРИЯ: ВИДЕО (включая GIF, как ты просил)
            else if (extLower == L".mp4" || extLower == L".gif" || extLower == L".mov" || extLower == L".avi") {
                folderName = L"Видео";
            }
            // 3. КАТЕГОРИЯ: ДОКУМЕНТЫ (С подпапками)
            else if (extLower == L".pdf") {
                folderName = L"Документы/pdf";
                isDocument = true;
            }
            else if (extLower == L".docx" || extLower == L".doc" || extLower == L".rtf") {
                folderName = L"Документы/Word_форматы";
                isDocument = true;
            }
            else if (extLower == L".txt") {
                folderName = L"Документы/Прочее_тексты";
                isDocument = true;
            }
            // 4. ИСКЛЮЧЕНИЯ (не трогаем системные файлы)
            else if (extLower == L".exe" || extLower == L".msi") {
                continue;
            }
            // 5. ВСЁ ОСТАЛЬНОЕ (динамические папки, например .zip -> папка "zip")
            else {
                folderName = extLower.substr(1);
            }

            // ПЕРЕМЕЩЕНИЕ
            if (!folderName.empty()) {
                fs::path subDir = targetPath / folderName;

                // create_directories (во множественном числе) создаст всю цепочку,
                // например "Документы/pdf", даже если папки "Документы" еще нет.
                if (!fs::exists(subDir)) {
                    fs::create_directories(subDir);
                }

                fs::path newPath = subDir / filePath.filename();
                if (filePath != newPath) {
                    fs::rename(filePath, newPath);
                    movedCount++;
                }
            }
        }

        std::wstring res = L"Сортировка завершена! Файлов обработано: " + std::to_wstring(movedCount);
        MessageBox(hwnd, res.c_str(), L"Органайзер", MB_OK | MB_ICONINFORMATION);

    }
    catch (const std::exception& e) {
        MessageBox(hwnd, L"Ошибка при перемещении файлов. Возможно, один из них открыт.", L"Ошибка", MB_OK | MB_ICONERROR);
    }
}