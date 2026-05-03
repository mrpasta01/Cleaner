#include <windows.h>
#include "resource.h"
#include <vector>
#include <string>
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Прототипы функций из commands.cpp
void RunCommand(LPCWSTR file, LPCWSTR params);
void CleanTempFiles();
void RestartExplorer();
void SmartOrganize(HWND hwnd);
std::vector<std::wstring> CleanInvalidSoftwareReg();
static HFONT hFont;
LRESULT CALLBACK LogWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // Создаем многострочное текстовое поле с прокруткой
        HWND hEdit = CreateWindowEx(0, L"EDIT", (LPCWSTR)((LPCREATESTRUCT)lParam)->lpCreateParams,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            10, 10, 360, 240, hwnd, (HMENU)ID_LOG_EDIT, NULL, NULL);

        SendMessage(hEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); break;
    default: return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void ShowResultsDialog(HWND parent, std::wstring text) {
    const wchar_t CLASS_NAME[] = L"LogWindowClass";
    WNDCLASS wc = { 0 };

    // Проверяем, не зарегистрирован ли класс уже, чтобы избежать ошибки
    if (!GetClassInfo(GetModuleHandle(NULL), CLASS_NAME, &wc)) {
        wc.lpfnWndProc = LogWindowProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = CLASS_NAME;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClass(&wc);
    }

    HWND hDlg = CreateWindowEx(
        WS_EX_TOPMOST, // Чтобы окно было поверх основного
        CLASS_NAME,
        L"Результаты очистки",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 320,
        parent, NULL, GetModuleHandle(NULL),
        (LPVOID)text.c_str()
    );

    if (hDlg) {
        ShowWindow(hDlg, SW_SHOWNORMAL); // ПОКАЗЫВАЕМ ОКНО
        UpdateWindow(hDlg);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");

        auto CreateBtn = [&](LPCWSTR name, int id, int y) {
            HWND hBtn = CreateWindow(L"BUTTON", name, WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                20, y, 220, 35, hwnd, (HMENU)id, NULL, NULL);
            SendMessage(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
            };

        // --- БЛОК ОЧИСТКИ (Файлы и Реестр) ---
        CreateBtn(L"🧹 Очистить временные файлы", ID_BTN_CLEAN_TEMP, 15);

        CreateBtn(L"⚙️ Системная очистка диска", ID_BTN_CLEANMGR, 55);
        // Чекбокс привязан к очистке диска
        HWND hCheck = CreateWindow(L"BUTTON", L"↳ настроить категории вручную",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            35, 95, 200, 20, hwnd, (HMENU)ID_CHK_SHOW_SELECT, NULL, NULL);
        SendMessage(hCheck, WM_SETFONT, (WPARAM)hFont, TRUE);

        CreateBtn(L"🔑 Очистить реестр", ID_BTN_REGEDIT_SOFT, 125);

        // --- РАЗДЕЛИТЕЛЬ ---
        CreateWindow(L"STATIC", L"", WS_VISIBLE | WS_CHILD | SS_ETCHEDHORZ,
            20, 175, 220, 2, hwnd, NULL, NULL, NULL);

        // --- БЛОК СЕРВИСА (Проверки и система) ---
        CreateBtn(L"🛡️ Проверка целостности", ID_BTN_SFC, 190);
        CreateBtn(L"🔍 Проверка диска", ID_BTN_CHKDSK, 235);
        CreateBtn(L"🛠️ Глубокое восстановление", ID_BTN_DISM, 280);
        CreateBtn(L"🔄 Перезапустить Проводник", ID_BTN_EXP, 320);
        CreateBtn(L"📂 Сортировать файлы в папке", ID_BTN_ORGANIZE, 360); 

        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_BTN_CLEAN_TEMP:
            CleanTempFiles();
            MessageBox(hwnd, L"Temp очищен!", L"Success", MB_OK);
            break;
        case ID_BTN_SFC:
            RunCommand(L"cmd.exe", L"/k sfc /scannow");
            MessageBox(hwnd, L"Результат будет написан в консоли", L"Success", MB_OK);
            break;
        case ID_BTN_CLEANMGR: {
            LRESULT isChecked = SendMessage(GetDlgItem(hwnd, ID_CHK_SHOW_SELECT), BM_GETCHECK, 0, 0);
            if (isChecked == BST_CHECKED) {
                MessageBox(hwnd, L"Сейчас откроется окно выбора категорий. Отметьте всё, что хотите удалить, и нажмите ОК.", L"Настройка очистки", MB_OK | MB_ICONINFORMATION);
                RunCommand(L"cleanmgr.exe", L"/sageset:1");
            }            
            RunCommand(L"cleanmgr.exe", L"/sagerun:1");
            break;
        }
        case ID_BTN_ORGANIZE:
            SmartOrganize(hwnd);
            break;
        case ID_BTN_CHKDSK:
            RunCommand(L"cmd.exe", L"/k chkdsk");
            MessageBox(hwnd, L"Результат будет написан в консоли", L"Success", MB_OK);
            break;
        case ID_BTN_DISM:
            MessageBox(hwnd, L"Внимание! Требуется интернет. Процесс может занять до 20 минут.", L"DISM Recovery", MB_OK | MB_ICONINFORMATION);
            RunCommand(L"cmd.exe", L"/k dism /online /cleanup-image /restorehealth");
            break;
        case ID_BTN_REGEDIT_SOFT: {
            std::vector<std::wstring> results = CleanInvalidSoftwareReg();

            if (!results.empty()) {
                std::wstring finalLog = L"Удалены следующие ветки:\r\n\r\n";
                for (const auto& name : results) {
                    finalLog += L"  o  " + name + L"\r\n";
                }
                ShowResultsDialog(hwnd, finalLog);
            }
            else {
                MessageBox(hwnd, L"Мусорных записей не обнаружено.", L"Реестр", MB_OK);
            }
            break;
        }
        case ID_BTN_EXP: RestartExplorer(); break;
        }
        break;
    }
    case WM_DESTROY:
        if (hFont) DeleteObject(hFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Функция для выполнения действия по номеру
int ExecuteByNumber(int number, HWND hwnd) {
    switch (number) {
    case 1:
        CleanTempFiles();
        MessageBox(hwnd, L"Temp очищен!", L"Success", MB_OK);
        return 1;
    case 2: {
        // Системная очистка диска
        // Для простоты - без настройки категорий
        RunCommand(L"cleanmgr.exe", L"");
        MessageBox(hwnd, L"Запущена очистка диска", L"Success", MB_OK);
        return 1;
    }
    case 3: {
        std::vector<std::wstring> results = CleanInvalidSoftwareReg();
        if (!results.empty()) {
            std::wstring finalLog = L"Удалены следующие ветки:\r\n\r\n";
            for (const auto& name : results) {
                finalLog += L"  o  " + name + L"\r\n";
            }
            ShowResultsDialog(hwnd, finalLog);
        }
        else {
            MessageBox(hwnd, L"Мусорных записей не обнаружено.", L"Реестр", MB_OK);
        }
        return 1;
    }
    case 4:
        RunCommand(L"cmd.exe", L"/k sfc /scannow");
        MessageBox(hwnd, L"Результат будет написан в консоли", L"Success", MB_OK);
        return 1;
    case 5:
        RunCommand(L"cmd.exe", L"/k chkdsk");
        MessageBox(hwnd, L"Результат будет написан в консоли", L"Success", MB_OK);
        return 1;
    case 6:
        MessageBox(hwnd, L"Внимание! Требуется интернет. Процесс может занять до 20 минут.", L"DISM Recovery", MB_OK | MB_ICONINFORMATION);
        RunCommand(L"cmd.exe", L"/k dism /online /cleanup-image /restorehealth");
        return 1;
    case 7:
        RestartExplorer();
        return 1;
    case 8:
        SmartOrganize(hwnd);
        return 1;
    default:
        return 0;
    }
}

// Функция для отображения справки в MessageBox (если нужно)
void ShowHelpDialog(HWND hwnd) {
    std::wstring helpText =
        L"CLEANER - Команды командной строки\n\n"
        L"Cleaner 1 - Очистить временные файлы\n"
        L"Cleaner 2 - Системная очистка диска\n"
        L"Cleaner 3 - Очистить реестр\n"
        L"Cleaner 4 - Проверка целостности (SFC)\n"
        L"Cleaner 5 - Проверка диска (CHKDSK)\n"
        L"Cleaner 6 - Глубокое восстановление (DISM)\n"
        L"Cleaner 7 - Перезапустить Проводник\n"
        L"Cleaner 8 - Сортировка файлов\n\n"
        L"Cleaner help / ? - Показать справку\n"
        L"Cleaner - Запустить графический интерфейс";

    MessageBox(hwnd, helpText.c_str(), L"Справка Cleaner", MB_OK | MB_ICONINFORMATION);
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    // Парсим аргументы командной строки
    std::string cmdLine(lpCmdLine);
    int argNumber = 0;

    if (!cmdLine.empty()) {
        // Убираем пробелы в начале
        size_t start = cmdLine.find_first_not_of(" \t");
        if (start != std::string::npos) {
            cmdLine = cmdLine.substr(start);
            argNumber = std::atoi(cmdLine.c_str());
        }
    }

    const wchar_t CLASS_NAME[] = L"ModernUtilClass";
    HICON hMainIcon = ExtractIcon(hInstance, L"shell32.dll", 84);
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = hMainIcon;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"Cleaner",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 280, 440, NULL, NULL, hInstance, NULL);
    
    // Если передан аргумент - выполняем соответствующую функцию
    if (!hwnd) return 0;

    // Проверка help
    if (cmdLine == "help" || cmdLine == "?") {
        ShowHelpDialog(hwnd);
        return 0;
    }

    int num = atoi(cmdLine.c_str());
    if (num >= 1 && num <= 8) {
        ShowWindow(hwnd, SW_HIDE);

        // Выполняем действие
        ExecuteByNumber(argNumber, hwnd);

        // Даем время на выполнение и закрываемся
        Sleep(2000);
        return 0;
    }
    

    // Если аргумента нет или он неверный - показываем окно
    ShowWindow(hwnd, nCmdShow);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}