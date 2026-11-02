#pragma once
// Read-only, localized explanations for failed link creation.
// These are possible causes and manual checks, NOT server diagnoses.
// The helper never probes a server or changes ACLs, registry or Samba settings.
#include <windows.h>
#include <string>
#include <vector>

namespace tcs::help {
enum class Language { English, Russian, Hebrew };
struct Failure {
    DWORD code = ERROR_SUCCESS;
    bool smbDestination = false;
    bool directory = false;
    bool junction = false;
    bool hardlink = false;
    bool smbSource = false;
};
inline std::vector<std::wstring> Explain(const Failure& f, Language lang) {
    std::vector<std::wstring> result;
    auto add = [&](const wchar_t* en, const wchar_t* ru, const wchar_t* he) {
        result.emplace_back(lang == Language::Russian ? ru :
                            lang == Language::Hebrew ? he : en);
    };
    if (f.smbDestination) {
        add(L"CONTEXT: The link is being created on an SMB share (Windows or Samba); the server can refuse operations accepted by local NTFS.",
            L"КОНТЕКСТ: ссылка создаётся на SMB-ресурсе (Windows или Samba). Сервер может запретить операцию, доступную на локальном NTFS.",
            L"הקשר: הקישור נוצר בשיתוף SMB (Windows או Samba). השרת עשוי לדחות פעולה שפועלת ב-NTFS מקומי.");
    } else {
        add(L"CONTEXT: The link is being created on a local or non-SMB path.",
            L"КОНТЕКСТ: ссылка создаётся на локальном пути или не-SMB ресурсе.",
            L"הקשר: הקישור נוצר בנתיב מקומי או שאינו SMB.");
    }
    if (f.junction && f.smbDestination) {
        add(L"CAUSE: NTFS junctions are local mount points and cannot be created on an SMB share. Choose a directory symlink if supported.",
            L"ПРИЧИНА: NTFS Junction работает на локальном томе и не создаётся на SMB. Для SMB нужен Directory Symlink, если сервер его поддерживает.",
            L"סיבה: Junction של NTFS מקומי בלבד; לא ניתן ליצור אותו בשיתוף SMB. יש לבחור Directory Symlink אם השרת תומך בו.");
        return result;
    }

    switch (f.code) {
        case ERROR_ACCESS_DENIED:
            if (f.smbDestination && f.directory) {
                add(L"MEANING (5): The server or Windows denied creation of a directory symlink. This code does NOT identify which restriction caused it.",
                    L"ЗНАЧЕНИЕ (5): Windows или SMB-сервер отказал в создании ссылки на каталог. По одному коду нельзя установить точную причину.",
                    L"משמעות (5): Windows או שרת SMB דחו יצירת קישור לתיקייה. הקוד לבדו אינו מזהה את הסיבה.");
                add(L"CHECK 1: Confirm the signed-in SMB account can create entries in the destination directory; check share and filesystem ACLs with the administrator.",
                    L"ПРОВЕРИТЬ 1: может ли текущая учётная запись создавать объекты в целевой папке; проверить права SMB-шары и файловой системы с администратором.",
                    L"בדיקה 1: האם המשתמש יכול ליצור פריטים בתיקיית היעד; לבדוק הרשאות שיתוף ומערכת קבצים עם המנהל.");
                add(L"CHECK 2: Confirm the source is an actual directory, destination is a real filesystem folder (not inside WIM/ISO/ZIP), and the link name is free.",
                    L"ПРОВЕРИТЬ 2: источник действительно каталог, папка назначения реальная (не внутри WIM/ISO/ZIP), а имя будущей ссылки свободно.",
                    L"בדיקה 2: המקור הוא תיקייה אמיתית, היעד אינו בתוך WIM/ISO/ZIP, ושם הקישור פנוי.");
                add(L"CHECK 3: A working SMB FILE symlink does NOT prove support for DIRECTORY symlinks. Ask the server administrator about directory reparse-point support and SMB/Samba logs.",
                    L"ПРОВЕРИТЬ 3: работающий File Symlink на той же шаре НЕ доказывает поддержку Directory Symlink. Уточнить поддержку directory reparse point и журналы SMB/Samba.",
                    L"בדיקה 3: קישור לקובץ שעובד אינו מוכיח תמיכה בקישור לתיקייה. לבדוק תמיכה ויומני SMB/Samba.");
                add(L"CHECK 4: Check Windows symlink creation privilege / Developer Mode if relevant; Windows policy is not necessarily the cause of server error 5.",
                    L"ПРОВЕРИТЬ 4: право Windows на создание symlink / Developer Mode, если применимо. Это лишь возможная причина, не доказанная ошибкой 5.",
                    L"בדיקה 4: לבדוק הרשאת Windows ליצירת symlink או Developer Mode לפי הצורך. זו רק אפשרות, לא אבחנה.");
                add(L"CHECK 5: Compare behavior on a local NTFS folder and a Windows SMB share to isolate server limitations; do not change Samba configuration automatically.",
                    L"ПРОВЕРИТЬ 5: сравнить создание на локальном NTFS и Windows SMB, чтобы отделить ограничение сервера; автоматически ничего в Samba не менять.",
                    L"בדיקה 5: להשוות ל-NTFS מקומי ולשיתוף Windows SMB כדי לזהות מגבלת שרת; לא לשנות Samba אוטומטית.");
            } else if (f.smbDestination && f.hardlink) {
                add(L"MEANING (5): The SMB server rejected a hardlink. Hardlinks are not symbolic links or reparse points, and servers may restrict hardlink creation.",
                    L"ЗНАЧЕНИЕ (5): SMB-сервер запретил Hardlink. Это не symlink и не reparse point; сервер может ограничивать создание жёстких ссылок.",
                    L"משמעות (5): שרת SMB דחה Hardlink. זה אינו symlink או reparse point ושרתים עשויים להגביל קישורים קשיחים.");
                add(L"CHECK: Source and link must refer to the same underlying filesystem and server share; verify write permissions and SMB hardlink support.",
                    L"ПРОВЕРИТЬ: источник и ссылка должны быть на одной файловой системе и серверной шаре; проверить права и поддержку SMB Hardlink.",
                    L"בדיקה: המקור והיעד צריכים להיות באותה מערכת קבצים ושיתוף; לבדוק הרשאות ותמיכת SMB ב-Hardlink.");
            } else if (f.smbDestination) {
                add(L"MEANING (5): SMB file-link creation was denied; possible causes include share/filesystem permissions, server reparse support, Windows privileges or endpoint security.",
                    L"ЗНАЧЕНИЕ (5): создание файловой SMB-ссылки запрещено. Возможны права шары/каталога, ограничения reparse points, права Windows или защита конечной точки.",
                    L"משמעות (5): יצירת קישור קובץ ב-SMB נדחתה. ייתכנו בעיות הרשאות, reparse, Windows או אבטחת קצה.");
                add(L"CHECK: Verify destination folder write/create permission, logged-in SMB identity, server logs and whether the server supports file symlinks.",
                    L"ПРОВЕРИТЬ: создание файлов в целевой папке, учётную запись SMB, журналы сервера и поддержку файловых symlink.",
                    L"בדיקה: הרשאות יצירה בתיקיית היעד, זהות SMB, יומני השרת ותמיכה בקישורי קבצים.");
            } else {
                add(L"MEANING (5): Windows denied the operation. Potential causes: directory ACLs, policy or endpoint protection; this is not proof that admin rights are required.",
                    L"ЗНАЧЕНИЕ (5): Windows отказала в операции. Возможны ACL каталога, политики или защита компьютера. Это не доказательство необходимости запуска от администратора.",
                    L"משמעות (5): Windows דחתה את הפעולה. ייתכנו הרשאות, מדיניות או הגנת קצה; אין בכך הוכחה שדרוש מנהל.");
                add(L"CHECK: Confirm create/write permissions in the link's parent folder, local policy and any security-software event.",
                    L"ПРОВЕРИТЬ: права создания в родительской папке ссылки, локальные политики и события защитного ПО.",
                    L"בדיקה: הרשאת כתיבה בתיקיית האב, מדיניות מקומית ואירועי תוכנת אבטחה.");
            }
            break;
        case ERROR_PRIVILEGE_NOT_HELD:
            add(L"MEANING (1314): The Windows token lacks the privilege or mode required for symbolic-link creation.",
                L"ЗНАЧЕНИЕ (1314): у токена Windows нет необходимого права или режима создания символических ссылок.",
                L"משמעות (1314): לאסימון Windows חסרה הרשאה או אפשרות ליצירת קישורים סימבוליים.");
            add(L"CHECK: Windows Developer Mode or an administrator-approved SeCreateSymbolicLinkPrivilege; an SMB server may still reject the operation.",
                L"ПРОВЕРИТЬ: Windows Developer Mode либо назначенное администратором SeCreateSymbolicLinkPrivilege. SMB-сервер всё равно может отказать.",
                L"בדיקה: Developer Mode או הרשאת SeCreateSymbolicLinkPrivilege שאושרה על ידי מנהל. שרת SMB עדיין עשוי לדחות.");
            break;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS:
            add(L"MEANING (183/80): The future link name already exists (possibly a broken symlink or hidden entry), or source and link path are identical.",
                L"ЗНАЧЕНИЕ (183/80): имя будущей ссылки уже занято (в том числе скрытым объектом или битой ссылкой), либо пути источника и ссылки совпадают.",
                L"משמעות (183/80): שם הקישור כבר תפוס (כולל קישור שבור או פריט מוסתר) או שנתיב המקור זהה לנתיב הקישור.");
            add(L"CHECK: Inspect the destination parent in Total Commander, compare full source/link paths, and choose a different name or folder. Nothing will be overwritten.",
                L"ПРОВЕРИТЬ: содержимое папки назначения в TC, сравнить полные пути источника и ссылки, выбрать другое имя или папку. Перезаписи не будет.",
                L"בדיקה: לבדוק את תיקיית היעד ב-Total Commander, להשוות נתיבים ולבחור שם או תיקייה אחרים. לא תהיה דריסה.");
            break;
        case ERROR_NOT_SUPPORTED:
        case ERROR_INVALID_FUNCTION:
            add(L"MEANING (50/1): This filesystem, SMB server, link type or protocol path does not support the requested operation.",
                L"ЗНАЧЕНИЕ (50/1): файловая система, SMB-сервер, тип ссылки или протокол не поддерживает эту операцию.",
                L"משמעות (50/1): מערכת הקבצים, שרת SMB, סוג הקישור או הפרוטוקול אינם תומכים בפעולה.");
            add(L"CHECK: Compare file vs directory symlinks, confirm server reparse support and the destination type; SMB junctions are unsupported.",
                L"ПРОВЕРИТЬ: различие File/Directory Symlink, поддержку reparse на сервере и тип расположения; Junction на SMB не поддерживается.",
                L"בדיקה: להשוות קישורי קובץ ותיקייה, לבדוק תמיכת reparse בשרת ואת סוג היעד; Junction אינו נתמך ב-SMB.");
            break;
        case ERROR_BAD_NETPATH:
        case ERROR_BAD_NET_NAME:
        case ERROR_NETWORK_UNREACHABLE:
            add(L"MEANING (53/67/network): Windows cannot reach or resolve the SMB destination or share.",
                L"ЗНАЧЕНИЕ (53/67/сеть): Windows не может найти сервер или SMB-шару либо подключиться к ним.",
                L"משמעות (53/67/רשת): Windows לא מצליחה להגיע לשרת או לשיתוף SMB.");
            add(L"CHECK: UNC server/share spelling, DNS, network/VPN, SMB authentication, mapped drive and availability of the destination directory.",
                L"ПРОВЕРИТЬ: UNC-имя сервера и шары, DNS, сеть/VPN, SMB-аутентификацию, сетевой диск и доступность каталога.",
                L"בדיקה: כתובת UNC, DNS, רשת/VPN, הזדהות SMB, כונן רשת ונגישות התיקייה.");
            break;
        case ERROR_PATH_NOT_FOUND:
        case ERROR_FILE_NOT_FOUND:
        case ERROR_DIRECTORY:
            add(L"MEANING (2/3/267): The source, destination parent or expected object type is missing or unsuitable.",
                L"ЗНАЧЕНИЕ (2/3/267): нет источника/родительской папки либо выбран неверный тип объекта.",
                L"משמעות (2/3/267): מקור או תיקיית אב חסרים, או שסוג הפריט אינו מתאים.");
            add(L"CHECK: Source still exists, destination parent is a real folder, requested File/Directory mode matches; virtual WIM/ISO/ZIP views are not ordinary folders.",
                L"ПРОВЕРИТЬ: источник существует, целевая папка реальная, режим File/Directory соответствует объекту; каталог внутри WIM/ISO/ZIP не обычная папка.",
                L"בדיקה: המקור קיים, תיקיית היעד אמיתית, מצב קובץ/תיקייה תואם, ונתיב בתוך WIM/ISO/ZIP אינו תיקייה רגילה.");
            break;
        case ERROR_NOT_SAME_DEVICE:
            add(L"MEANING (17): Hardlinks require a single filesystem volume; they do not work across drives or SMB shares.",
                L"ЗНАЧЕНИЕ (17): Hardlink требует одного файлового тома и не работает между дисками или SMB-шарами.",
                L"משמעות (17): Hardlink דורש אותו נפח ואינו עובד בין כוננים או שיתופי SMB.");
            add(L"CHECK: Put source and destination on the same NTFS volume, or select a suitable symbolic-link type.",
                L"ПРОВЕРИТЬ: разместить источник и ссылку на одном NTFS-томе либо выбрать другой тип ссылки.",
                L"בדיקה: להשתמש באותו אמצעי אחסון NTFS או לבחור סוג קישור אחר.");
            break;
        case ERROR_INVALID_REPARSE_DATA:
            add(L"IMPORTANT (4392): The create call may have succeeded but the resulting object did NOT verify as the expected symlink. An entry may already exist!",
                L"ВАЖНО (4392): команда создания могла выполниться, но результат НЕ подтверждён как нужный symlink. Объект по пути ссылки уже может существовать!",
                L"חשוב (4392): ייתכן שהיצירה הצליחה אך האובייקט לא אומת כקישור הרצוי. ייתכן שכבר קיים פריט בנתיב!");
            add(L"CHECK: Inspect the link object/reparse tag and target without deleting or overwriting it. Do NOT retry blindly; on Samba inspect server-side metadata if needed.",
                L"ПРОВЕРИТЬ: объект, reparse tag и путь назначения без удаления или перезаписи. НЕ повторять вслепую; на Samba при необходимости проверить метаданные сервера.",
                L"בדיקה: לבדוק את האובייקט, תג reparse והיעד ללא מחיקה או דריסה. לא לנסות שוב בעיוורון.");
            break;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_NAME:
        case ERROR_FILENAME_EXCED_RANGE:
            add(L"MEANING (87/123/206): Unsupported API flags, malformed or overly long path, or invalid link name.",
                L"ЗНАЧЕНИЕ (87/123/206): неподдерживаемые флаги API, некорректный/слишком длинный путь либо недопустимое имя.",
                L"משמעות (87/123/206): דגלי API לא נתמכים, נתיב שגוי/ארוך מדי או שם לא חוקי.");
            add(L"CHECK: Full UNC/NTFS path, filename characters and path length; ensure the target is a real file or directory.",
                L"ПРОВЕРИТЬ: полный UNC/NTFS-путь, символы в имени, длину пути и реальность целевого объекта.",
                L"בדיקה: נתיב UNC/NTFS מלא, תווים בשם ואורך הנתיב; לוודא שהמקור אמיתי.");
            break;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            add(L"MEANING (32/33): Another process or filesystem filter is blocking the object.",
                L"ЗНАЧЕНИЕ (32/33): объект удерживает другая программа или файловый фильтр.",
                L"משמעות (32/33): תהליך אחר או מסנן מערכת קבצים חוסם את הפריט.");
            add(L"CHECK: Open file handles, Total Commander/plugin activity, antivirus or backup scans; retry only after the lock is released.",
                L"ПРОВЕРИТЬ: открытые дескрипторы, работу TC/плагинов, антивирус и резервное копирование; повторять после снятия блокировки.",
                L"בדיקה: קבצים פתוחים, פעילות TC/תוספים, אנטי-וירוס וגיבוי; לנסות שוב רק לאחר שחרור הנעילה.");
            break;
        default:
            add(L"MEANING: Windows returned an error for which no single confirmed cause is known. The numerical Win32 code above is retained for troubleshooting.",
                L"ЗНАЧЕНИЕ: Windows вернула ошибку без однозначно установленной причины. Числовой код Win32 указан выше для диагностики.",
                L"משמעות: Windows החזירה שגיאה ללא סיבה ודאית. קוד Win32 מוצג למעלה לצורך אבחון.");
            add(L"CHECK: Confirm source, destination, selected link type, permissions and server logs as appropriate.",
                L"ПРОВЕРИТЬ: путь источника, папку назначения, тип ссылки, права доступа и журналы сервера при необходимости.",
                L"בדיקה: מקור, יעד, סוג קישור, הרשאות ויומני השרת לפי הצורך.");
            break;
    }
    if (f.smbDestination && f.smbSource) {
        add(L"SMB NOTE: If source and destination are in different shares, client R2R symlink traversal policy can affect opening the link AFTER creation; it is not proof of a creation-permission failure.",
            L"ПРИМЕЧАНИЕ SMB: если источник и ссылка в разных шарах, политика Windows R2R может мешать переходу ПОСЛЕ создания. Это не доказательство причины ошибки создания.",
            L"הערת SMB: אם המקור והקישור בשיתופים שונים, מדיניות R2R עשויה להשפיע על פתיחה לאחר יצירה; זו אינה הוכחה לסיבת שגיאת יצירה.");
    }
    add(L"SAFETY: The plugin does not change Samba settings, ACLs, Windows symlink policy or existing destination objects.",
        L"БЕЗОПАСНОСТЬ: плагин не меняет настройки Samba, ACL, политику Windows Symlink или существующие объекты.",
        L"בטיחות: התוסף אינו משנה Samba, הרשאות, מדיניות קישורים של Windows או פריטים קיימים.");
    return result;
}
} // namespace tcs::help
