# ScrollFix

Background tool for Windows that filters false mouse-wheel ticks from a worn or broken encoder.

When you scroll one way, a damaged wheel sometimes sends a short pulse the other way. ScrollFix watches the sequence of wheel events and drops those glitches. Normal scrolling and real direction changes still work.

## Requirements

- Windows 10 / 11
- No install, no Python, no drivers

## Run

1. Open `bin\ScrollFix.exe`
2. The app sits in the system tray (near the clock)
3. Right-click the tray icon for settings, language, diagnostics, exit

Optional: in settings, turn on **Start with Windows**.

## Build from source

Needs MinGW g++ (WinLibs) with `windres`.

```bat
build.bat
```

Output: `bin\ScrollFix.exe`

## Notes

- Keep **Auto** sensitivity unless your mouse still glitches — then try **Strict**
- Diagnostics shows live wheel events and what was filtered
- Version shows in the tray right-click menu

## Russian

See [README.ru.md](README.ru.md)
