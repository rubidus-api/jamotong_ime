================================================================
 Jamotong IME - Install & Verification Guide
================================================================

Jamotong is a Korean (Hangul) IME for Windows 11, implemented in
pure C23 + WinAPI as a TSF (Text Services Framework) text service.

Homepage: https://github.com/rubidus-api/jamotong_ime


1. INSTALL
----------------------------------------------------------------
 1) Extract the zip to a folder you will KEEP (the DLLs are
    loaded from this folder; do not delete it after install).
 2) Right-click install.bat -> "Run as administrator".
 3) Sign out and back in (or reboot).
 4) Press Win+Space and select "Jamotong IME".

 The installer registers:
   - jamotong.dll   (64-bit TSF text service)
   - jamotong32.dll (32-bit, for 32-bit applications)
   + TSF profile (branding icon) and categories
     (keyboard / display attribute / immersive / systray).


2. BASIC USE
----------------------------------------------------------------
 - Switch IME:          Win+Space -> "Jamotong IME"
 - Cycle layouts:       Right Alt, Hangul key, or Shift+Space
                        (configurable in Settings)
 - Settings window:     Ctrl+Alt+K  (or run jamotong.exe)
 - Hanja conversion:    compose a syllable, press the Hanja key;
                        or select text first, then press Hanja
 - Special characters:  single consonant (e.g. Mieum) + Hanja key
 - Unicode input:       Ctrl+Alt+U -> type hex -> Enter
 - Tray monitor:        run jamotong.exe (left-click = settings,
                        right-click = menu)

 Data files (hanja.txt, hanja_hunum.txt) must stay next to the
 DLLs. User settings: %APPDATA%\Jamotong\config.ini


3. VERIFY CHECKLIST
----------------------------------------------------------------
 [ ] Win+Space list shows "Jamotong IME" with its icon.
 [ ] Typing "gksrmf" in Notepad produces Hangul syllables.
 [ ] Composition preview chip appears at the caret.
 [ ] Hanja key on a composed syllable opens the candidate list
     (with meaning/reading shown).
 [ ] Layout cycling works (Right Alt).
 [ ] Legacy apps (old EDIT controls) receive text correctly.


4. UNINSTALL
----------------------------------------------------------------
 1) Switch to another IME first (e.g. Microsoft IME).
 2) Right-click uninstall.bat -> "Run as administrator".
 3) Sign out and back in.
 4) Delete this folder. User settings remain at
    %APPDATA%\Jamotong (delete manually if desired).


5. TROUBLESHOOTING
----------------------------------------------------------------
 - Registration failed: check that files are unblocked
   (Properties -> Unblock) and antivirus did not quarantine
   the DLLs, then run install.bat again.
 - IME not listed after install: sign out/in again, or reboot.
 - No hanja candidates: hanja.txt must be in the same folder
   as jamotong.dll.

 Licenses: see COPYRIGHT.md and UNICODE-LICENSE.txt.
