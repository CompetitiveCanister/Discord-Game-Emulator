#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

const char* JSON_URL = "https://raw.github.com/swypieuwuu/Discord-Game-Emulator/refs/heads/main/gamelist.json";
const float APP_VERSION = 4.1f;
const char* VERSION_URL = "https://raw.githubusercontent.com/swypieuwuu/Discord-Game-Emulator/refs/heads/main/version.txt";
char updateUrl[512] = { 0 };

char* globalJsonData = NULL;
HBRUSH hBgBrush, hBtnBrush, hEditBrush;
COLORREF clrText = RGB(240, 240, 240);
COLORREF clrBg = RGB(30, 30, 30);
COLORREF clrEditBg = RGB(45, 45, 45);
HFONT hFont;

typedef struct {
    char gameName[256];
    char customExe[256];
    int timeSec;
} QueueItem;

QueueItem queue[100];
int queueCount = 0;

int appMode = 0;
HWND hGameName, hCustomExe, hTime;
HWND hBtnLaunch, hBtnToggleQueue, hBtnUpdate, hBtnSearch;
HWND hSearchWnd = NULL, hSearchEdit, hSearchList;
HWND hBtnAddQueue, hBtnStartQueue, hBtnRemoveQueue, hListBox, hQueueLabel;
HWND hBtnTerminateSimul, hBtnTerminateAllSimul;
BOOL isSimulManager = FALSE;

typedef struct {
    char displayName[256];
    char folderPath[MAX_PATH];
    char exePath[256];
    HANDLE hProcess;
    int timeSec;
    BOOL active;
} SimulGame;
SimulGame simulGames[100];

int totalTime = 0, timeLeft = 0;
int qCurrent = 1, qTotal = 1;
char dgeFolderPath[MAX_PATH] = { 0 };
char queueFilePath[MAX_PATH] = { 0 };
char currentGameName[256] = { 0 };
HWND hTimerLabel, hProgressLabel, hQueueStatusLabel, hGameLabel, hBtnCancel;
BOOL finishedNaturally = FALSE;
BOOL isSilent = FALSE;

int EvalExpr(const char** p);
int ParseFactor(const char** p) {
    while (**p == ' ') (*p)++;
    int val = 0;
    if (**p == '(') {
        (*p)++; val = EvalExpr(p);
        if (**p == ')') (*p)++;
    } else {
        while (**p >= '0' && **p <= '9') { val = val * 10 + (**p - '0'); (*p)++; }
    }
    while (**p == ' ') (*p)++;
    return val;
}
int ParseTerm(const char** p) {
    int val = ParseFactor(p);
    while (**p == '*' || **p == '/') {
        char op = **p; (*p)++;
        int next = ParseFactor(p);
        if (op == '*') val *= next;
        else if (next != 0) val /= next;
    }
    return val;
}
int EvalExpr(const char** p) {
    int val = ParseTerm(p);
    while (**p == '+' || **p == '-') {
        char op = **p; (*p)++;
        int next = ParseTerm(p);
        if (op == '+') val += next;
        else val -= next;
    }
    return val;
}

BOOL FuzzyCompare(const char* s1, const char* s2) {
    while (*s1 || *s2) {

        while (*s1 && !((*s1 >= 'A' && *s1 <= 'Z') || (*s1 >= 'a' && *s1 <= 'z') || (*s1 >= '0' && *s1 <= '9') || (unsigned char)*s1 >= 0x80)) s1++;
        while (*s2 && !((*s2 >= 'A' && *s2 <= 'Z') || (*s2 >= 'a' && *s2 <= 'z') || (*s2 >= '0' && *s2 <= '9') || (unsigned char)*s2 >= 0x80)) s2++;

        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return FALSE;

        if (*s1) s1++;
        if (*s2) s2++;
    }
    return TRUE;
}

void NormalizePath(char* dest, const char* src) {
    char* w = dest;
    while (*src) {
        if (*src == '/') { *w++ = '\\'; *w++ = '\\'; }
        else if (*src == '\\') {
            *w++ = '\\'; *w++ = '\\';
            if (*(src + 1) == '\\') src++;
        } else { *w++ = *src; }
        src++;
    }
    *w = '\0';
}

char* FetchJSON(const char* url) {
    HINTERNET hInternet = InternetOpenA("DGE_App/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return NULL;



    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_NO_CACHE_WRITE;
    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0, flags, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return NULL; }



    DWORD contentLength = 0;
    DWORD lengthSize = sizeof(contentLength);
    HttpQueryInfoA(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &lengthSize, NULL);



    DWORD bufSize = (contentLength > 0) ? (contentLength + 1024) : 524288;

    char* buffer = (char*)malloc(bufSize);
    if (!buffer) { InternetCloseHandle(hUrl); InternetCloseHandle(hInternet); return NULL; }

    DWORD bytesRead = 0;
    DWORD totalBytes = 0;


    while (InternetReadFile(hUrl, buffer + totalBytes, bufSize - totalBytes - 1, &bytesRead) && bytesRead > 0) {
        totalBytes += bytesRead;
        buffer[totalBytes] = '\0';


        if (bufSize - totalBytes < 4096) {
            bufSize *= 2;
            char* newBuffer = (char*)realloc(buffer, bufSize);
            if (!newBuffer) { free(buffer); InternetCloseHandle(hUrl); InternetCloseHandle(hInternet); return NULL; }
            buffer = newBuffer;
        }
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return buffer;
}

BOOL ParseGame(const char* json, const char* target, char* outPrimary, char* outExe) {
    const char* p = json;
    while (p && (p = strstr(p, "\"names\"")) != NULL) {
        p = strchr(p, '[');
        if (!p) break;
        const char* endArr = strchr(p, ']');
        if (!endArr) break;

        char primary[256] = { 0 };
        BOOL hasPrimary = FALSE, matchFound = FALSE;
        const char* strStart = p;

        while ((strStart = strchr(strStart, '"')) != NULL && strStart < endArr) {
            strStart++;
            const char* strEnd = strchr(strStart, '"');
            if (!strEnd || strEnd > endArr) break;

            size_t len = strEnd - strStart;
            if (len > 255) len = 255;
            char alias[256] = { 0 };
            strncpy(alias, strStart, len);

            if (!hasPrimary) { strcpy(primary, alias); hasPrimary = TRUE; }
            if (FuzzyCompare(alias, target)) matchFound = TRUE;
            strStart = strEnd + 1;
        }

        if (matchFound) {
            const char* nextObj = strstr(endArr, "\"names\"");
            const char* exeProp = strstr(endArr, "\"exe\"");
            if (exeProp && (!nextObj || exeProp < nextObj)) {
                const char* exeStart = strchr(exeProp + 5, '"');
                if (exeStart) {
                    exeStart++;
                    const char* exeEnd = strchr(exeStart, '"');
                    if (exeEnd) {
                        size_t exeLen = exeEnd - exeStart;
                        if (exeLen > 255) exeLen = 255;
                        memset(outExe, 0, 256);
                        strncpy(outExe, exeStart, exeLen);
                        strcpy(outPrimary, primary);

                        char *read = outExe, *write = outExe;
                        while (*read) {
                            if (*read == '\\' && *(read + 1) == '\\') { *write++ = '\\'; read += 2; }
                            else { *write++ = *read++; }
                        }
                        *write = '\0';
                        return TRUE;
                    }
                }
            }
        }
        p = endArr;
    }
    return FALSE;
}

void ProcessQueueBaton(BOOL abortQueue) {
    char cmd[MAX_PATH * 3];
    char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);

    if (!abortQueue && qCurrent < qTotal) {

        FILE* f = fopen(queueFilePath, "r");
        char nextBaseDir[MAX_PATH] = { 0 };

        if (f) {
            char fileData[8192] = { 0 };
            fread(fileData, 1, 8192, f);
            fclose(f);

            char searchStr[32]; sprintf(searchStr, "CURRENT=%d", qCurrent);
            char replaceStr[32]; sprintf(replaceStr, "CURRENT=%d", qCurrent + 1);
            char* pos = strstr(fileData, searchStr);
            if (pos) strncpy(pos, replaceStr, strlen(replaceStr));

            f = fopen(queueFilePath, "w");
            if (f) { fwrite(fileData, 1, strlen(fileData), f); fclose(f); }


            char nextTarget[32]; sprintf(nextTarget, "\n%d|", qCurrent + 1);
            char* line = strstr(fileData, nextTarget);
            if (line) {
                line++;
                char tIdx[16], dName[256], fName[256], ePath[256], tSec[32];
                sscanf(line, "%[^|]|%[^|]|%[^|]|%[^|]|%s", tIdx, dName, fName, ePath, tSec);

                char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
                char nextBaseDir[MAX_PATH]; sprintf(nextBaseDir, "%sDGE_%s", tempDir, fName);
                char nextExePath[MAX_PATH]; sprintf(nextExePath, "%s\\%s", nextBaseDir, ePath);

                char dirToCreate[MAX_PATH]; strcpy(dirToCreate, nextExePath);
                char* lastSlash = strrchr(dirToCreate, '\\');
                if (lastSlash) *lastSlash = '\0';
                SHCreateDirectoryExA(NULL, dirToCreate, NULL);

                char currentExe[MAX_PATH]; GetModuleFileNameA(NULL, currentExe, MAX_PATH);
                CopyFileA(currentExe, nextExePath, FALSE);

                char nextCmdLine[MAX_PATH * 3];
                sprintf(nextCmdLine, "\"%s\" -queue \"%s\"", nextExePath, queueFilePath);

                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi;
                CreateProcessA(NULL, nextCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            }
        }

        sprintf(cmd, "/c ping 127.0.0.1 -n 2 > nul & for /d %%x in (\"%sDGE_*\") do if /i not \"%%~fx\"==\"%s\" rmdir /s /q \"%%x\" & del /q /f \"%sDGE_*\"", tempDir, nextBaseDir, tempDir);
        ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
    } else {

        sprintf(cmd, "/c ping 127.0.0.1 -n 2 > nul & for /d %%x in (\"%sDGE_*\") do rmdir /s /q \"%%x\" & del /q /f \"%sDGE_*\" & del /q /f \"%s\"", tempDir, tempDir, queueFilePath);
        ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
    }
}

BOOL CALLBACK SetFontProc(HWND hwndChild, LPARAM lParam) {
    SendMessage(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        char qStr[64]; sprintf(qStr, "Game %d of %d", qCurrent, qTotal);

        hQueueStatusLabel = CreateWindowA("STATIC", qStr, WS_CHILD | WS_VISIBLE | SS_CENTER, 27, 10, 240, 20, hwnd, NULL, NULL, NULL);

        hGameLabel = CreateWindowA("STATIC", currentGameName, WS_CHILD | WS_VISIBLE | SS_CENTER, 27, 35, 240, 20, hwnd, NULL, NULL, NULL);

        hTimerLabel = CreateWindowA("STATIC", "Time Remaining: --", WS_CHILD | WS_VISIBLE | SS_CENTER, 27, 60, 240, 20, hwnd, NULL, NULL, NULL);
        hProgressLabel = CreateWindowA("STATIC", "Progress: 0%", WS_CHILD | WS_VISIBLE | SS_CENTER, 27, 85, 240, 20, hwnd, NULL, NULL, NULL);
        hBtnCancel = CreateWindowA("BUTTON", "Terminate", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 87, 120, 120, 30, hwnd, (HMENU)1, NULL, NULL);

        SetTimer(hwnd, 1, 1000, NULL);
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFont);

        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        break;
    }
    case WM_TIMER: {
        timeLeft--;
        if (timeLeft <= 0) { finishedNaturally = TRUE; PostMessage(hwnd, WM_CLOSE, 0, 0); return 0; }

        char buf[64];
        sprintf(buf, "Time Remaining: %dm %02ds", timeLeft / 60, timeLeft % 60);
        SetWindowTextA(hTimerLabel, buf);

        int percent = (int)(((float)(totalTime - timeLeft) / (float)totalTime) * 100.0f);
        sprintf(buf, "Progress: %d%%", percent);
        SetWindowTextA(hProgressLabel, buf);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT p = (LPDRAWITEMSTRUCT)lParam;
        FillRect(p->hDC, &p->rcItem, hBtnBrush);
        SetBkMode(p->hDC, TRANSPARENT); SetTextColor(p->hDC, clrText);

        char btnText[32]; GetWindowTextA(p->hwndItem, btnText, 32);
        DrawTextA(p->hDC, btnText, -1, &p->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wParam, clrText); SetBkColor((HDC)wParam, clrBg); return (LRESULT)hBgBrush;
    case WM_CLOSE:
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (isSilent) {

            char cmd[MAX_PATH * 2];
            sprintf(cmd, "/c ping 127.0.0.1 -n 2 > nul & rmdir /s /q \"%s\"", dgeFolderPath);
            ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
            PostQuitMessage(0);
        } else {
            ProcessQueueBaton(!finishedNaturally);
            PostQuitMessage(0);
        }
        break;
    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    case WM_ACTIVATE:

        if (LOWORD(wParam) == WA_INACTIVE) {
            SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        }
        break;
    }
    return 0;
}

DWORD WINAPI BackgroundDownloadThread(LPVOID lpParam) {
    globalJsonData = FetchJSON(JSON_URL);
    return 0;
}

void UpdateSimulList(HWND hwnd, HWND hList);
void ExecuteQueue(HWND hwnd, int executeMode) {
    char currentExe[MAX_PATH];
    GetModuleFileNameA(NULL, currentExe, MAX_PATH);

    if (executeMode == 0) {
        char gInput[256], cExe[256], tInput[32];
        GetWindowTextA(hGameName, gInput, 256); GetWindowTextA(hCustomExe, cExe, 256); GetWindowTextA(hTime, tInput, 32);

        char tempExe[512] = { 0 };
            NormalizePath(tempExe, cExe);
            strcpy(cExe, tempExe);

        const char* mathPtr = tInput; int t = EvalExpr(&mathPtr);
        if (t <= 0 || (strlen(gInput) == 0 && strlen(cExe) == 0)) {
            MessageBoxA(hwnd, "Please enter a Game Name or Custom EXE, and a valid Time.", "Error", MB_ICONERROR | MB_OK); return;
        }
        strcpy(queue[0].gameName, gInput); strcpy(queue[0].customExe, cExe); queue[0].timeSec = t;
        queueCount = 1;
    } else {
        if (queueCount == 0) { MessageBoxA(hwnd, "Queue is empty!", "Error", MB_ICONERROR | MB_OK); return; }
    }
    char fileBuf[8192] = { 0 };
    sprintf(fileBuf, "TOTAL=%d\nCURRENT=1\n", queueCount);

    for (int i = 0; i < queueCount; i++) {
        char primaryName[256] = { 0 }, exePath[256] = { 0 };
        if (strlen(queue[i].customExe) > 0) {
            strcpy(exePath, queue[i].customExe);
            strcpy(primaryName, strlen(queue[i].gameName) > 0 ? queue[i].gameName : "CustomGame");
        }
        else {
            int waitLoops = 0;
            while (!globalJsonData && waitLoops < 50) { Sleep(100); waitLoops++; }

            if (globalJsonData) {
                ParseGame(globalJsonData, queue[i].gameName, primaryName, exePath);
            }
        }


        if (strlen(exePath) == 0) {
            char err[512]; sprintf(err, "Could not find path for game: '%s'. Check for spelling errors, entering a custom EXE path or finding your game in the game database", queue[i].gameName);
            MessageBoxA(hwnd, err, "Error", MB_ICONERROR | MB_OK);
            if (executeMode == 0) queueCount = 0;
            return;
        }
        char folderName[256];
        char* r = primaryName; char* w = folderName;
        while (*r) {
            if (*r != ':' && *r != ';' && *r != '<' && *r != '>' && *r != '"' && *r != '/' && *r != '\\' && *r != '|' && *r != '?' && *r != '*') {
                *w++ = *r;
            }
            r++;
        }
        *w = '\0';
        if (executeMode == 2) {
            char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
            sprintf(simulGames[i].folderPath, "%sDGE_%s", tempDir, folderName);
            strcpy(simulGames[i].exePath, exePath);
            strcpy(simulGames[i].displayName, primaryName);
            simulGames[i].timeSec = queue[i].timeSec;
            simulGames[i].active = TRUE;
        }
        sprintf(fileBuf + strlen(fileBuf), "%d|%s|%s|%s|%d\n", i + 1, primaryName, folderName, exePath, queue[i].timeSec);
    }
    if (executeMode == 2) {
        if (globalJsonData) {
            free(globalJsonData);
            globalJsonData = NULL;
        }
        if (hSearchWnd) {
            DestroyWindow(hSearchWnd);
            hSearchWnd = NULL;
        }
        isSimulManager = TRUE;
        SetWindowPos(hwnd, NULL, 0, 0, 360, 420, SWP_NOMOVE | SWP_NOZORDER);
        ShowWindow(hGameName, SW_HIDE); ShowWindow(hCustomExe, SW_HIDE); ShowWindow(hTime, SW_HIDE);
        ShowWindow(hBtnAddQueue, SW_HIDE); ShowWindow(hBtnRemoveQueue, SW_HIDE); ShowWindow(hBtnStartQueue, SW_HIDE);
        ShowWindow(hBtnToggleQueue, SW_HIDE); ShowWindow(hBtnSearch, SW_HIDE); ShowWindow(hQueueLabel, SW_HIDE);
        SetWindowPos(hListBox, NULL, 27, 20, 300, 300, SWP_NOZORDER);
        ShowWindow(hBtnTerminateSimul, SW_SHOW);
        ShowWindow(hBtnTerminateAllSimul, SW_SHOW);
        SetWindowPos(hBtnTerminateSimul, NULL, 27, 330, 145, 30, SWP_NOZORDER);
        SetWindowPos(hBtnTerminateAllSimul, NULL, 182, 330, 145, 30, SWP_NOZORDER);

        for(int i=0; i<queueCount; i++) {
            char dirToCreate[MAX_PATH];

            sprintf(dirToCreate, "%s\\%s", simulGames[i].folderPath, simulGames[i].exePath);
            char* lastSlash = strrchr(dirToCreate, '\\');
            if (lastSlash) *lastSlash = '\0';
            SHCreateDirectoryExA(NULL, dirToCreate, NULL);

            char targetExe[MAX_PATH];
            sprintf(targetExe, "%s\\%s", simulGames[i].folderPath, simulGames[i].exePath);
            CopyFileA(currentExe, targetExe, FALSE);

            char cmdLine[MAX_PATH * 3];
            sprintf(cmdLine, "\"%s\" -silent %d \"%s\"", targetExe, simulGames[i].timeSec, simulGames[i].folderPath);

            STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi;
            if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                simulGames[i].hProcess = pi.hProcess;
                CloseHandle(pi.hThread);
            }
        }

        SetTimer(hwnd, 2, 1000, NULL);
        UpdateSimulList(hwnd, hListBox);
        SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        return;
    }

    char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
    sprintf(queueFilePath, "%sQueueSession.txt", tempDir);
    FILE* f = fopen(queueFilePath, "w");
    if (f) { fwrite(fileBuf, 1, strlen(fileBuf), f); fclose(f); }
    char dName[256], fName[256], ePath[256];
    char searchStr[16]; sprintf(searchStr, "\n1|");
    char* line1 = strstr(fileBuf, searchStr);
    sscanf(line1 + 3, "%[^|]|%[^|]|%[^|]", dName, fName, ePath);
    char baseDgeFolder[MAX_PATH]; sprintf(baseDgeFolder, "%sDGE_%s", tempDir, fName);
    char fullExePath[MAX_PATH]; sprintf(fullExePath, "%s\\%s", baseDgeFolder, ePath);
    char dirToCreate[MAX_PATH]; strcpy(dirToCreate, fullExePath);
    char* lastSlash = strrchr(dirToCreate, '\\');
    if (lastSlash) *lastSlash = '\0';
    SHCreateDirectoryExA(NULL, dirToCreate, NULL);
    CopyFileA(currentExe, fullExePath, FALSE);
    char cmdLine[MAX_PATH * 3];
    sprintf(cmdLine, "\"%s\" -queue \"%s\"", fullExePath, queueFilePath);
    STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        PostQuitMessage(0);
    }
}

typedef struct {
    char name[256];
    int score;
} SearchResult;

int CompareResults(const void* a, const void* b) {
    SearchResult* r1 = (SearchResult*)a;
    SearchResult* r2 = (SearchResult*)b;
    if (r1->score != r2->score) return r2->score - r1->score;
    return lstrcmpiA(r1->name, r2->name);
}

int FuzzyScore(const char* target, const char* query) {
    if (!query || !*query) return 1;

    char nT[512] = {0}, nQ[512] = {0};
    int i = 0, j = 0;

    for (const char* p = target; *p && i < 511; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80)
            nT[i++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    }
    for (const char* p = query; *p && j < 511; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80)
            nQ[j++] = (c >= 'A' && c <= 'Z') ? c + 32 : c;
    }

    if (j == 0) return 1;

    if (strcmp(nT, nQ) == 0) return 4;

    BOOL isPrefix = TRUE;
    for (int k = 0; k < j; k++) {
        if (nT[k] != nQ[k]) { isPrefix = FALSE; break; }
    }
    if (isPrefix) return 3;


    if (strstr(nT, nQ) != NULL) return 2;

    return 0;
}

void PerformSearch(HWND hList, const char* query) {
    SendMessageA(hList, WM_SETREDRAW, FALSE, 0);
    SendMessageA(hList, LB_RESETCONTENT, 0, 0);

    if (!globalJsonData) {
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"Loading database...");
        SendMessageA(hList, WM_SETREDRAW, TRUE, 0);
        return;
    }

    int maxRes = 1000;
    SearchResult* results = (SearchResult*)malloc(sizeof(SearchResult) * maxRes);
    int resCount = 0;


    BOOL hasQuery = FALSE;
    if (query) {
        for (const char* p = query; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80) {
                hasQuery = TRUE;
                break;
            }
        }
    }

    const char* p = globalJsonData;
    while (p && (p = strstr(p, "\"names\"")) != NULL) {
        p = strchr(p, '[');
        if (!p) break;
        const char* endArr = strchr(p, ']');
        if (!endArr) break;

        char primary[256] = { 0 };
        BOOL hasPrimary = FALSE;
        int bestScore = 0;
        const char* strStart = p;

        while ((strStart = strchr(strStart, '"')) != NULL && strStart < endArr) {
            strStart++;
            const char* strEnd = strchr(strStart, '"');
            if (!strEnd || strEnd > endArr) break;

            size_t len = strEnd - strStart;
            if (len > 255) len = 255;
            char alias[256] = { 0 };
            strncpy(alias, strStart, len);

            if (!hasPrimary) { strcpy(primary, alias); hasPrimary = TRUE; }

            int score = FuzzyScore(alias, query);
            if (score > bestScore) bestScore = score;

            strStart = strEnd + 1;
        }

        if (bestScore > 0 && hasPrimary) {
            if (resCount >= maxRes) {
                maxRes *= 2;
                results = (SearchResult*)realloc(results, sizeof(SearchResult) * maxRes);
            }
            strcpy(results[resCount].name, primary);
            results[resCount].score = bestScore;
            resCount++;
        }
        p = endArr;
    }

    if (resCount > 0) {


        if (hasQuery) {
            qsort(results, resCount, sizeof(SearchResult), CompareResults);
        }

        for (int i = 0; i < resCount; i++) {
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)results[i].name);
        }
    }

    free(results);
    SendMessageA(hList, WM_SETREDRAW, TRUE, 0);
}

LRESULT CALLBACK SearchWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowA("STATIC", "Search Game Database:", WS_CHILD | WS_VISIBLE, 12, 10, 200, 20, hwnd, NULL, NULL, NULL);
        hSearchEdit = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 12, 30, 270, 22, hwnd, (HMENU)101, NULL, NULL);
        hSearchList = CreateWindowA("LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 12, 60, 270, 280, hwnd, (HMENU)102, NULL, NULL);
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFont);
        if (!globalJsonData) SetTimer(hwnd, 1, 200, NULL);
        PerformSearch(hSearchList, "");
        break;
    }
    case WM_TIMER: {

        if (wParam == 1 && globalJsonData) {
            KillTimer(hwnd, 1);
            char query[256];
            GetWindowTextA(hSearchEdit, query, 256);
            PerformSearch(hSearchList, query);
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);


        if (id == 101 && code == EN_CHANGE) {
            char query[256];
            GetWindowTextA(hSearchEdit, query, 256);
            PerformSearch(hSearchList, query);
        }

        else if (id == 102 && code == LBN_DBLCLK) {
            int sel = SendMessageA(hSearchList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                char selectedGame[256] = { 0 };
                SendMessageA(hSearchList, LB_GETTEXT, sel, (LPARAM)selectedGame);
                SetWindowTextA(hGameName, selectedGame);
                char dummyName[256] = { 0 }, exePath[256] = { 0 };
                if (globalJsonData && ParseGame(globalJsonData, selectedGame, dummyName, exePath)) {
                    SetWindowTextA(hCustomExe, exePath);
                }
                DestroyWindow(hwnd);
                hSearchWnd = NULL;
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wParam, clrText); SetBkColor((HDC)wParam, clrBg); return (LRESULT)hBgBrush;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wParam, clrText); SetBkColor((HDC)wParam, clrEditBg); return (LRESULT)hEditBrush;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        hSearchWnd = NULL;
        break;
    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void UpdateSimulList(HWND hwnd, HWND hList) {
    SendMessageA(hList, WM_SETREDRAW, FALSE, 0);
    SendMessageA(hList, LB_RESETCONTENT, 0, 0);
    int activeCount = 0;

    for (int i = 0; i < queueCount; i++) {
        if (simulGames[i].active) {
            activeCount++;
            char buf[300];
            sprintf(buf, "[%dm %02ds] %s", simulGames[i].timeSec / 60, simulGames[i].timeSec % 60, simulGames[i].displayName);
            int idx = SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)buf);
            SendMessageA(hList, LB_SETITEMDATA, idx, i);
        }
    }
    SendMessageA(hList, WM_SETREDRAW, TRUE, 0);
    if (activeCount == 0) {
        char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
        char cleanCmd[MAX_PATH * 3];
        sprintf(cleanCmd, "/c ping 127.0.0.1 -n 2 > nul & for /d %%x in (\"%sDGE_*\") do rmdir /s /q \"%%x\" & del /q /f \"%sDGE_*\"", tempDir, tempDir);
        ShellExecuteA(NULL, "open", "cmd.exe", cleanCmd, NULL, SW_HIDE);
        PostQuitMessage(0);
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowA("STATIC", "Game Name (or alias):", WS_CHILD | WS_VISIBLE, 27, 25, 200, 20, hwnd, NULL, NULL, NULL);
        hGameName = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 27, 45, 240, 22, hwnd, NULL, NULL, NULL);
        CreateWindowA("STATIC", "Custom EXE Path (Optional):", WS_CHILD | WS_VISIBLE, 27, 75, 200, 20, hwnd, NULL, NULL, NULL);
        hCustomExe = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 27, 95, 240, 22, hwnd, NULL, NULL, NULL);
        CreateWindowA("STATIC", "Time (Seconds or Math):", WS_CHILD | WS_VISIBLE, 27, 125, 200, 20, hwnd, NULL, NULL, NULL);
        hTime = CreateWindowA("EDIT", "910", WS_CHILD | WS_VISIBLE | WS_BORDER, 27, 145, 100, 22, hwnd, NULL, NULL, NULL);
        hBtnLaunch = CreateWindowA("BUTTON", "Emulate", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 87, 185, 120, 30, hwnd, (HMENU)2, NULL, NULL);
        hBtnToggleQueue = CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 255, 10, 25, 25, hwnd, (HMENU)3, NULL, NULL);
        char* verData = FetchJSON(VERSION_URL);
        BOOL updateFound = FALSE;
        if (verData) {
            float remoteVer;
            if (sscanf(verData, "%f\n%s", &remoteVer, updateUrl) == 2) {
                if (remoteVer > APP_VERSION) updateFound = TRUE;
            }
            free(verData);
        }
        hBtnUpdate = CreateWindowA("BUTTON", "", WS_CHILD | (updateFound ? WS_VISIBLE : 0) | BS_OWNERDRAW, 195, 10, 25, 25, hwnd, (HMENU)7, NULL, NULL);
        hBtnSearch = CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 225, 10, 25, 25, hwnd, (HMENU)8, NULL, NULL);
        hQueueLabel = CreateWindowA("STATIC", "Emulation Queue:", WS_CHILD, 310, 20, 200, 20, hwnd, NULL, NULL, NULL);
        hListBox = CreateWindowA("LISTBOX", NULL, WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 310, 40, 250, 122, hwnd, NULL, NULL, NULL);
        hBtnAddQueue = CreateWindowA("BUTTON", "Add to Queue", WS_CHILD | BS_OWNERDRAW, 87, 185, 120, 30, hwnd, (HMENU)4, NULL, NULL);
        hBtnRemoveQueue = CreateWindowA("BUTTON", "Remove", WS_CHILD | BS_OWNERDRAW, 310, 180, 90, 30, hwnd, (HMENU)5, NULL, NULL);
        hBtnStartQueue = CreateWindowA("BUTTON", "Start Queue", WS_CHILD | BS_OWNERDRAW, 460, 180, 100, 30, hwnd, (HMENU)6, NULL, NULL);
        hBtnTerminateSimul = CreateWindowA("BUTTON", "Terminate Selected", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)9, NULL, NULL);
        hBtnTerminateAllSimul = CreateWindowA("BUTTON", "Terminate All", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, (HMENU)10, NULL, NULL);
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFont);
        break;
    }

    case WM_TIMER: {
        if (wParam == 2 && isSimulManager) {
            BOOL changed = FALSE;
            for (int i = 0; i < queueCount; i++) {
                if (simulGames[i].active) {
                    simulGames[i].timeSec--;
                    if (simulGames[i].timeSec <= 0 || WaitForSingleObject(simulGames[i].hProcess, 0) == WAIT_OBJECT_0) {
                        simulGames[i].active = FALSE;
                        changed = TRUE;
                    } else {
                        changed = TRUE;
                    }
                }
            }
            if (changed) {
                int sel = SendMessageA(hListBox, LB_GETCURSEL, 0, 0);
                UpdateSimulList(hwnd, hListBox);
                if (sel != LB_ERR) SendMessageA(hListBox, LB_SETCURSEL, sel, 0);
            }
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 2) { ExecuteQueue(hwnd, 0); }
        else if (id == 6) { ExecuteQueue(hwnd, appMode); }
        else if (id == 3) {
            appMode++;
            if (appMode > 2) appMode = 0;

            if (appMode == 0) {
                SetWindowPos(hwnd, NULL, 0, 0, 300, 280, SWP_NOMOVE | SWP_NOZORDER);
                ShowWindow(hBtnLaunch, SW_SHOW); ShowWindow(hBtnAddQueue, SW_HIDE); ShowWindow(hListBox, SW_HIDE);
                ShowWindow(hQueueLabel, SW_HIDE); ShowWindow(hBtnRemoveQueue, SW_HIDE); ShowWindow(hBtnStartQueue, SW_HIDE);
            }
            else {
                SetWindowPos(hwnd, NULL, 0, 0, 600, 280, SWP_NOMOVE | SWP_NOZORDER);
                ShowWindow(hBtnLaunch, SW_HIDE); ShowWindow(hBtnAddQueue, SW_SHOW); ShowWindow(hListBox, SW_SHOW);
                ShowWindow(hQueueLabel, SW_SHOW); ShowWindow(hBtnRemoveQueue, SW_SHOW); ShowWindow(hBtnStartQueue, SW_SHOW);

                if (appMode == 1) {
                    SetWindowTextA(hBtnAddQueue, "Add to Queue");
                    SetWindowTextA(hBtnStartQueue, "Start Queue");
                    SetWindowTextA(hQueueLabel, "Queued Emulation List:");
                } else {
                    SetWindowTextA(hBtnAddQueue, "Add to List");
                    SetWindowTextA(hBtnStartQueue, "Start All");
                    SetWindowTextA(hQueueLabel, "Simultaneous Emulation List:");
                }
                
                InvalidateRect(hBtnAddQueue, NULL, TRUE);
                InvalidateRect(hBtnStartQueue, NULL, TRUE);
                InvalidateRect(hQueueLabel, NULL, TRUE);
            }
            InvalidateRect(hBtnToggleQueue, NULL, TRUE);
        }
        else if (id == 4) {
            if (queueCount >= 100) return 0;
            char gInput[256], cExe[256], tInput[32];
            GetWindowTextA(hGameName, gInput, 256); GetWindowTextA(hCustomExe, cExe, 256); GetWindowTextA(hTime, tInput, 32);

            char tempExe[512] = { 0 };
            NormalizePath(tempExe, cExe);
            strcpy(cExe, tempExe);

            const char* mathPtr = tInput; int t = EvalExpr(&mathPtr);
            if (t <= 0 || (strlen(gInput) == 0 && strlen(cExe) == 0)) {
                MessageBoxA(hwnd, "Invalid Entry.", "Error", MB_ICONERROR | MB_OK); break;
            }

            strcpy(queue[queueCount].gameName, gInput);
            strcpy(queue[queueCount].customExe, cExe);
            queue[queueCount].timeSec = t;
            queueCount++;
            char listStr[300];
            char* dispName = strlen(gInput) > 0 ? gInput : cExe;
            sprintf(listStr, "[%dm %02ds] %s", t / 60, t % 60, dispName);
            SendMessageA(hListBox, LB_ADDSTRING, 0, (LPARAM)listStr);
            SetWindowTextA(hGameName, ""); SetWindowTextA(hCustomExe, "");
        }
        else if (id == 5) {
            int sel = SendMessageA(hListBox, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                SendMessageA(hListBox, LB_DELETESTRING, sel, 0);
                for (int i = sel; i < queueCount - 1; i++) queue[i] = queue[i + 1];
                queueCount--;
            }
        }
        else if (id == 7) {
            if (MessageBoxA(hwnd, "A new update is available! Do you want to download and restart the app?", "Update Available", MB_ICONQUESTION | MB_YESNO) == IDYES) {


                char currentExe[MAX_PATH]; GetModuleFileNameA(NULL, currentExe, MAX_PATH);


                char updateExe[MAX_PATH]; strcpy(updateExe, currentExe);
                char* lastSlash = strrchr(updateExe, '\\');
                if (lastSlash) strcpy(lastSlash + 1, "DGE_UpdateTemp.exe");

                char cmdStr[2048];
                sprintf(cmdStr, "/c curl -s -L -o \"%s\" \"%s\" & ping 127.0.0.1 -n 2 > nul & move /y \"%s\" \"%s\" & start \"\" \"%s\"",
                    updateExe, updateUrl, updateExe, currentExe, currentExe);


                ShellExecuteA(NULL, "open", "cmd.exe", cmdStr, NULL, SW_HIDE);
                PostQuitMessage(0);
            }
        }
        else if (id == 8) {
            if (!hSearchWnd) {
                WNDCLASSA swc = { 0 };
                swc.lpfnWndProc = SearchWndProc;
                swc.hInstance = (HINSTANCE)GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
                swc.hbrBackground = hBgBrush;
                swc.lpszClassName = "DgeSearchClass";
                swc.hIcon = (HICON)SendMessageA(hwnd, WM_GETICON, ICON_SMALL, 0);
                RegisterClassA(&swc);
                hSearchWnd = CreateWindowA("DgeSearchClass", "Game Library",
                    WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                    CW_USEDEFAULT, CW_USEDEFAULT, 300, 369, hwnd, NULL, swc.hInstance, NULL);
                int useImmersiveDarkMode = 1;
                DwmSetWindowAttribute(hSearchWnd, 20, &useImmersiveDarkMode, sizeof(useImmersiveDarkMode));
                ShowWindow(hSearchWnd, SW_SHOW);
                UpdateWindow(hSearchWnd);
                SetFocus(hSearchEdit);
            } else {
                SetForegroundWindow(hSearchWnd);
                SetFocus(hSearchEdit);
            }
        }
        else if (id == 9) {
            int sel = SendMessageA(hListBox, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                int i = SendMessageA(hListBox, LB_GETITEMDATA, sel, 0);
                if (simulGames[i].active) {
                    TerminateProcess(simulGames[i].hProcess, 0);
                    simulGames[i].active = FALSE;
                    char cmd[MAX_PATH * 2];
                    sprintf(cmd, "/c rmdir /s /q \"%s\"", simulGames[i].folderPath);
                    ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
                    UpdateSimulList(hwnd, hListBox);
                }
            }
        }
        else if (id == 10) {
            for (int i = 0; i < queueCount; i++) {
                if (simulGames[i].active) {
                    TerminateProcess(simulGames[i].hProcess, 0);
                    simulGames[i].active = FALSE;
                }
            }

            UpdateSimulList(hwnd, hListBox);
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wParam, clrText); SetBkColor((HDC)wParam, clrBg); return (LRESULT)hBgBrush;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wParam, clrText); SetBkColor((HDC)wParam, clrEditBg); return (LRESULT)hEditBrush;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT p = (LPDRAWITEMSTRUCT)lParam;
        FillRect(p->hDC, &p->rcItem, hBtnBrush);

        if (p->CtlID == 7) {

            HPEN hPen = CreatePen(PS_SOLID, 2, RGB(50, 220, 50));
            HPEN hOldPen = SelectObject(p->hDC, hPen);
            MoveToEx(p->hDC, 12, 6, NULL); LineTo(p->hDC, 12, 16);
            MoveToEx(p->hDC, 7, 11, NULL); LineTo(p->hDC, 13, 17);
            MoveToEx(p->hDC, 17, 11, NULL); LineTo(p->hDC, 11, 17);
            MoveToEx(p->hDC, 6, 19, NULL); LineTo(p->hDC, 19, 19);
            SelectObject(p->hDC, hOldPen);
            DeleteObject(hPen);
        }
        else if (p->CtlID == 3) {
            if (appMode == 0) {
                HPEN hPen = CreatePen(PS_SOLID, 2, clrText);
                HPEN hOldPen = (HPEN)SelectObject(p->hDC, hPen);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(p->hDC, GetStockObject(NULL_BRUSH));
                Ellipse(p->hDC, 4, 4, 21, 21);
                SetBkMode(p->hDC, TRANSPARENT);
                SetTextColor(p->hDC, clrText);
                DrawTextA(p->hDC, "1", -1, &p->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                SelectObject(p->hDC, hOldBrush);
                SelectObject(p->hDC, hOldPen);
                DeleteObject(hPen);
            }
            else if (appMode == 1) {
                HBRUSH hIconBrush = CreateSolidBrush(clrText);
                HPEN hNullPen = CreatePen(PS_NULL, 0, 0);
                HPEN hOldPen = (HPEN)SelectObject(p->hDC, hNullPen);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(p->hDC, hIconBrush);
                POINT playTri[3] = { { 6, 5 }, { 6, 12 }, { 11, 8 } };
                Polygon(p->hDC, playTri, 3);
                RECT l1 = { 13, 8, 20, 10 };
                RECT l2 = { 6, 13, 20, 15 };
                RECT l3 = { 6, 18, 20, 20 };
                FillRect(p->hDC, &l1, hIconBrush);
                FillRect(p->hDC, &l2, hIconBrush);
                FillRect(p->hDC, &l3, hIconBrush);
                SelectObject(p->hDC, hOldBrush);
                SelectObject(p->hDC, hOldPen);
                DeleteObject(hNullPen);
                DeleteObject(hIconBrush);
            }
            else if (appMode == 2) {

                HPEN hPen = CreatePen(PS_SOLID, 2, clrText);
                HPEN hOldPen = (HPEN)SelectObject(p->hDC, hPen);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(p->hDC, GetStockObject(NULL_BRUSH));
                Rectangle(p->hDC, 6, 6, 16, 16);
                SelectObject(p->hDC, hBtnBrush);
                Rectangle(p->hDC, 10, 10, 20, 20);
                SelectObject(p->hDC, hOldBrush);
                SelectObject(p->hDC, hOldPen);
                DeleteObject(hPen);
            }
        }
        else if (p->CtlID == 8) {
            HPEN hPen = CreatePen(PS_SOLID, 2, clrText);
            HPEN hOldPen = SelectObject(p->hDC, hPen);
            HBRUSH hOldBrush = SelectObject(p->hDC, GetStockObject(NULL_BRUSH));
            Ellipse(p->hDC, 5, 5, 17, 17);
            MoveToEx(p->hDC, 15, 15, NULL); LineTo(p->hDC, 19, 19);
            SelectObject(p->hDC, hOldBrush);
            SelectObject(p->hDC, hOldPen);
            DeleteObject(hPen);
        }
        else if (p->CtlID == 10) {
            SetBkMode(p->hDC, TRANSPARENT);
            SetTextColor(p->hDC, RGB(232, 38, 47));
            char btnText[32]; GetWindowTextA(p->hwndItem, btnText, 32);
            DrawTextA(p->hDC, btnText, -1, &p->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        else {
            SetBkMode(p->hDC, TRANSPARENT);
            SetTextColor(p->hDC, clrText);
            char btnText[32]; GetWindowTextA(p->hwndItem, btnText, 32);
            DrawTextA(p->hDC, btnText, -1, &p->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        return TRUE;
    }
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE) {
            SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
        }
        break;
    case WM_CLOSE:
        if (isSimulManager) {
            KillTimer(hwnd, 2);
            for (int i = 0; i < queueCount; i++) {
                if (simulGames[i].active && simulGames[i].hProcess) {
                    TerminateProcess(simulGames[i].hProcess, 0);
                    CloseHandle(simulGames[i].hProcess);
                    simulGames[i].active = FALSE;
                }
            }
            
            char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
            char cleanCmd[MAX_PATH * 3];
            sprintf(cleanCmd, "/c ping 127.0.0.1 -n 2 > nul & for /d %%x in (\"%sDGE_*\") do rmdir /s /q \"%%x\" & del /q /f \"%sDGE_*\"", tempDir, tempDir);
            ShellExecuteA(NULL, "open", "cmd.exe", cleanCmd, NULL, SW_HIDE);
        }
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int showCmd) {
    hBgBrush = CreateSolidBrush(clrBg);
    hBtnBrush = CreateSolidBrush(RGB(70, 70, 70));
    hFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    BOOL isDummy = FALSE;

    if (argc >= 3) {
        char arg1[32]; WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, arg1, 32, NULL, NULL);
        if (strcmp(arg1, "-silent") == 0) {
            isDummy = TRUE;
            isSilent = TRUE;
            totalTime = _wtoi(argv[2]);
            timeLeft = totalTime;
            WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, dgeFolderPath, MAX_PATH, NULL, NULL);
        }
        else if (strcmp(arg1, "-queue") == 0) {
            isDummy = TRUE;
            WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, queueFilePath, MAX_PATH, NULL, NULL);
            FILE* f = fopen(queueFilePath, "r");
            if (f) {
                char fileData[8192] = { 0 }; fread(fileData, 1, 8192, f); fclose(f);
                char* pTotal = strstr(fileData, "TOTAL="); if (pTotal) qTotal = atoi(pTotal + 6);
                char* pCur = strstr(fileData, "CURRENT="); if (pCur) qCurrent = atoi(pCur + 8);
                char targetLine[32]; sprintf(targetLine, "\n%d|", qCurrent);
                char* line = strstr(fileData, targetLine);
                if (line) {
                    char tIdx[16], dName[256], fName[256], ePath[256], tSec[32];
                    sscanf(line + 1, "%[^|]|%[^|]|%[^|]|%[^|]|%s", tIdx, dName, fName, ePath, tSec);
                    if (strcmp(dName, "CustomGame") == 0) {
                        char *r = ePath, *w = currentGameName;
                        while (*r) {
                            if (*r == '\\' && *(r + 1) == '\\') { *w++ = '\\'; r += 2; }
                            else { *w++ = *r++; }
                        }
                        *w = '\0';
                    } else {
                        strcpy(currentGameName, dName);
                    }
                    totalTime = atoi(tSec); timeLeft = totalTime;
                    char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
                    sprintf(dgeFolderPath, "%sDGE_%s", tempDir, fName);
                }
            }
        }
    }
    LocalFree(argv);
    if (!isDummy) {
        hEditBrush = CreateSolidBrush(clrEditBg);
        char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
        char cleanCmd[MAX_PATH * 3];
        sprintf(cleanCmd, "/c for /d %%x in (\"%sDGE_*\") do rmdir /s /q \"%%x\" & del /q /f \"%sDGE_*\"", tempDir, tempDir);
        ShellExecuteA(NULL, "open", "cmd.exe", cleanCmd, NULL, SW_HIDE);
        CreateThread(NULL, 0, BackgroundDownloadThread, NULL, 0, NULL);
    }

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = isDummy ? DummyWndProc : MainWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(101));
    wc.hbrBackground = hBgBrush;
    wc.lpszClassName = isDummy ? "DgeDummyClass" : "DgeMainClass";
    RegisterClassA(&wc);

    DWORD exStyle = isSilent ? WS_EX_TOOLWINDOW : 0;
    int xPos = isSilent ? -10000 : CW_USEDEFAULT;
    int yPos = isSilent ? -10000 : CW_USEDEFAULT;

    HWND hwnd = CreateWindowExA(exStyle, wc.lpszClassName, isDummy ? "Game Session" : "Discord Game Emulator",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        xPos, yPos, 300, isDummy ? 200 : 280, NULL, NULL, hInst, NULL);

    int useImmersiveDarkMode = 1;
    DwmSetWindowAttribute(hwnd, 20, &useImmersiveDarkMode, sizeof(useImmersiveDarkMode));

    ShowWindow(hwnd, isSilent ? SW_SHOWNA : showCmd);
    UpdateWindow(hwnd);

    if (!isDummy) {
        SetFocus(hGameName);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!isDummy && msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND activeRoot = GetAncestor(msg.hwnd, GA_ROOT);
            if (activeRoot == hwnd) {
                SendMessageA(hwnd, WM_COMMAND, (appMode == 0) ? 2 : 4, 0);
                continue;
            }
            else if (hSearchWnd && activeRoot == hSearchWnd) {

                int sel = SendMessageA(hSearchList, LB_GETCURSEL, 0, 0);
                if (sel == LB_ERR) sel = 0;
                if (SendMessageA(hSearchList, LB_GETCOUNT, 0, 0) > 0) {
                    SendMessageA(hSearchList, LB_SETCURSEL, sel, 0);
                    SendMessageA(hSearchWnd, WM_COMMAND, MAKEWPARAM(102, LBN_DBLCLK), 0);
                }
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    DeleteObject(hBgBrush); DeleteObject(hBtnBrush); if (!isDummy) DeleteObject(hEditBrush); DeleteObject(hFont);
    if (globalJsonData) free(globalJsonData);
    return 0;
}
