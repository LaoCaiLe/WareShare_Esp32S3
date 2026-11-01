' Start the PC hardware monitor with no console window.
' Double-click this file (wscript runs it silently); pythonw.exe has no
' terminal, so nothing appears in the taskbar. Output is discarded.
Set sh = CreateObject("WScript.Shell")
sh.CurrentDirectory = "F:\ESP-IDF\project\wave_share_esp32_s3\lvgl_template_project_PIO\pc_companion"
pyw = sh.ExpandEnvironmentStrings("%LOCALAPPDATA%\Programs\Python\Python312\pythonw.exe")
sh.Run """" & pyw & """ -u monitor.py", 0, False
