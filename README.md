# nav-xp

A tiny native Win32 client for [Navidrome](https://www.navidrome.org/) / Subsonic servers that runs on
**Windows XP SP2 (32-bit)** on old SSE-only CPUs (Pentium III and up) and still works on Windows 11.
One C file, no runtime dependencies, about 130 KB executable (fits on a floppy). Created with Claude Opus 5.5.

Written for a retro PC that has to play music from a home server over plain HTTP on a local network.

## Features

- Library browser (artists / albums / tracks), playlists, favorites (starred), search
- Playback starts while the file is still downloading; seeking inside the downloaded part
- Built-in MP3 and FLAC decoders ([dr_libs](https://github.com/mackron/dr_libs)), SSE1-only MP3 path for Pentium III; Opus optional (`USE_OPUS`, needs opusfile)
- Stream quality selectable in the app: original, or server-side MP3 / Opus transcoding with bitrate choice
- Playback queue next to the library: current track highlighted, play next / add to queue, reorder, remove, clear, shuffle, repeat (off / list / track)
- 10-band equalizer with presets
- Cover art (GDI+), tag info panel, like button (Navidrome favorites)
- Multiple servers managed in the app (add / edit / delete / test connection)
- Global multimedia keys (Play/Pause, Next, Prev, Stop), tray icon
- Russian and English UI, switchable from the menu
- Remembers window position, volume, mode, equalizer, last track
- CPU profiler (`Ctrl+D`), tunable buffer and priority for slow machines

Tested on: Pentium III 1000 + XP SP2, Celeron M 360 + XP SP2 (about 5-8 % CPU, no dropouts while using other programs), Windows 11.

## Quick start

1. Build (see below) or take `nav-xp.exe` from Releases.
2. Run it, **File - Servers** (F2): add the server as `http://host:4533`, login, password, press *Test*, then *Connect*.
3. Expand *Library*, double-click a track.

Settings live next to the exe: `nav-xp.sav` (state and servers, **the password is stored in plain text**)
and an optional `nav-xp.ini` (File - Advanced: `Bitrate`, `Priority`, `BufferMs`).

## Hotkeys

| Key | Action |
|---|---|
| Space | Play / pause |
| Ctrl+Right / Ctrl+Left | Next / previous track |
| Shift+Right / Shift+Left | Seek +10 s / -10 s |
| Ctrl+Up / Ctrl+Down | Volume |
| Ctrl+F | Search |
| Ctrl+L | Like / unlike |
| Ctrl+H / Ctrl+R | Shuffle / repeat |
| Ctrl+E | Equalizer |
| Ctrl+D | CPU load display |
| Ins | Add selected to queue |
| F2 / F5 | Servers / refresh library |

## Building

The source is `src/nav-xp.c`; keep `dr_mp3.h` and `dr_flac.h` next to it.
**The file must stay UTF-8 with BOM and CRLF line endings** (Visual Studio 2010 otherwise breaks the Cyrillic strings).

**Visual Studio 2010 Express** (used by the author): new empty Win32 project, add `src/nav-xp.c`,
configuration *Release / Win32*, *Enable Enhanced Instruction Set* = `SSE (/arch:SSE)`,
*Runtime Library* = `Multi-threaded (/MT)`. Libraries and the XP manifest are linked through `#pragma`s.

**MinGW** (not tested by the author, see the header comment in the source for the command line).

## Known limitations

- Plain HTTP only (no HTTPS), token + salt authentication
- Passwords are stored unencrypted in the `.sav` file
- Opus needs the whole file downloaded before playback; ID3v1 tail is not skipped for MP3
- All Navidrome libraries of the server are shown together
- Code comments are mostly in Russian

## License

MIT, see [LICENSE](LICENSE). Bundled `dr_mp3.h` / `dr_flac.h`: public domain / MIT-0, see [src/THIRD_PARTY.md](src/THIRD_PARTY.md).

[Русская версия](README.ru.md)
