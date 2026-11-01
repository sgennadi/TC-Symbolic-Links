TC SYMBOLIC LINKS 0.2.16 — SELECTIVE INSTALLER (one ZIP)
======================================================

РУССКИЙ
1. Распакуйте ВЕСЬ архив TC-Symbolic-Links-Installer.zip в отдельную папку.
2. Запустите Install_TC_Symbolic_Links.cmd, желательно ИЗ Total Commander.
   Если запуск производится из TC, установщик определит разрядность самого TC.
   64-битный Total Commander -> только SymlinkInspector.wdx64.
   32-битный Total Commander -> только SymlinkInspector.wdx.
3. Если запущены обе версии TC, либо установщик не видит TC, выберите
   нужную разрядность в диалоге. Никаких догадок по разрядности Windows.
4. При запросе закройте ВСЕ окна Total Commander. Настройки будут
   записаны только после полного закрытия TC, чтобы он не перезаписал их
   обратно при выходе. При закрытии TC инсталлятор НЕ завершается.
5. Укажите wincmd.ini при запросе. Перед изменением создается отдельная
   резервная копия. Личные кнопки, usercmd.ini и другие плагины сохраняются.
6. Перезапустите Total Commander и подтвердите безопасную настройку
   трех кнопок Symlink. Плагин предложит изменить только свои команды.
7. Для последующих обновлений используйте встроенную кнопку
   Check for Updates. Она обновляет только установленную архитектуру.

Внимание: в архиве НЕ имеется корневого pluginst.inf. Это специально,
так как встроенный ZIP-установщик Total Commander распаковывает все файлы
без выбора x86/x64. НЕ запускайте установку старого транспортного архива
TC-Symbolic-Links-win32-win64.zip вручную: он нужен исключительно для
совместимости со встроенным Update Manager 0.2.15.

Командная строка:
  Install_TC_Symbolic_Links.cmd -Architecture x64
  Install_TC_Symbolic_Links.cmd -Architecture x86
  Install_TC_Symbolic_Links.cmd -Architecture x64 -IniPath "C:\Users\Name\AppData\Roaming\Ghisler\wincmd.ini"
  Install_TC_Symbolic_Links.cmd -Architecture x64 -IniPath "..." -Destination "D:\TotalCmd\Plugins\TCSymbolicLinks\x64"

ENGLISH
1. Extract the ENTIRE TC-Symbolic-Links-Installer.zip into one folder.
2. Run Install_TC_Symbolic_Links.cmd, preferably from inside Total Commander.
   The installer detects the invoking TC process, not the Windows OS bitness.
3. Only SymlinkInspector.wdx64 is installed for x64 TC; only
   SymlinkInspector.wdx is installed for x86 TC.
4. Close ALL Total Commander windows when prompted; the installer writes
   wincmd.ini only after TC has exited so TC cannot overwrite it on exit.
5. If TC architecture or wincmd.ini is ambiguous, choose explicitly.
   The original INI is backed up before any registration edits. Existing
   toolbar bars, custom commands, columns and unrelated plugins are kept.
6. Restart TC and accept the native plugin's safe three-button setup.
7. Later updates only touch the already installed architecture, with rollback.

The legacy win32-win64 ZIP is retained exclusively for updater compatibility.
Do not manually install the legacy ZIP in Total Commander.

No permanent companion executable is required: the installer is a .cmd/.ps1
script using Windows PowerShell 5.1; the plugin remains a native WDX DLL.
