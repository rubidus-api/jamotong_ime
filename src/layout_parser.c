#include "layout_parser.h"
#include <stdio.h>
#include <string.h>

static void TrimCrLf(wchar_t *str) {
    size_t len = wcslen(str);
    while (len > 0 && (str[len - 1] == L'\n' || str[len - 1] == L'\r')) {
        str[len - 1] = L'\0';
        len--;
    }
}

bool LayoutParser_LoadJamo(JamotongConfig *config, const wchar_t *filepath) {
    if (config->layoutCount >= 8) return false; // Max layouts
    
    FILE *fp = _wfopen(filepath, L"r, ccs=UTF-8");
    if (!fp) return false;
    
    LayoutConfig newLayout = {0};
    newLayout.type = LAYOUT_TYPE_STATIC_MAP;
    
    // 기본적으로 자기 자신으로 매핑
    for (int i = 0; i < 256; i++) {
        newLayout.charMap[i] = (wchar_t)i;
    }
    
    wchar_t line[256];
    int section = 0; // 1 = Info, 2 = Map
    wchar_t nameBuf[64] = {0};
    
    while (fgetws(line, 256, fp)) {
        TrimCrLf(line);
        if (wcscmp(line, L"[Info]") == 0) section = 1;
        else if (wcscmp(line, L"[Map]") == 0) section = 2;
        else if (section == 1) {
            if (wcsncmp(line, L"Name=", 5) == 0) {
                wcscpy_s(nameBuf, 64, line + 5);
            }
        }
        else if (section == 2) {
            // q=' format
            if (wcslen(line) >= 3 && line[1] == L'=') {
                wchar_t src = line[0];
                wchar_t dst = line[2];
                if (src < 256) {
                    newLayout.charMap[src] = dst;
                }
            }
        }
    }
    fclose(fp);
    
    if (nameBuf[0] == L'\0') {
        wcscpy_s(nameBuf, 64, L"Custom_Map");
    }
    newLayout.name = _wcsdup(nameBuf);
    
    config->layouts[config->layoutCount++] = newLayout;
    return true;
}
