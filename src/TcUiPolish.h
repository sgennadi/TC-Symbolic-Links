#pragma once
// UI-only helpers shared by native Win32 dialogs. No network or filesystem effects.
#include <algorithm>
#include <string>
#include <string_view>
namespace tcs::ui {
enum class Lang { English, Russian, Hebrew };
inline std::wstring Translate(Lang lang,const wchar_t* en,const wchar_t* ru,
                             const wchar_t* he) {
    return lang==Lang::Russian?ru:lang==Lang::Hebrew?he:en;
}
inline std::wstring Version(std::wstring_view raw) {
    if(raw.empty())return L"\u2014";
    if(raw.front()==L'v'||raw.front()==L'V')return std::wstring(raw);
    return L"v"+std::wstring(raw);
}
inline std::wstring UpdateButton(bool newer,bool reinstall,
                                 std::wstring_view available,Lang lang) {
    if(newer && !available.empty())
        return Translate(lang,L"Update to ",L"Обновить до ",L"עדכן לגרסה ")+
            Version(available);
    if(reinstall)return Translate(lang,L"Reinstall",L"Переустановить",L"התקן מחדש");
    return Translate(lang,L"Check for updates",L"Проверить обновления",
                     L"בדוק עדכונים");
}
inline std::wstring UpdateDescription(bool newer,bool reinstall,Lang lang) {
    if(newer)return Translate(lang,
        L"Download the official ZIP, verify SHA-256 and install after Total Commander closes.",
        L"Скачивание официального ZIP, проверка SHA-256; установка после закрытия Total Commander.",
        L"הורדת קובץ ZIP רשמי, אימות SHA-256 והתקנה לאחר סגירת Total Commander.");
    if(reinstall)return Translate(lang,
        L"Reinstall the same verified release without changing your TC commands.",
        L"Повторная установка проверенной версии без изменения команд Total Commander.",
        L"התקנה מחדש של אותה גרסה מאומתת בלי לשנות פקודות ב-Total Commander.");
    return Translate(lang,
        L"Checks the official GitHub release channel. No files are changed.",
        L"Проверка официального канала GitHub. Файлы не изменяются.",
        L"בדיקת ערוץ ההפצה הרשמי של GitHub. לא משתנים קבצים.");
}
inline std::wstring EmptyLinksTitle(Lang lang,bool searched) {
    if(!searched)return Translate(lang,L"Explore your links",
        L"Проверьте свои ссылки",L"סקירת קישורים");
    return Translate(lang,L"No links found",L"Ссылки не найдены",
        L"לא נמצאו קישורים");
}
inline std::wstring EmptyLinksHelp(Lang lang,bool searched) {
    if(!searched)return Translate(lang,
        L"Choose a folder and click Find links to inspect existing symbolic links and hardlinks.",
        L"Выберите папку и нажмите «Найти ссылки» для проверки symlink и hardlink.",
        L"בחר תיקייה ולחץ על חפש קישורים כדי לבדוק קישורים קיימים.");
    return Translate(lang,
        L"Try another folder or increase the search depth. No files were changed.",
        L"Выберите другую папку или увеличьте глубину поиска. Файлы не изменены.",
        L"נסה תיקייה אחרת או הגדל את עומק החיפוש. לא שונו קבצים.");
}
// DIP dimensions for one compact, responsive non-resizable update window.
struct UpdateSize {int width;int height;};
inline UpdateSize UpdateWindowSize(int workWidth,int workHeight) {
    return {(std::max)(300,(std::min)(670,workWidth-24)),
            (std::max)(236,(std::min)(354,workHeight-24))};
}
} // namespace tcs::ui
