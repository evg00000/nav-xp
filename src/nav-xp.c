/*
 * nav-xp.c v4 - клиент Navidrome (Subsonic API) для Windows XP SP2 ... Windows 11
 *
 * Что внутри:
 *   - WinINet (http), CryptoAPI (MD5-токен), waveOut (звук), TreeView (список)
 *   - декодеры mp3/flac встроены: dr_mp3.h и dr_flac.h (https://github.com/mackron/dr_libs)
 *   - opus (необязательно): opusfile, включается через USE_OPUS
 *   - обложки: GDI+ (gdiplus.dll загружается динамически; нет - просто нет обложек)
 *   - быстрый старт: mp3/flac (и перекодированный сервером mp3) играют, пока трек ещё качается
 *   - поиск (search3), плейлисты, теги, горячие клавиши, запоминание состояния
 *
 * ВАЖНО: файл должен быть сохранён как UTF-8 С BOM (VS2010 иначе портит русские строки).
 * dr_mp3.h и dr_flac.h положите рядом с этим файлом.
 *
 * VS2010: Release/Win32, /arch:SSE, Runtime Library = /MT. Манифест для стилей XP
 *         подключается pragma-ой ниже.
 * MinGW:  i686-w64-mingw32-gcc -std=gnu99 -O2 -march=pentium3 -mno-sse2 -mfpmath=387 \
 *           -D_WIN32_WINNT=0x0501 nav-xp.c -o nav-xp.exe -mwindows -static -static-libgcc \
 *           -Wl,--major-os-version=5 -Wl,--minor-os-version=1 \
 *           -Wl,--major-subsystem-version=5 -Wl,--minor-subsystem-version=1 \
 *           -lcomctl32 -lwininet -lwinmm -ladvapi32 -lshell32 -luser32 -lgdi32 -lole32
 *         (для стилей XP в MinGW нужен .rc с манифестом, без него будет классический вид)
 */
#define UNICODE
#define _UNICODE
#define WINVER 0x0501
#define _WIN32_WINNT 0x0501
#define _WIN32_IE 0x0600
#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include <wininet.h>
#include <wincrypt.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>

#ifdef _MSC_VER
#pragma comment(lib,"comctl32.lib")
#pragma comment(lib,"wininet.lib")
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(linker,"/SUBSYSTEM:WINDOWS,5.01")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

/* --- декодеры: только скалярный код, без SSE2 --- */
/* mp3 на P3: у dr_mp3 есть ветка на SSE-интринсиках, но она по умолчанию включается только при SSE2.
   Берём из неё только SSE1: вывод в float (DR_MP3_FLOAT_OUTPUT - ветка синтеза в int16, единственная
   с SSE2-командами, при этом не компилируется), а макрос __SSE2__ нужен лишь чтобы заголовок включил
   эту ветку без проверки cpuid. Свой код перевода float -> int16 ниже использует только SSE1.
   Отключить: добавить NDC_NO_SSE1_MP3 в определения препроцессора. */
#if defined(_MSC_VER) && !defined(NDC_NO_SSE1_MP3)
#define NDC_MP3_SSE1 1
#define DR_MP3_FLOAT_OUTPUT
#define __SSE2__ 1
#else
#define DR_MP3_NO_SIMD
#endif
#define DR_MP3_ONLY_MP3
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#ifdef NDC_MP3_SSE1
#undef __SSE2__
#endif
#define DR_FLAC_NO_SIMD
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
/* разные версии dr_libs имеют разные сигнатуры init/open */
#if defined(DRMP3_VERSION_MINOR) && DRMP3_VERSION_MINOR >= 7
#define MP3_INIT(p, rd, sk, ud) drmp3_init((p), (rd), (sk), NULL, NULL, (ud), NULL)
#else
#define MP3_INIT(p, rd, sk, ud) drmp3_init((p), (rd), (sk), (ud), NULL)
#endif
#if defined(DRFLAC_VERSION_MINOR) && DRFLAC_VERSION_MINOR >= 13
#define FLAC_OPEN(rd, sk, ud) drflac_open((rd), (sk), NULL, (ud), NULL)
#else
#define FLAC_OPEN(rd, sk, ud) drflac_open((rd), (sk), (ud), NULL)
#endif
#ifdef USE_OPUS
#include <opusfile.h>
#endif

/* ----------------------------------------------------------- идентификаторы */
#define ID_TREE   100
#define ID_PREV   101
#define ID_PLAY   102
#define ID_STOP   103
#define ID_NEXT   104
#define ID_SEEK   105
#define ID_VOL    106
#define ID_TIME   107
#define ID_STAT   109
#define ID_SEARCH 110
#define ID_FIND   111
#define ID_TITLE  112
#define ID_INFO   113
#define ID_COVER  114
#define ID_SHUF   115
#define ID_REP    116
#define ID_LIKE   117
#define ID_QLIST  118
#define ID_QHEAD  119
#define ID_QCLR   120
#define ID_QUP    121
#define ID_QDN    122
#define ID_VOLLBL 123
#define IDM_RELOAD   201
#define IDM_SETTINGS 202
#define IDM_EXIT     203
#define IDM_FINDFOCUS 204
#define IDM_VOLUP    208
#define IDM_VOLDN    209
#define IDM_SEEKF    210
#define IDM_SEEKB    211
#define IDM_EQ       212
#define IDM_SHUF     213
#define IDM_REP      214
#define IDM_PROF     215
#define IDM_LIKE     216
#define IDM_TRAY_SHOW 217
#define IDM_ENQ      218
#define IDM_PLAYNEXT 219
#define IDM_PLAYSEL  220
#define IDM_SERVERS  221
#define IDM_GMEDIA   222
#define IDM_QDEL     223
#define IDM_QPLAY    224
#ifndef LANG_RUSSIAN
#define LANG_RUSSIAN 0x19
#endif
#define IDM_LANG_RU  230
#define IDM_LANG_EN  231
#define IDM_Q_BASE   300
#define SV_LIST  500
#define SV_NAME  501
#define SV_URL   502
#define SV_USER  503
#define SV_PASS  504
#define SV_SHOW  505
#define SV_ADD   506
#define SV_DEL   507
#define SV_TEST  508
#define SV_CONN  509
#define SV_CLOSE 510
#define SV_MSG   511
#define WM_APP_PING (WM_APP + 10)
#ifndef APPCOMMAND_MEDIA_PLAY
#define APPCOMMAND_MEDIA_PLAY 46
#endif
#ifndef APPCOMMAND_MEDIA_PAUSE
#define APPCOMMAND_MEDIA_PAUSE 47
#endif
#define WM_APP_OPENED (WM_APP + 1)
#define WM_APP_LOAD  (WM_APP + 2)
#define WM_APP_END   (WM_APP + 3)
#define WM_APP_COVER (WM_APP + 4)
#define WM_APP_STAR  (WM_APP + 5)
#define WM_APP_TRAY  (WM_APP + 6)
#define IDT_PLAY 1
#define NBMAX 48
#define COVER_PX 160

enum { N_ROOT = 0, N_ARTIST, N_ALBUM, N_SONG, N_LIBRARY, N_PLAYLISTS, N_PLAYLIST, N_SEARCH, N_SGROUP, N_STARRED };
enum { ST_STOPPED = 0, ST_LOADING, ST_PLAYING, ST_PAUSED };
enum { DEC_NONE = 0, DEC_MP3, DEC_FLAC, DEC_OPUS };

/* ------------------------------------------------------------------- типы */
typedef struct {
    wchar_t title[160], artist[128], album[128];
    int year, track, disc, bitrate, duration, starred;
    unsigned long long size;
    char cover[80];
    char suffix[16];
} SongInfo;

typedef struct {
    int  type;
    int  loaded;
    char id[96];
    SongInfo *si;
} Node;

typedef struct { char k[24]; char v[256]; } Attr;

/* сервер из списка в настройках */
#define MAXSRV 16
typedef struct { wchar_t name[64]; char url[256]; char user[128]; char pass[128]; } Server;

/* элемент очереди воспроизведения: независимая копия песни (дерево может перестраиваться) */
typedef struct { char id[96]; SongInfo si; HTREEITEM h; int seq; } QItem;

/* Декодер (mp3 / flac / opus). Читает данные через Src_Read/Src_Seek (см. ниже). */
typedef struct {
    int kind, opened;
    unsigned int rate, ch;
    drmp3 mp3;
    drflac *flac;
    float *ftmp;                 /* буфер float для mp3 (путь SSE1) */
    int fcap;
#ifdef USE_OPUS
    OggOpusFile *opus;
#endif
} Dec;

/* Один проигрываемый трек. Файл на диске растёт, пока трек качается; декодер читает его
   через свои callback-и и ждёт, если нужных байт ещё нет - поэтому играть можно сразу,
   а перемотка работает в уже скачанной части (дальше - ждёт докачки). */
typedef struct {
    volatile LONG refs;          /* UI + поток загрузки + поток воспроизведения */
    LONG gen;
    int kind, raw, durSec;
    volatile LONG size;          /* сколько байт уже записано в файл */
    volatile LONG dstate;        /* 0 - качается, 1 - готово, 2 - ошибка или отмена */
    volatile LONG cancel;
    volatile LONG pause;
    volatile LONG seekMs;        /* запрос перемотки, -1 - нет */
    volatile LONG posMs;         /* текущая позиция */
    volatile LONG lenMs;
    volatile LONG buffering;     /* 1, если декодер ждёт данные */
    LONG rpos;                   /* позиция чтения декодера (трогает только поток воспроизведения) */
    LONG fpos;                   /* где сейчас указатель файла hr (-1 - неизвестно) */
    HANDLE hw, hr;               /* запись (поток загрузки) и чтение (декодер) */
    PVOID volatile hUrl;
    wchar_t path[MAX_PATH];
    char url[1400];
    Dec dec;
} Track;

typedef struct { char url[1400]; LONG gen; } CoverJob;

/* ---------------------------------------------------------------- глобальное */
static HWND g_hwnd, g_tree, g_bPrev, g_bPlay, g_bStop, g_bNext, g_seek, g_vol, g_time;
static HWND g_search, g_bFind, g_title, g_info, g_cover, g_status;
static HFONT g_font, g_fontB;
static HINTERNET g_inet;
static char g_url[256], g_user[128], g_pass[128], g_salt[24], g_tok[40];
static int  g_bitrate = 192;            /* битрейт перекодирования из ini (когда выбран "Оригинал") */

static int  g_prio = 1;                  /* приоритет процесса: 0 - обычный, 1 - выше обычного, 2 - высокий */
static int  g_bufMs = 500;               /* глубина очереди звука, мс */

/* качество потока: 0 - оригинал, 1 - MP3, 2 - Opus (если собрано с USE_OPUS) */
typedef struct { int fmt, br; const wchar_t *name; } QOpt;
static const QOpt g_qopts[] = {
    { 0,   0, L"Оригинал (без перекодирования)" },
    { 1,  64, L"MP3  64 kbps" },
    { 1,  96, L"MP3  96 kbps" },
    { 1, 128, L"MP3 128 kbps" },
    { 1, 160, L"MP3 160 kbps" },
    { 1, 192, L"MP3 192 kbps" },
    { 1, 256, L"MP3 256 kbps" },
    { 1, 320, L"MP3 320 kbps" },
#ifdef USE_OPUS
    { 2,  64, L"Opus  64 kbps" },
    { 2,  96, L"Opus  96 kbps" },
    { 2, 128, L"Opus 128 kbps" },
    { 2, 192, L"Opus 192 kbps" },
#endif
};
#define QN ((int)(sizeof g_qopts / sizeof g_qopts[0]))
static int g_qFmt = 0, g_qBr = 192;

/* режимы воспроизведения */
static int g_shuffle = 0, g_repeat = 0;   /* repeat: 0 - выкл, 1 - список, 2 - трек */
static HWND g_bShuf, g_bRep;
static HMENU g_mCtl, g_mQ;

/* эквалайзер: усиление полос в половинках дБ (-24..24) */
static volatile LONG g_eqOn = 0, g_eqVer = 1, g_eqPre = 0, g_eqGain[10];
static HWND g_eqWnd;

/* встроенный профайлер (Ctrl+D): микросекунды, накопленные потоком воспроизведения */
static volatile LONG g_prof = 0, g_pfDec = 0, g_pfEq = 0, g_pfOut = 0, g_pfWait = 0;
static long long g_qpcFreq = 0;
static int g_lastSec = -1, g_lastLen = -1;
static volatile LONG g_underruns = 0;    /* сколько раз очередь звука опустела при воспроизведении */

/* значок в трее и кнопка "лайк" */
static NOTIFYICONDATAW g_nid;
static int g_inTray = 0;
static UINT g_msgTaskbar = 0;
static HICON g_icBig, g_icSmall;
static HWND g_bLike, g_volLbl;

static Server g_srv[MAXSRV];
static int g_nsrv = 0, g_srvActive = 0;
static int g_globalMedia = 1;
static HWND g_svWnd, g_svList, g_svName, g_svUrl, g_svUser, g_svPass, g_svShow, g_svMsg;
static int g_svSel = -1;

/* очередь воспроизведения */
static QItem *g_q;
static int g_qn, g_qcap, g_qpos = -1, g_qseq;
static HTREEITEM g_qOrigin;              /* альбом, из которого собрана очередь */
static QItem g_np;                       /* то, что играет сейчас */
static int g_npOk;
static HWND g_qlist, g_qhead, g_bQClr, g_bQUp, g_bQDn;
static WNDPROC g_oldQList;
static HFONT g_fontQB;
static int g_qW = 340, g_qItemH = 18, g_splitX, g_splitTop, g_splitDrag;
static void QRefreshUi(void);
static void SrvOpen(void);
static void RegMediaKeys(int on);
static void SyncLikeUi(void);
static void TrayUpdateTip(void);
static wchar_t g_ini[MAX_PATH], g_sav[MAX_PATH];
static HTREEITEM g_cur, g_searchItem;
static int g_state = ST_STOPPED, g_drag = 0, g_len = 0, g_firstLoad = 1, g_wasBuf = 0;
static volatile LONG g_gen = 0, g_coverGen = 0;
static Track *g_trk = NULL;
static WNDPROC g_oldEdit;

static HANDLE g_pthread = NULL;
static volatile LONG g_gain = 256;

static char g_coverWant[80], g_coverShown[80];

/* сохранённое состояние */
static int g_stX, g_stY, g_stW, g_stH, g_stMax, g_stVol = 100;
static char g_last[500];

/* GDI+ (flat API, загружается динамически) */
typedef struct { UINT32 GdiplusVersion; void *DebugEventCallback; BOOL SuppressBackgroundThread; BOOL SuppressExternalCodecs; } GpStartupInput;
typedef int  (WINAPI *PFN_GpStartup)(ULONG_PTR *, const GpStartupInput *, void *);
typedef void (WINAPI *PFN_GpShutdown)(ULONG_PTR);
typedef int  (WINAPI *PFN_GpBmpFromStream)(IStream *, void **);
typedef int  (WINAPI *PFN_GpHBitmap)(void *, HBITMAP *, DWORD);
typedef int  (WINAPI *PFN_GpDispose)(void *);
static HMODULE g_gdipDll;
static ULONG_PTR g_gdipTok;
static PFN_GpShutdown g_GpShutdown;
static PFN_GpBmpFromStream g_GpBmpFromStream;
static PFN_GpHBitmap g_GpHBitmap;
static PFN_GpDispose g_GpDispose;

/* ------------------------------------------------------------------ utils */

typedef struct { const wchar_t *ru, *en; } TrEnt;
static const TrEnt g_tr[] = {
    { L"Оригинал (без перекодирования)", L"Original (no transcoding)" },
    { L"Основной", L"Main" },
    { L"Загрузка...", L"Loading..." },
    { L"Нет связи с сервером (проверьте Url в ini-файле)", L"No connection to the server (check Url in the ini file)" },
    { L"Ошибка сервера", L"Server error" },
    { L"Готово", L"Done" },
    { L"Пусто", L"Empty" },
    { L"Добавьте сервер (меню Файл - Серверы)", L"Add a server (File - Servers)" },
    { L"Библиотека", L"Library" },
    { L"Избранное", L"Favorites" },
    { L"Плейлисты", L"Playlists" },
    { L"Сервер: %s. Раскройте «Библиотека», «Избранное» или «Плейлисты»", L"Server: %s. Expand \"Library\", \"Favorites\" or \"Playlists\"" },
    { L"Введите запрос", L"Enter a search query" },
    { L"Поиск: %s", L"Search: %s" },
    { L"Исполнители", L"Artists" },
    { L"Альбомы", L"Albums" },
    { L"Треки", L"Tracks" },
    { L"Ничего не найдено", L"Nothing found" },
    { L"Поиск завершён", L"Search finished" },
    { L"Остановлено", L"Stopped" },
    { L"Исполнитель: %s\r\n", L"Artist: %s\r\n" },
    { L"Альбом: %s\r\n", L"Album: %s\r\n" },
    { L"Год: %d\r\n", L"Year: %d\r\n" },
    { L"Трек %d (диск %d)\r\n", L"Track %d (disc %d)\r\n" },
    { L"Трек %d\r\n", L"Track %d\r\n" },
    { L"В очереди: %d из %d\r\n", L"In queue: %d of %d\r\n" },
    { L"Формат: %s, %d kbps", L"Format: %s, %d kbps" },
    { L"Формат: %s", L"Format: %s" },
    { L"Формат: %s -> %s %d kbps (с сервера)", L"Format: %s -> %s %d kbps (from server)" },
    { L"(без названия)", L"(untitled)" },
    { L"Не удалось создать временный файл (проверьте папку TEMP)", L"Could not create a temporary file (check the TEMP folder)" },
    { L"Загрузка трека...", L"Loading track..." },
    { L"Загрузка трека (перекодирование на сервере)...", L"Loading track (transcoding on the server)..." },
    { L"Не удалось запустить загрузку", L"Could not start the download" },
    { L"Не удалось запустить воспроизведение", L"Could not start playback" },
    { L"Здесь нет треков для воспроизведения", L"No tracks to play here" },
    { L"Не хватает памяти", L"Out of memory" },
    { L"Выберите трек, альбом или плейлист", L"Select a track, album or playlist" },
    { L"Здесь нет треков", L"No tracks here" },
    { L"Будет следующим: %d", L"Up next: %d" },
    { L"Добавлено в очередь: %d", L"Added to queue: %d" },
    { L"Воспроизведение", L"Playing" },
    { L"Пауза", L"Paused" },
    { L"Громкость: %d", L"Volume: %d" },
    { L"Нагрузка: вся программа %s | декодер %s | эквалайзер %s | вывод %s | ожидание данных %s | обрывов звука: %d", L"Load: whole program %s | decoder %s | equalizer %s | output %s | waiting for data %s | audio underruns: %d" },
    { L"Буферизация (ожидание данных с сервера)...", L"Buffering (waiting for data from the server)..." },
    { L"Повтор: выкл", L"Repeat: off" },
    { L"Повтор: список", L"Repeat: list" },
    { L"Повтор: трек", L"Repeat: track" },
    { L"Из избранного", L"Unlike" },
    { L"В избранное", L"Like" },
    { L"Сначала включите трек", L"Play a track first" },
    { L"Добавляю в избранное...", L"Adding to favorites..." },
    { L"Убираю из избранного...", L"Removing from favorites..." },
    { L"Показать окно", L"Show window" },
    { L"Пуск / пауза", L"Play / pause" },
    { L"Предыдущий трек", L"Previous track" },
    { L"Следующий трек", L"Next track" },
    { L"Убрать из избранного", L"Remove from favorites" },
    { L"Выход", L"Exit" },
    { L"Случайный порядок: очередь перемешана", L"Shuffle: queue shuffled" },
    { L"Случайный порядок: выключен, очередь в исходном порядке", L"Shuffle: off, queue in original order" },
    { L"Показ нагрузки включён: данные появятся через пару секунд", L"CPU load display on: data will appear in a couple of seconds" },
    { L"Показ нагрузки выключен", L"CPU load display off" },
    { L"Повтор: выключен", L"Repeat: off" },
    { L"Повтор: весь список", L"Repeat: whole list" },
    { L"Повтор: один трек", L"Repeat: single track" },
    { L"Качество: %s (применится со следующего трека)", L"Quality: %s (applies from the next track)" },
    { L"Плоский", L"Flat" },
    { L"Усиленные басы", L"Bass boost" },
    { L"Яркие верха", L"Bright treble" },
    { L"Вокал", L"Vocal" },
    { L"Рок", L"Rock" },
    { L"Поп", L"Pop" },
    { L"Электроника", L"Electronic" },
    { L"Классика", L"Classical" },
    { L"Тихо / ночью", L"Quiet / night" },
    { L"Общ.", L"Gen." },
    { L"Включить эквалайзер", L"Enable equalizer" },
    { L"Сброс", L"Reset" },
    { L"Пользовательский", L"Custom" },
    { L"Шкала: от -12 до +12 дБ, шаг 0,5 дБ. Частоты в Гц. Изменения слышны сразу.", L"Scale: -12 to +12 dB, 0.5 dB step. Frequencies in Hz. Changes are heard immediately." },
    { L"Эквалайзер", L"Equalizer" },
    { L"Сервер %d", L"Server %d" },
    { L"%s  (активный)", L"%s  (active)" },
    { L"Достигнуто максимальное число серверов", L"Maximum number of servers reached" },
    { L"Сервер удалён", L"Server removed" },
    { L"Сначала добавьте сервер", L"Add a server first" },
    { L"Введите адрес сервера, например http://192.168.1.10:4533", L"Enter the server address, e.g. http://192.168.1.10:4533" },
    { L"Не удалось посчитать подпись (CryptoAPI)", L"Could not compute the signature (CryptoAPI)" },
    { L"Не удалось запустить проверку", L"Could not start the check" },
    { L"Проверка соединения...", L"Checking connection..." },
    { L"Добавить", L"Add" },
    { L"Удалить", L"Delete" },
    { L"Название:", L"Name:" },
    { L"Адрес:", L"Address:" },
    { L"Логин:", L"Login:" },
    { L"Пароль:", L"Password:" },
    { L"Показать пароль", L"Show password" },
    { L"Проверить", L"Test" },
    { L"Подключиться", L"Connect" },
    { L"Адрес вида http://192.168.1.10:4533 (https не поддерживается). Пароль хранится в файле .sav рядом с программой.", L"Address like http://192.168.1.10:4533 (https is not supported). The password is stored in the .sav file next to the program." },
    { L"Закрыть", L"Close" },
    { L"Соединение установлено, логин и пароль приняты.", L"Connection established, login and password accepted." },
    { L"Сервер отвечает, но логин или пароль неверные.", L"The server answers, but the login or password is wrong." },
    { L"Сервер ответил, но это не похоже на Navidrome / Subsonic. Проверьте адрес и порт.", L"The server answered, but it does not look like Navidrome / Subsonic. Check the address and port." },
    { L"Нет ответа. Проверьте адрес, порт и что сервер запущен и доступен по сети.", L"No response. Check the address, the port, and that the server is running and reachable." },
    { L"Серверы", L"Servers" },
    { L"Очередь: %d из %d", L"Queue: %d of %d" },
    { L"Очередь: %d", L"Queue: %d" },
    { L"Играющий трек из очереди не убирается: выберите другой", L"The playing track cannot be removed from the queue: select another one" },
    { L"Очередь очищена", L"Queue cleared" },
    { L"Воспроизвести", L"Play" },
    { L"Убрать из очереди\tDel", L"Remove from queue\tDel" },
    { L"Очистить очередь", L"Clear queue" },
    { L"Случайно", L"Shuffle" },
    { L"Найти", L"Find" },
    { L"Громкость: 0", L"Volume: 0" },
    { L"Очередь: 0", L"Queue: 0" },
    { L"Очистить", L"Clear" },
    { L"Вверх", L"Up" },
    { L"Вниз", L"Down" },
    { L"Поиск (Ctrl+F)", L"Search (Ctrl+F)" },
    { L"Добавлено в избранное", L"Added to favorites" },
    { L"Убрано из избранного", L"Removed from favorites" },
    { L"Не удалось изменить избранное (ошибка сервера)", L"Could not change favorites (server error)" },
    { L"Обрыв загрузки трека", L"Download interrupted" },
    { L"Не удалось открыть трек (повреждён, неподдерживаемый формат или ошибка сервера)", L"Could not open the track (corrupt, unsupported format or server error)" },
    { L"Ошибка вывода звука", L"Audio output error" },
    { L"Мультимедиа-клавиши работают во всех окнах", L"Media keys work in all windows" },
    { L"Мультимедиа-клавиши работают только когда плеер активен", L"Media keys work only when the player is active" },
    { L"Дополнительные настройки: после правки сохраните файл и нажмите F5", L"Advanced settings: save the file after editing and press F5" },
    { L"Играть следующим", L"Play next" },
    { L"Добавить в очередь\tIns", L"Add to queue\tIns" },
    { L"Серверы...\tF2", L"Servers...\tF2" },
    { L"Обновить библиотеку\tF5", L"Refresh library\tF5" },
    { L"Дополнительно (ini-файл)", L"Advanced (ini file)" },
    { L"Файл", L"File" },
    { L"Пуск / пауза\tПробел", L"Play / pause\tSpace" },
    { L"Стоп", L"Stop" },
    { L"Следующий трек\tCtrl+Вправо", L"Next track\tCtrl+Right" },
    { L"Предыдущий трек\tCtrl+Влево", L"Previous track\tCtrl+Left" },
    { L"Перемотка +10 с\tShift+Вправо", L"Seek +10 s\tShift+Right" },
    { L"Перемотка -10 с\tShift+Влево", L"Seek -10 s\tShift+Left" },
    { L"Громче\tCtrl+Вверх", L"Volume up\tCtrl+Up" },
    { L"Тише\tCtrl+Вниз", L"Volume down\tCtrl+Down" },
    { L"В избранное / убрать из избранного\tCtrl+L", L"Like / unlike\tCtrl+L" },
    { L"Случайный порядок\tCtrl+H", L"Shuffle\tCtrl+H" },
    { L"Повтор (выкл / список / трек)\tCtrl+R", L"Repeat (off / list / track)\tCtrl+R" },
    { L"Показывать нагрузку на CPU\tCtrl+D", L"Show CPU load\tCtrl+D" },
    { L"Поиск\tCtrl+F", L"Search\tCtrl+F" },
    { L"Играть выбранное следующим", L"Play selected next" },
    { L"Добавить выбранное в очередь\tIns", L"Add selected to queue\tIns" },
    { L"Мультимедиа-клавиши во всех окнах", L"Media keys in all windows" },
    { L"Управление", L"Controls" },
    { L"Эквалайзер...\tCtrl+E", L"Equalizer...\tCtrl+E" },
    { L"Качество потока (со следующего трека):", L"Stream quality (from the next track):" },
    { L"Звук", L"Sound" },
};
static int g_lang;                         /* 0 - русский, 1 - English */

static const wchar_t *T(const wchar_t *ru)
{
    int i;
    if (!g_lang) return ru;
    for (i = 0; i < (int)(sizeof g_tr / sizeof g_tr[0]); i++)
        if (g_tr[i].ru == ru || wcscmp(g_tr[i].ru, ru) == 0) return g_tr[i].en;
    return ru;
}

static void SetStatus(const wchar_t *s) { SetWindowTextW(g_status, s); }

static void ToUtf8(const wchar_t *w, char *out, int n)
{
    if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, n, NULL, NULL)) out[0] = 0;
}

static void FromUtf8(const char *s, wchar_t *w, int n)
{
    wchar_t t[1024];
    if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, t, 1024)) t[0] = 0;
    lstrcpynW(w, t, n);
}

static void UrlEnc(const char *s, char *out, size_t n)
{
    size_t j = 0;
    for (; *s && j + 4 < n; s++) {
        unsigned char c = (unsigned char)*s;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out[j++] = (char)c;
        else j += sprintf(out + j, "%%%02X", c);
    }
    out[j] = 0;
}

static int Md5Hex(const char *data, char *hex)
{
    HCRYPTPROV p; HCRYPTHASH h; BYTE d[16]; DWORD l = 16; int ok = 0, i;
    if (!CryptAcquireContext(&p, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return 0;
    if (CryptCreateHash(p, CALG_MD5, 0, 0, &h)) {
        if (CryptHashData(h, (const BYTE *)data, (DWORD)strlen(data), 0) &&
            CryptGetHashParam(h, HP_HASHVAL, d, &l, 0)) {
            for (i = 0; i < 16; i++) sprintf(hex + i * 2, "%02x", d[i]);
            ok = 1;
        }
        CryptDestroyHash(h);
    }
    CryptReleaseContext(p, 0);
    return ok;
}

static void FmtTime(wchar_t *o, int ms)
{
    int s = ms / 1000;
    wsprintfW(o, L"%d:%02d", s / 60, s % 60);
}

/* ------------------------------------------------- чтение текстовых файлов */

static int IsUtf8(const BYTE *s, DWORD n)
{
    DWORD i = 0, j;
    while (i < n) {
        BYTE c = s[i];
        int k;
        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) k = 1;
        else if ((c & 0xF0) == 0xE0) k = 2;
        else if ((c & 0xF8) == 0xF0) k = 3;
        else return 0;
        if (i + k >= n) return 0;
        for (j = 1; j <= (DWORD)k; j++) if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k + 1;
    }
    return 1;
}

/* UTF-16 LE (BOM), UTF-8 (с BOM или без) или ANSI. */
static wchar_t *ReadTextFile(const wchar_t *path)
{
    HANDLE f;
    DWORD sz, rd = 0;
    BYTE *b;
    wchar_t *w = NULL;
    int n;

    f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    sz = GetFileSize(f, NULL);
    if (sz == INVALID_FILE_SIZE || sz > (1u << 20)) { CloseHandle(f); return NULL; }
    b = (BYTE *)malloc(sz + 4);
    if (!b) { CloseHandle(f); return NULL; }
    ReadFile(f, b, sz, &rd, NULL);
    CloseHandle(f);
    memset(b + rd, 0, 4);

    if (rd >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        n = (int)((rd - 2) / 2);
        w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
        if (w) { memcpy(w, b + 2, n * sizeof(wchar_t)); w[n] = 0; }
    } else {
        UINT cp = CP_ACP;
        DWORD skip = 0;
        if (rd >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { skip = 3; cp = CP_UTF8; }
        else if (IsUtf8(b, rd)) cp = CP_UTF8;
        n = MultiByteToWideChar(cp, 0, (const char *)b + skip, (int)(rd - skip), NULL, 0);
        w = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
        if (w) {
            MultiByteToWideChar(cp, 0, (const char *)b + skip, (int)(rd - skip), w, n);
            w[n] = 0;
        }
    }
    free(b);
    return w;
}

static void IniGet(const wchar_t *text, const wchar_t *key, wchar_t *out, int n)
{
    const wchar_t *p = text;
    size_t kl = wcslen(key);
    out[0] = 0;
    while (*p) {
        const wchar_t *ls = p, *le = p, *q;
        while (*le && *le != L'\n') le++;
        q = ls;
        while (q < le && (*q == L' ' || *q == L'\t' || *q == 0xFEFF)) q++;
        if (q < le && *q != L';' && *q != L'#' && *q != L'[' && _wcsnicmp(q, key, kl) == 0) {
            const wchar_t *r = q + kl, *e;
            int len;
            while (r < le && (*r == L' ' || *r == L'\t')) r++;
            if (r < le && *r == L'=') {
                r++;
                while (r < le && (*r == L' ' || *r == L'\t')) r++;
                e = le;
                while (e > r && (e[-1] == L'\r' || e[-1] == L' ' || e[-1] == L'\t')) e--;
                len = (int)(e - r);
                if (len > n - 1) len = n - 1;
                memcpy(out, r, len * sizeof(wchar_t));
                out[len] = 0;
                return;
            }
        }
        p = *le ? le + 1 : le;
    }
}

static void SaveState(HWND hwnd);

/* Приводит адрес к виду http://host:port (без хвостового слэша, схема по умолчанию http). */
static void NormalizeUrl(char *u)
{
    char tmp[300];
    size_t l;
    char *b = u;
    while (*b == ' ' || *b == '\t') b++;
    if (b != u) memmove(u, b, strlen(b) + 1);
    l = strlen(u);
    while (l && (u[l - 1] == '/' || u[l - 1] == ' ' || u[l - 1] == '\t')) u[--l] = 0;
    if (!u[0]) return;
    if (!strstr(u, "://")) {
        _snprintf(tmp, sizeof tmp, "http://%s", u);
        tmp[sizeof tmp - 1] = 0;
        lstrcpynA(u, tmp, 256);
    }
}

/* Читает необязательный ini (тонкие настройки) и берёт активный сервер из списка. */
static int LoadConfig(void)
{
    wchar_t *text, w[256];
    char in[300];
    Server *sv;

    g_bitrate = 192; g_prio = 1; g_bufMs = 500;
    text = ReadTextFile(g_ini);
    if (text) {
        IniGet(text, L"Bitrate", w, 256);
        if (w[0]) g_bitrate = _wtoi(w);
        if (g_bitrate < 32 || g_bitrate > 320) g_bitrate = 192;
        IniGet(text, L"Priority", w, 256);
        if (w[0]) g_prio = _wtoi(w);
        IniGet(text, L"BufferMs", w, 256);
        if (w[0]) g_bufMs = _wtoi(w);
        if (g_nsrv == 0) {                      /* перенос сервера из старого ini */
            IniGet(text, L"Url", w, 256);
            if (w[0]) {
                Server *s0 = &g_srv[0];
                memset(s0, 0, sizeof *s0);
                ToUtf8(w, s0->url, sizeof s0->url);
                NormalizeUrl(s0->url);
                IniGet(text, L"User", w, 256);     ToUtf8(w, s0->user, sizeof s0->user);
                IniGet(text, L"Password", w, 256); ToUtf8(w, s0->pass, sizeof s0->pass);
                lstrcpyW(s0->name, T(L"Основной"));
                g_nsrv = 1; g_srvActive = 0;
                SaveState(g_hwnd);
            }
        }
        free(text);
    }
    if (g_prio < 0 || g_prio > 2) g_prio = 1;
    SetPriorityClass(GetCurrentProcess(), g_prio == 2 ? HIGH_PRIORITY_CLASS :
                     (g_prio == 1 ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS));
    if (g_bufMs < 250) g_bufMs = 250;
    if (g_bufMs > 2500) g_bufMs = 2500;

    g_url[0] = g_user[0] = g_pass[0] = 0;
    if (g_nsrv <= 0) return 0;
    if (g_srvActive < 0 || g_srvActive >= g_nsrv) g_srvActive = 0;
    sv = &g_srv[g_srvActive];
    lstrcpynA(g_url, sv->url, sizeof g_url);
    lstrcpynA(g_user, sv->user, sizeof g_user);
    lstrcpynA(g_pass, sv->pass, sizeof g_pass);
    NormalizeUrl(g_url);
    if (!g_url[0]) return 0;

    _snprintf(g_salt, sizeof g_salt, "%08x%04x", (unsigned)GetTickCount(), (unsigned)(rand() & 0xFFFF));
    g_salt[sizeof g_salt - 1] = 0;
    _snprintf(in, sizeof in, "%s%s", g_pass, g_salt);
    in[sizeof in - 1] = 0;
    return Md5Hex(in, g_tok);
}

static void WriteDefaultIni(void)
{
    static const char txt[] =
        "\xEF\xBB\xBF"
        "; nav-xp: servers are managed in the program (File - Servers). Advanced options only:\r\n"
        "[settings]\r\n"
        "; bitrate for server-side mp3 transcoding (formats without built-in decoder)\r\n"
        "Bitrate=192\r\n"
        "; process priority: 0 normal, 1 above normal (default), 2 high\r\n"
        "Priority=1\r\n"
        "; audio queue depth in ms (250..2500). Larger = fewer dropouts, volume reacts slower\r\n"
        "BufferMs=500\r\n";
    HANDLE f = CreateFileW(g_ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wr;
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, txt, (DWORD)(sizeof txt - 1), &wr, NULL);
        CloseHandle(f);
    }
}

/* ------------------------------------------------------ сохранённое состояние */

static void LoadState(void)
{
    wchar_t *t, w[600];
    t = ReadTextFile(g_sav);
    if (!t) return;
    IniGet(t, L"Volume", w, 600); if (w[0]) g_stVol = _wtoi(w);
    IniGet(t, L"Left", w, 600);   g_stX = _wtoi(w);
    IniGet(t, L"Top", w, 600);    g_stY = _wtoi(w);
    IniGet(t, L"Width", w, 600);  g_stW = _wtoi(w);
    IniGet(t, L"Height", w, 600); g_stH = _wtoi(w);
    IniGet(t, L"Max", w, 600);    g_stMax = _wtoi(w);
    IniGet(t, L"Servers", w, 600);  g_nsrv = _wtoi(w);
    if (g_nsrv < 0) g_nsrv = 0;
    if (g_nsrv > MAXSRV) g_nsrv = MAXSRV;
    IniGet(t, L"ServerActive", w, 600); g_srvActive = _wtoi(w);
    {
        int k;
        wchar_t key[24];
        for (k = 0; k < g_nsrv; k++) {
            wsprintfW(key, L"Srv%dName", k + 1); IniGet(t, key, w, 600); lstrcpynW(g_srv[k].name, w, 64);
            wsprintfW(key, L"Srv%dUrl", k + 1);  IniGet(t, key, w, 600); ToUtf8(w, g_srv[k].url, sizeof g_srv[k].url);
            wsprintfW(key, L"Srv%dUser", k + 1); IniGet(t, key, w, 600); ToUtf8(w, g_srv[k].user, sizeof g_srv[k].user);
            wsprintfW(key, L"Srv%dPass", k + 1); IniGet(t, key, w, 600); ToUtf8(w, g_srv[k].pass, sizeof g_srv[k].pass);
        }
    }
    IniGet(t, L"Lang", w, 600);     if (w[0]) g_lang = (_wtoi(w) != 0);
    IniGet(t, L"GlobalMedia", w, 600); g_globalMedia = w[0] ? (_wtoi(w) != 0) : 1;
    IniGet(t, L"QueueW", w, 600);   if (w[0]) g_qW = _wtoi(w);
    if (g_qW < 280) g_qW = 280;
    if (g_qW > 900) g_qW = 900;
    IniGet(t, L"QFmt", w, 600);   if (w[0]) g_qFmt = _wtoi(w);
    IniGet(t, L"QBr", w, 600);    if (w[0]) g_qBr = _wtoi(w);
    IniGet(t, L"Shuffle", w, 600); g_shuffle = _wtoi(w) ? 1 : 0;
    IniGet(t, L"Repeat", w, 600); g_repeat = _wtoi(w);
    IniGet(t, L"EqOn", w, 600);   g_eqOn = _wtoi(w) ? 1 : 0;
    IniGet(t, L"EqPre", w, 600);  g_eqPre = _wtoi(w);
    IniGet(t, L"EqBands", w, 600);
    {
        wchar_t *p = w;
        int k;
        for (k = 0; k < 10 && *p; k++) {
            g_eqGain[k] = wcstol(p, &p, 10);
            if (*p == L',') p++;
        }
    }
    IniGet(t, L"Last", w, 600);   ToUtf8(w, g_last, sizeof g_last);
    free(t);
    {
        int k, ok = 0;
        for (k = 0; k < QN; k++) if (g_qopts[k].fmt == g_qFmt && (g_qFmt == 0 || g_qopts[k].br == g_qBr)) ok = 1;
        if (!ok) { g_qFmt = 0; g_qBr = 192; }
        if (g_repeat < 0 || g_repeat > 2) g_repeat = 0;
        if (g_eqPre < -24) g_eqPre = -24;
        if (g_eqPre > 24) g_eqPre = 24;
        for (k = 0; k < 10; k++) {
            if (g_eqGain[k] < -24) g_eqGain[k] = -24;
            if (g_eqGain[k] > 24) g_eqGain[k] = 24;
        }
    }
    if (g_stVol < 0) g_stVol = 0;
    if (g_stVol > 100) g_stVol = 100;
}

static void SvAp(char **p, char *end, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (*p >= end - 1) return;
    va_start(ap, fmt);
    n = _vsnprintf(*p, (size_t)(end - *p), fmt, ap);
    va_end(ap);
    if (n < 0) { *p = end - 1; **p = 0; } else *p += n;
}

static void SaveState(HWND hwnd)
{
    enum { CAP = 40000 };
    char *buf, *p, *end, nm[200];
    WINDOWPLACEMENT wp;
    HANDLE f;
    DWORD wr;
    int vol = (int)SendMessage(g_vol, TBM_GETPOS, 0, 0), k;

    memset(&wp, 0, sizeof wp);
    wp.length = sizeof wp;
    if (!GetWindowPlacement(hwnd, &wp)) return;
    buf = (char *)malloc(CAP);
    if (!buf) return;
    p = buf; end = buf + CAP;
    SvAp(&p, end, "\xEF\xBB\xBF");
    SvAp(&p, end, "Volume=%d\r\nLeft=%d\r\nTop=%d\r\nWidth=%d\r\nHeight=%d\r\nMax=%d\r\n",
         vol, (int)wp.rcNormalPosition.left, (int)wp.rcNormalPosition.top,
         (int)(wp.rcNormalPosition.right - wp.rcNormalPosition.left),
         (int)(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top),
         (wp.showCmd == SW_SHOWMAXIMIZED || (wp.flags & WPF_RESTORETOMAXIMIZED)) ? 1 : 0);
    SvAp(&p, end, "QFmt=%d\r\nQBr=%d\r\nShuffle=%d\r\nRepeat=%d\r\nEqOn=%d\r\nEqPre=%d\r\n",
         g_qFmt, g_qBr, g_shuffle, g_repeat, (int)g_eqOn, (int)g_eqPre);
    SvAp(&p, end, "EqBands=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
         (int)g_eqGain[0], (int)g_eqGain[1], (int)g_eqGain[2], (int)g_eqGain[3], (int)g_eqGain[4],
         (int)g_eqGain[5], (int)g_eqGain[6], (int)g_eqGain[7], (int)g_eqGain[8], (int)g_eqGain[9]);
    SvAp(&p, end, "GlobalMedia=%d\r\nQueueW=%d\r\nServers=%d\r\nServerActive=%d\r\nLang=%d\r\n",
         g_globalMedia, g_qW, g_nsrv, g_srvActive, g_lang);
    for (k = 0; k < g_nsrv; k++) {
        ToUtf8(g_srv[k].name, nm, sizeof nm);
        SvAp(&p, end, "Srv%dName=%s\r\nSrv%dUrl=%s\r\nSrv%dUser=%s\r\nSrv%dPass=%s\r\n",
             k + 1, nm, k + 1, g_srv[k].url, k + 1, g_srv[k].user, k + 1, g_srv[k].pass);
    }
    SvAp(&p, end, "Last=%s\r\n", g_last);
    f = CreateFileW(g_sav, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, buf, (DWORD)(p - buf), &wr, NULL);
        CloseHandle(f);
    }
    free(buf);
}

/* ------------------------------------------------------------------ http */

static void BuildUrl(char *out, size_t n, const char *method, const char *extra)
{
    char ue[400];
    UrlEnc(g_user, ue, sizeof ue);
    _snprintf(out, n, "%s/rest/%s.view?u=%s&t=%s&s=%s&v=1.13.0&c=nav-xp%s%s",
              g_url, method, ue, g_tok, g_salt, extra[0] ? "&" : "", extra);
    out[n - 1] = 0;
}

static char *HttpGet(const char *url, DWORD *outLen)
{
    HINTERNET h;
    size_t cap = 65536, len = 0;
    char *buf, *nb;
    DWORD got;

    h = InternetOpenUrlA(g_inet, url, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
    if (!h) return NULL;
    buf = (char *)malloc(cap + 1);
    if (!buf) { InternetCloseHandle(h); return NULL; }
    for (;;) {
        if (len + 16384 > cap) {
            cap *= 2;
            nb = (char *)realloc(buf, cap + 1);
            if (!nb) { free(buf); InternetCloseHandle(h); return NULL; }
            buf = nb;
        }
        if (!InternetReadFile(h, buf + len, 16384, &got) || !got) break;
        len += got;
    }
    buf[len] = 0;
    InternetCloseHandle(h);
    if (outLen) *outLen = (DWORD)len;
    return buf;
}

/* ------------------------------------------------------------ mini XML */

static void XmlDecode(const char *s, size_t n, char *out, size_t cap)
{
    size_t i = 0, j = 0;
    while (i < n && j + 5 < cap) {
        if (s[i] == '&') {
            const char *semi = (const char *)memchr(s + i, ';', n - i);
            if (semi && semi - (s + i) <= 10) {
                size_t el = (size_t)(semi - (s + i)) - 1;
                const char *e = s + i + 1;
                unsigned long cp = 0;
                int ok = 1;
                if (el == 3 && !memcmp(e, "amp", 3)) cp = '&';
                else if (el == 2 && !memcmp(e, "lt", 2)) cp = '<';
                else if (el == 2 && !memcmp(e, "gt", 2)) cp = '>';
                else if (el == 4 && !memcmp(e, "quot", 4)) cp = '"';
                else if (el == 4 && !memcmp(e, "apos", 4)) cp = '\'';
                else if (e[0] == '#') {
                    cp = (e[1] == 'x' || e[1] == 'X') ? strtoul(e + 2, NULL, 16) : strtoul(e + 1, NULL, 10);
                } else ok = 0;
                if (ok) {
                    if (cp < 0x80) out[j++] = (char)cp;
                    else if (cp < 0x800) { out[j++] = (char)(0xC0 | (cp >> 6)); out[j++] = (char)(0x80 | (cp & 0x3F)); }
                    else if (cp < 0x10000) { out[j++] = (char)(0xE0 | (cp >> 12)); out[j++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[j++] = (char)(0x80 | (cp & 0x3F)); }
                    else { out[j++] = (char)(0xF0 | (cp >> 18)); out[j++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[j++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[j++] = (char)(0x80 | (cp & 0x3F)); }
                    i = (size_t)(semi - s) + 1;
                    continue;
                }
            }
        }
        out[j++] = s[i++];
    }
    out[j] = 0;
}

/* Ищет следующий тег <name ...>, разбирает атрибуты. Возвращает число атрибутов или -1. */
static int NextTag(const char **pp, const char *name, Attr *a, int maxa)
{
    const char *p = *pp;
    size_t nl = strlen(name);
    int na = 0;

    for (;;) {
        p = strchr(p, '<');
        if (!p) { *pp = *pp + strlen(*pp); return -1; }
        if (strncmp(p + 1, name, nl) == 0 &&
            (p[1 + nl] == ' ' || p[1 + nl] == '/' || p[1 + nl] == '>')) break;
        p++;
    }
    p += 1 + nl;
    for (;;) {
        const char *ks, *vs;
        size_t kl, vl;
        char q;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == '>') { p++; break; }
        if (*p == '/' && p[1] == '>') { p += 2; break; }
        ks = p;
        while (*p && *p != '=' && *p != ' ' && *p != '>' && *p != '\n') p++;
        kl = (size_t)(p - ks);
        if (*p != '=') { if (p == ks) p++; continue; }
        p++;
        q = *p;
        if (q != '"' && q != '\'') continue;
        p++;
        vs = p;
        while (*p && *p != q) p++;
        vl = (size_t)(p - vs);
        if (*p) p++;
        if (na < maxa) {
            if (kl > sizeof a[na].k - 1) kl = sizeof a[na].k - 1;
            memcpy(a[na].k, ks, kl); a[na].k[kl] = 0;
            XmlDecode(vs, vl, a[na].v, sizeof a[na].v);
            na++;
        }
    }
    *pp = p;
    return na;
}

static const char *Get(Attr *a, int n, const char *k)
{
    int i;
    for (i = 0; i < n; i++) if (strcmp(a[i].k, k) == 0) return a[i].v;
    return "";
}

/* Запрос к API: возвращает XML (free()) или NULL, ошибку пишет в статус. */
static char *FetchXml(const char *method, const char *extra)
{
    char url[1400];
    char *xml;
    Attr a[8];
    wchar_t w[400];
    HCURSOR old;
    const char *e;
    int na;

    BuildUrl(url, sizeof url, method, extra);
    old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    SetStatus(T(L"Загрузка..."));
    xml = HttpGet(url, NULL);
    SetCursor(old);
    if (!xml) { SetStatus(T(L"Нет связи с сервером (проверьте Url в ini-файле)")); return NULL; }
    if (strstr(xml, "status=\"failed\"")) {
        e = xml;
        na = NextTag(&e, "error", a, 8);
        if (na >= 0) FromUtf8(Get(a, na, "message"), w, 400);
        else lstrcpyW(w, T(L"Ошибка сервера"));
        SetStatus(w);
        free(xml);
        return NULL;
    }
    return xml;
}

/* ------------------------------------------------------------ GDI+ / обложки */

static void InitGdip(void)
{
    PFN_GpStartup st;
    GpStartupInput in;
    g_gdipDll = LoadLibraryA("gdiplus.dll");
    if (!g_gdipDll) return;
    st = (PFN_GpStartup)GetProcAddress(g_gdipDll, "GdiplusStartup");
    g_GpShutdown = (PFN_GpShutdown)GetProcAddress(g_gdipDll, "GdiplusShutdown");
    g_GpBmpFromStream = (PFN_GpBmpFromStream)GetProcAddress(g_gdipDll, "GdipCreateBitmapFromStream");
    g_GpHBitmap = (PFN_GpHBitmap)GetProcAddress(g_gdipDll, "GdipCreateHBITMAPFromBitmap");
    g_GpDispose = (PFN_GpDispose)GetProcAddress(g_gdipDll, "GdipDisposeImage");
    memset(&in, 0, sizeof in);
    in.GdiplusVersion = 1;
    if (!st || !g_GpShutdown || !g_GpBmpFromStream || !g_GpHBitmap || !g_GpDispose ||
        st(&g_gdipTok, &in, NULL) != 0) {
        FreeLibrary(g_gdipDll);
        g_gdipDll = NULL;
    }
}

static void ShutdownGdip(void)
{
    if (g_gdipDll) { g_GpShutdown(g_gdipTok); g_gdipDll = NULL; }
}

static HBITMAP DecodeImage(const void *data, DWORD len)
{
    HGLOBAL hg;
    void *p, *bmp = NULL;
    IStream *st = NULL;
    HBITMAP hb = NULL;
    COLORREF c;
    DWORD bg;

    if (!g_gdipDll) return NULL;
    hg = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hg) return NULL;
    p = GlobalLock(hg);
    if (!p) { GlobalFree(hg); return NULL; }
    memcpy(p, data, len);
    GlobalUnlock(hg);
    if (CreateStreamOnHGlobal(hg, TRUE, &st) != S_OK) { GlobalFree(hg); return NULL; }
    c = GetSysColor(COLOR_BTNFACE);
    bg = 0xFF000000u | ((DWORD)(c & 0xFF) << 16) | ((DWORD)((c >> 8) & 0xFF) << 8) | (DWORD)((c >> 16) & 0xFF);
    if (g_GpBmpFromStream(st, &bmp) == 0 && bmp) {
        if (g_GpHBitmap(bmp, &hb, bg) != 0) hb = NULL;
        g_GpDispose(bmp);
    }
    st->lpVtbl->Release(st);
    return hb;
}

static unsigned __stdcall CoverThread(void *p)
{
    CoverJob *j = (CoverJob *)p;
    DWORD len = 0;
    char *data;
    HBITMAP hb = NULL;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    data = HttpGet(j->url, &len);
    if (data && len > 64) hb = DecodeImage(data, len);
    free(data);
    PostMessage(g_hwnd, WM_APP_COVER, (WPARAM)j->gen, (LPARAM)hb);
    free(j);
    return 0;
}

static void SetCover(HBITMAP hb)
{
    HBITMAP old = (HBITMAP)SendMessage(g_cover, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hb);
    if (old) DeleteObject(old);
    if (hb) lstrcpynA(g_coverShown, g_coverWant, sizeof g_coverShown);
    else g_coverShown[0] = 0;
}

static void RequestCover(const SongInfo *si)
{
    CoverJob *j;
    char idenc[200], extra[300];
    uintptr_t th;

    if (!si->cover[0]) { InterlockedIncrement(&g_coverGen); SetCover(NULL); return; }
    if (strcmp(si->cover, g_coverShown) == 0) return;
    lstrcpynA(g_coverWant, si->cover, sizeof g_coverWant);
    InterlockedIncrement(&g_coverGen);
    j = (CoverJob *)calloc(1, sizeof(CoverJob));
    if (!j) return;
    UrlEnc(si->cover, idenc, sizeof idenc);
    _snprintf(extra, sizeof extra, "id=%s&size=%d", idenc, COVER_PX);
    extra[sizeof extra - 1] = 0;
    BuildUrl(j->url, sizeof j->url, "getCoverArt", extra);
    j->gen = g_coverGen;
    th = _beginthreadex(NULL, 0, CoverThread, j, 0, NULL);
    if (th) CloseHandle((HANDLE)th); else free(j);
}

/* ------------------------------------------------------------- дерево */

static Node *GetNode(HTREEITEM h)
{
    TVITEMW it;
    if (!h) return NULL;
    memset(&it, 0, sizeof it);
    it.mask = TVIF_PARAM;
    it.hItem = h;
    if (!SendMessageW(g_tree, TVM_GETITEMW, 0, (LPARAM)&it)) return NULL;
    return (Node *)it.lParam;
}

static void FreeNode(Node *n)
{
    if (n) { free(n->si); free(n); }
}

static HTREEITEM InsertNodeEx(HTREEITEM parent, HTREEITEM after, Node *n, const wchar_t *text)
{
    TVINSERTSTRUCTW tvi;
    memset(&tvi, 0, sizeof tvi);
    tvi.hParent = parent;
    tvi.hInsertAfter = after;
    tvi.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
    tvi.item.pszText = (LPWSTR)text;
    tvi.item.lParam = (LPARAM)n;
    tvi.item.cChildren = (n->type == N_SONG) ? 0 : 1;
    return (HTREEITEM)SendMessageW(g_tree, TVM_INSERTITEMW, 0, (LPARAM)&tvi);
}

static int IsUnder(HTREEITEM h, HTREEITEM anc)
{
    while (h) {
        if (h == anc) return 1;
        h = TreeView_GetParent(g_tree, h);
    }
    return 0;
}

/* ctx - тип родителя (влияет на подпись трека/альбома) */
static Node *MakeNode(int ctype, int ctx, Attr *a, int na, wchar_t *label, int labelCap)
{
    Node *n;
    const char *id = Get(a, na, "id");
    char tmp[900];
    wchar_t w[900];

    if (!id[0]) return NULL;
    n = (Node *)calloc(1, sizeof(Node));
    if (!n) return NULL;
    n->type = ctype;
    strncpy(n->id, id, sizeof n->id - 1);

    if (ctype == N_ARTIST) {
        const char *ac = Get(a, na, "albumCount");
        if (ac[0]) _snprintf(tmp, sizeof tmp, "%s (%s)", Get(a, na, "name"), ac);
        else _snprintf(tmp, sizeof tmp, "%s", Get(a, na, "name"));
    } else if (ctype == N_ALBUM) {
        const char *yr = Get(a, na, "year");
        const char *ar = Get(a, na, "artist");
        char t2[500];
        if (ctx == N_SGROUP && ar[0]) _snprintf(t2, sizeof t2, "%s - %s", ar, Get(a, na, "name"));
        else _snprintf(t2, sizeof t2, "%s", Get(a, na, "name"));
        t2[sizeof t2 - 1] = 0;
        if (yr[0]) _snprintf(tmp, sizeof tmp, "%s (%s)", t2, yr);
        else _snprintf(tmp, sizeof tmp, "%s", t2);
    } else if (ctype == N_PLAYLIST) {
        const char *sc = Get(a, na, "songCount");
        if (sc[0]) _snprintf(tmp, sizeof tmp, "%s (%s)", Get(a, na, "name"), sc);
        else _snprintf(tmp, sizeof tmp, "%s", Get(a, na, "name"));
    } else {
        SongInfo *si = (SongInfo *)calloc(1, sizeof(SongInfo));
        char t2[500];
        if (!si) { free(n); return NULL; }
        n->si = si;
        FromUtf8(Get(a, na, "title"),  si->title,  160);
        FromUtf8(Get(a, na, "artist"), si->artist, 128);
        FromUtf8(Get(a, na, "album"),  si->album,  128);
        si->year     = atoi(Get(a, na, "year"));
        si->track    = atoi(Get(a, na, "track"));
        si->disc     = atoi(Get(a, na, "discNumber"));
        si->bitrate  = atoi(Get(a, na, "bitRate"));
        si->duration = atoi(Get(a, na, "duration"));
        si->starred  = Get(a, na, "starred")[0] ? 1 : 0;
        si->size     = (unsigned long long)_atoi64(Get(a, na, "size"));
        lstrcpynA(si->cover, Get(a, na, "coverArt"), sizeof si->cover);
        lstrcpynA(si->suffix, Get(a, na, "suffix"), sizeof si->suffix);
        if (ctx == N_ALBUM && si->track > 0)
            _snprintf(t2, sizeof t2, "%02d. %s", si->track, Get(a, na, "title"));
        else if (ctx == N_ALBUM)
            _snprintf(t2, sizeof t2, "%s", Get(a, na, "title"));
        else
            _snprintf(t2, sizeof t2, "%s - %s", Get(a, na, "artist"), Get(a, na, "title"));
        t2[sizeof t2 - 1] = 0;
        _snprintf(tmp, sizeof tmp, "%s  [%d:%02d]", t2, si->duration / 60, si->duration % 60);
    }
    tmp[sizeof tmp - 1] = 0;
    FromUtf8(tmp, w, 900);
    lstrcpynW(label, w, labelCap);
    return n;
}

static int LoadChildren(HTREEITEM parent, Node *pn)
{
    char extra[300], idenc[200];
    const char *method, *tag;
    char *xml;
    const char *p;
    Attr a[64];
    int na, cnt = 0, ctype;
    wchar_t label[400];

    extra[0] = 0;
    switch (pn->type) {
    case N_LIBRARY:   method = "getArtists";   tag = "artist";   ctype = N_ARTIST;   break;
    case N_ARTIST:    method = "getArtist";    tag = "album";    ctype = N_ALBUM;    break;
    case N_ALBUM:     method = "getAlbum";     tag = "song";     ctype = N_SONG;     break;
    case N_PLAYLISTS: method = "getPlaylists"; tag = "playlist"; ctype = N_PLAYLIST; break;
    case N_STARRED:   method = "getStarred2";  tag = "song";     ctype = N_SONG;     break;
    case N_PLAYLIST:  method = "getPlaylist";  tag = "entry";    ctype = N_SONG;     break;
    default: return 0;
    }
    if (pn->type == N_ARTIST || pn->type == N_ALBUM || pn->type == N_PLAYLIST) {
        UrlEnc(pn->id, idenc, sizeof idenc);
        _snprintf(extra, sizeof extra, "id=%s", idenc);
        extra[sizeof extra - 1] = 0;
    }
    xml = FetchXml(method, extra);
    if (!xml) return 0;

    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    p = xml;
    while ((na = NextTag(&p, tag, a, 64)) >= 0) {
        Node *n = MakeNode(ctype, pn->type, a, na, label, 400);
        if (!n) continue;
        InsertNodeEx(parent, TVI_LAST, n, label);
        cnt++;
    }
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, NULL, TRUE);
    free(xml);

    if (cnt == 0 && parent != TVI_ROOT) {
        TVITEMW it;
        memset(&it, 0, sizeof it);
        it.mask = TVIF_CHILDREN; it.hItem = parent; it.cChildren = 0;
        SendMessageW(g_tree, TVM_SETITEMW, 0, (LPARAM)&it);
    }
    SetStatus(cnt ? T(L"Готово") : T(L"Пусто"));
    return 1;
}

static void AddFolder(const wchar_t *title, int type)
{
    Node *n = (Node *)calloc(1, sizeof(Node));
    if (!n) return;
    n->type = type;
    InsertNodeEx(TVI_ROOT, TVI_LAST, n, title);
}

static void DeleteChildren(HTREEITEM h)
{
    HTREEITEM c;
    while ((c = TreeView_GetChild(g_tree, h)) != NULL) TreeView_DeleteItem(g_tree, c);
}

/* Папка "Избранное" устарела (лайк поставлен или снят): обновляем её так, чтобы не оборвать воспроизведение. */
static void RefreshStarred(void)
{
    HTREEITEM f = TreeView_GetRoot(g_tree);
    Node *n = NULL;
    TVITEMW it;
    while (f) {
        n = GetNode(f);
        if (n && n->type == N_STARRED) break;
        f = TreeView_GetNextSibling(g_tree, f);
    }
    if (!f || !n) return;
    if (IsUnder(g_cur, f)) { n->loaded = 2; return; }       /* играет отсюда: обновим при следующем раскрытии */
    if (SendMessageW(g_tree, TVM_GETITEMSTATE, (WPARAM)f, TVIS_EXPANDED) & TVIS_EXPANDED) {
        DeleteChildren(f);
        n->loaded = 1;
        if (!LoadChildren(f, n)) n->loaded = 0;
    } else {
        DeleteChildren(f);
        n->loaded = 0;
        memset(&it, 0, sizeof it);
        it.mask = TVIF_CHILDREN; it.hItem = f; it.cChildren = 1;
        SendMessageW(g_tree, TVM_SETITEMW, 0, (LPARAM)&it);
    }
}

static void RestoreLast(void)
{
    char tmp[500], *tok;
    HTREEITEM cur, s;
    Node *n;
    int wantType;

    if (!g_last[0]) return;
    lstrcpynA(tmp, g_last, sizeof tmp);
    tok = strtok(tmp, "|");
    if (!tok) return;
    wantType = (tok[0] == 'L') ? N_LIBRARY : (tok[0] == 'S' ? N_STARRED : N_PLAYLISTS);
    s = TreeView_GetRoot(g_tree);
    while (s) {
        n = GetNode(s);
        if (n && n->type == wantType) break;
        s = TreeView_GetNextSibling(g_tree, s);
    }
    if (!s) return;
    cur = s;
    while ((tok = strtok(NULL, "|")) != NULL) {
        TreeView_Expand(g_tree, cur, TVE_EXPAND);
        s = TreeView_GetChild(g_tree, cur);
        while (s) {
            n = GetNode(s);
            if (n && strcmp(n->id, tok) == 0) break;
            s = TreeView_GetNextSibling(g_tree, s);
        }
        if (!s) break;
        cur = s;
    }
    TreeView_SelectItem(g_tree, cur);
    TreeView_EnsureVisible(g_tree, cur);
}

static void SaveLastPath(HTREEITEM it)
{
    HTREEITEM chain[8], h = it;
    int n = 0, i;
    Node *root, *x;
    char buf[500];

    while (h && n < 8) { chain[n++] = h; h = TreeView_GetParent(g_tree, h); }
    if (n < 2 || h) return;
    root = GetNode(chain[n - 1]);
    if (!root) return;
    if (root->type == N_LIBRARY) strcpy(buf, "L");
    else if (root->type == N_PLAYLISTS) strcpy(buf, "P");
    else if (root->type == N_STARRED) strcpy(buf, "S");
    else return;
    for (i = n - 2; i >= 0; i--) {
        x = GetNode(chain[i]);
        if (!x || strlen(buf) + strlen(x->id) + 2 >= sizeof buf) return;
        strcat(buf, "|");
        strcat(buf, x->id);
    }
    lstrcpynA(g_last, buf, sizeof g_last);
}

static void ReloadLibrary(void)
{
    if (!LoadConfig()) { SetStatus(T(L"Добавьте сервер (меню Файл - Серверы)")); SrvOpen(); return; }
    g_cur = NULL;
    g_searchItem = NULL;
    TreeView_DeleteAllItems(g_tree);
    AddFolder(T(L"Библиотека"), N_LIBRARY);
    AddFolder(T(L"Избранное"), N_STARRED);
    AddFolder(T(L"Плейлисты"), N_PLAYLISTS);
    {
        wchar_t st[200];
        wsprintfW(st, T(L"Сервер: %s. Раскройте «Библиотека», «Избранное» или «Плейлисты»"), g_srv[g_srvActive].name);
        SetStatus(st);
    }
    if (g_firstLoad) { g_firstLoad = 0; RestoreLast(); }
}

/* ---------------------------------------------------------------- поиск */

static int FillGroup(const char *xml, const char *tag, int ctype, HTREEITEM folder, const wchar_t *title)
{
    const char *p = xml;
    Attr a[64];
    int na, cnt = 0;
    HTREEITEM grp = NULL;
    Node *n, *gn;
    wchar_t label[400], gl[200];

    while ((na = NextTag(&p, tag, a, 64)) >= 0) {
        n = MakeNode(ctype, N_SGROUP, a, na, label, 400);
        if (!n) continue;
        if (!grp) {
            gn = (Node *)calloc(1, sizeof(Node));
            if (!gn) { FreeNode(n); break; }
            gn->type = N_SGROUP;
            gn->loaded = 1;
            grp = InsertNodeEx(folder, TVI_LAST, gn, title);
        }
        InsertNodeEx(grp, TVI_LAST, n, label);
        cnt++;
    }
    if (grp) {
        TVITEMW it;
        wsprintfW(gl, L"%s (%d)", title, cnt);
        memset(&it, 0, sizeof it);
        it.mask = TVIF_TEXT; it.hItem = grp; it.pszText = gl;
        SendMessageW(g_tree, TVM_SETITEMW, 0, (LPARAM)&it);
    }
    return cnt;
}

static void RunSearch(void)
{
    wchar_t wq[256], label[300];
    char q[600], qenc[900], extra[1000];
    char *xml;
    Node *sn;
    int total = 0;
    HTREEITEM c;

    GetWindowTextW(g_search, wq, 256);
    ToUtf8(wq, q, sizeof q);
    if (!q[0]) { SetStatus(T(L"Введите запрос")); return; }
    UrlEnc(q, qenc, sizeof qenc);
    _snprintf(extra, sizeof extra, "query=%s&artistCount=30&albumCount=60&songCount=200", qenc);
    extra[sizeof extra - 1] = 0;
    xml = FetchXml("search3", extra);
    if (!xml) return;

    if (g_searchItem) {
        if (IsUnder(g_cur, g_searchItem)) g_cur = NULL;
        TreeView_DeleteItem(g_tree, g_searchItem);
        g_searchItem = NULL;
    }
    sn = (Node *)calloc(1, sizeof(Node));
    if (!sn) { free(xml); return; }
    sn->type = N_SEARCH;
    sn->loaded = 1;
    wsprintfW(label, T(L"Поиск: %s"), wq);
    g_searchItem = InsertNodeEx(TVI_ROOT, TVI_FIRST, sn, label);

    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    total += FillGroup(xml, "artist", N_ARTIST, g_searchItem, T(L"Исполнители"));
    total += FillGroup(xml, "album",  N_ALBUM,  g_searchItem, T(L"Альбомы"));
    total += FillGroup(xml, "song",   N_SONG,   g_searchItem, T(L"Треки"));
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    free(xml);

    if (!total) {
        TreeView_DeleteItem(g_tree, g_searchItem);
        g_searchItem = NULL;
        SetStatus(T(L"Ничего не найдено"));
        return;
    }
    TreeView_Expand(g_tree, g_searchItem, TVE_EXPAND);
    c = TreeView_GetChild(g_tree, g_searchItem);
    while (c) { TreeView_Expand(g_tree, c, TVE_EXPAND); c = TreeView_GetNextSibling(g_tree, c); }
    TreeView_SelectItem(g_tree, g_searchItem);
    TreeView_EnsureVisible(g_tree, g_searchItem);
    SetStatus(T(L"Поиск завершён"));
    SetFocus(g_tree);
}

static long long NowUs(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (long long)(c.QuadPart / g_qpcFreq) * 1000000 + (long long)(c.QuadPart % g_qpcFreq) * 1000000 / g_qpcFreq;
}

/* ------------------------------------------- источник данных: растущий файл */

/* Читает до n байт. Ждёт, пока докачается нужный кусок; короче n возвращает только в конце файла. */
static size_t Src_Read(Track *t, void *out, size_t n)
{
    LONG st, sz, avail, want;
    DWORD got = 0;
    int waited = 0;
    long long w0 = 0;

    if (n == 0) return 0;
    for (;;) {
        if (t->cancel) return 0;
        st = t->dstate;
        sz = t->size;
        if (t->rpos + (LONG)n <= sz || st != 0) break;
        if (!waited && g_prof) w0 = NowUs();
        waited = 1;
        t->buffering = 1;
        Sleep(8);
    }
    if (waited) {
        t->buffering = 0;
        if (g_prof && w0) InterlockedExchangeAdd(&g_pfWait, (LONG)(NowUs() - w0));
    }
    avail = sz - t->rpos;
    if (avail <= 0) return 0;
    want = ((LONG)n < avail) ? (LONG)n : avail;
    if (t->fpos != t->rpos) SetFilePointer(t->hr, t->rpos, NULL, FILE_BEGIN);   /* без лишнего системного вызова при последовательном чтении */
    if (!ReadFile(t->hr, out, (DWORD)want, &got, NULL)) { t->fpos = -1; return 0; }
    t->rpos += (LONG)got;
    t->fpos = t->rpos;
    return (size_t)got;
}

/* origin: 0 - от начала, 1 - от текущей позиции, 2 - от конца (ждёт полной загрузки). */
static int Src_Seek(Track *t, int off, int origin)
{
    LONG np;
    if (origin == 0) np = off;
    else if (origin == 1) np = t->rpos + off;
    else {
        while (!t->cancel && t->dstate == 0) Sleep(8);
        if (t->cancel) return 0;
        np = t->size + off;
    }
    if (np < 0) return 0;
    t->rpos = np;
    return 1;
}

static size_t Mp3_Read(void *ud, void *out, size_t n) { return Src_Read((Track *)ud, out, n); }
static drmp3_bool32 Mp3_Seek(void *ud, int off, drmp3_seek_origin o) { return Src_Seek((Track *)ud, off, (int)o) ? 1 : 0; }
static size_t Flac_Read(void *ud, void *out, size_t n) { return Src_Read((Track *)ud, out, n); }
static drflac_bool32 Flac_Seek(void *ud, int off, drflac_seek_origin o) { return Src_Seek((Track *)ud, off, (int)o) ? 1 : 0; }

#ifdef USE_OPUS
static int Op_Read(void *ud, unsigned char *p, int n) { return (int)Src_Read((Track *)ud, p, (size_t)n); }
static int Op_Seek(void *ud, opus_int64 off, int whence)
{
    return Src_Seek((Track *)ud, (int)off, whence == SEEK_SET ? 0 : (whence == SEEK_CUR ? 1 : 2)) ? 0 : -1;
}
static opus_int64 Op_Tell(void *ud) { return (opus_int64)((Track *)ud)->rpos; }
#endif

/* ----------------------------------------------------------------- декодеры */

/* Вызывается в потоке воспроизведения: может ждать первые байты трека. */
static int Dec_Open(Dec *d, int kind, Track *t)
{
    d->kind = kind;
    t->rpos = 0;
    if (kind == DEC_MP3) {
        if (!MP3_INIT(&d->mp3, Mp3_Read, Mp3_Seek, t)) return 0;
        d->opened = 1;
        d->rate = d->mp3.sampleRate;
        d->ch = d->mp3.channels;
        return d->rate && d->ch;
    }
    if (kind == DEC_FLAC) {
        d->flac = FLAC_OPEN(Flac_Read, Flac_Seek, t);
        if (!d->flac) return 0;
        d->opened = 1;
        d->rate = d->flac->sampleRate;
        d->ch = d->flac->channels;
        return d->rate && d->ch;
    }
#ifdef USE_OPUS
    if (kind == DEC_OPUS) {
        OpusFileCallbacks cb;
        int err = 0;
        cb.read = Op_Read; cb.seek = Op_Seek; cb.tell = Op_Tell; cb.close = NULL;
        d->opus = op_open_callbacks(t, &cb, NULL, 0, &err);   /* opusfile узнаёт длину - ждёт конца загрузки */
        if (!d->opus) return 0;
        d->opened = 1;
        d->rate = 48000;
        d->ch = 2;
        return 1;
    }
#endif
    return 0;
}

static void Dec_Close(Dec *d)
{
    free(d->ftmp);
    d->ftmp = NULL;
    d->fcap = 0;
    if (!d->opened) return;
    if (d->kind == DEC_MP3) drmp3_uninit(&d->mp3);
    else if (d->kind == DEC_FLAC) { if (d->flac) drflac_close(d->flac); }
#ifdef USE_OPUS
    else if (d->kind == DEC_OPUS) { if (d->opus) op_free(d->opus); }
#endif
    d->opened = 0;
}

/* Общее число кадров, если декодер его знает (иначе 0 - тогда берётся длительность от сервера). */
static unsigned long long Dec_Frames(Dec *d)
{
    if (d->kind == DEC_FLAC && d->flac) return (unsigned long long)d->flac->totalPCMFrameCount;
#ifdef USE_OPUS
    if (d->kind == DEC_OPUS && d->opus) {
        ogg_int64_t t = op_pcm_total(d->opus, -1);
        if (t > 0) return (unsigned long long)t;
    }
#endif
    return 0;
}

#ifdef NDC_MP3_SSE1
/* mp3 -> float (SSE-ветка dr_mp3) -> int16 с округлением */
static int Mp3_ReadS16(Dec *d, short *out, int frames)
{
    int n, i, cnt, v;
    float x;
    if (frames * (int)d->ch > d->fcap) {
        float *nb = (float *)realloc(d->ftmp, (size_t)frames * d->ch * sizeof(float));
        if (!nb) return 0;
        d->ftmp = nb;
        d->fcap = frames * (int)d->ch;
    }
    n = (int)drmp3_read_pcm_frames_f32(&d->mp3, (drmp3_uint64)frames, d->ftmp);
    cnt = n * (int)d->ch;
    for (i = 0; i < cnt; i++) {
        x = d->ftmp[i] * 32768.0f;
        v = (int)(x >= 0.0f ? x + 0.5f : x - 0.5f);
        if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
        out[i] = (short)v;
    }
    return n;
}
#endif

static int Dec_RawRead(Dec *d, short *out, int frames)
{
#ifdef NDC_MP3_SSE1
    if (d->kind == DEC_MP3)  return Mp3_ReadS16(d, out, frames);
#else
    if (d->kind == DEC_MP3)  return (int)drmp3_read_pcm_frames_s16(&d->mp3, (drmp3_uint64)frames, out);
#endif
    if (d->kind == DEC_FLAC) return (int)drflac_read_pcm_frames_s16(d->flac, (drflac_uint64)frames, out);
#ifdef USE_OPUS
    if (d->kind == DEC_OPUS) {
        int r;
        do { r = op_read_stereo(d->opus, (opus_int16 *)out, frames * 2); } while (r == OP_HOLE);
        return r < 0 ? 0 : r;
    }
#endif
    return 0;
}

/* 16 бит; для >2 каналов берёт два передних. */
static int Dec_ReadPcm(Dec *d, short *out, int frames, short *tmp)
{
    int n, i;
    if (d->ch <= 2 || !tmp) return Dec_RawRead(d, out, frames);
    n = Dec_RawRead(d, tmp, frames);
    for (i = 0; i < n; i++) {
        out[2 * i]     = tmp[i * d->ch];
        out[2 * i + 1] = tmp[i * d->ch + 1];
    }
    return n;
}

static void Dec_Seek(Dec *d, unsigned long long frame)
{
    if (d->kind == DEC_MP3) drmp3_seek_to_pcm_frame(&d->mp3, (drmp3_uint64)frame);
    else if (d->kind == DEC_FLAC) drflac_seek_to_pcm_frame(d->flac, (drflac_uint64)frame);
#ifdef USE_OPUS
    else if (d->kind == DEC_OPUS) op_pcm_seek(d->opus, (ogg_int64_t)frame);
#endif
}

/* -------------------------------------------------- трек: файл + загрузка */

static void TrackRelease(Track *t)
{
    if (InterlockedDecrement(&t->refs) == 0) {
        if (t->hr != INVALID_HANDLE_VALUE) CloseHandle(t->hr);
        if (t->hw != INVALID_HANDLE_VALUE) CloseHandle(t->hw);
        DeleteFileW(t->path);
        free(t);
    }
}

/* Отмена: будит все ожидания и рвёт http-соединение. */
static void TrackAbort(Track *t)
{
    HINTERNET h;
    InterlockedExchange(&t->cancel, 1);
    h = (HINTERNET)InterlockedExchangePointer(&t->hUrl, NULL);
    if (h) InternetCloseHandle(h);
}

static unsigned __stdcall DlThread(void *param)
{
    Track *t = (Track *)param;
    HINTERNET h, h2;
    BYTE buf[32768];
    DWORD got, wr, code = 0, clen = sizeof code;
    LONG total = 0;
    int ok = 0;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    h = InternetOpenUrlA(g_inet, t->url, NULL, 0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI, 0);
    if (h) {
        InterlockedExchangePointer(&t->hUrl, h);
        if (HttpQueryInfoA(h, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &code, &clen, NULL) && code != 200) {
            /* сервер ответил ошибкой (401, 404...) - тело не нужно */
        } else {
            while (!t->cancel) {
                if (!InternetReadFile(h, buf, sizeof buf, &got)) break;
                if (!got) { ok = (total > 0); break; }
                if (!WriteFile(t->hw, buf, got, &wr, NULL) || wr != got) break;
                total += (LONG)wr;
                InterlockedExchange(&t->size, total);
            }
        }
        h2 = (HINTERNET)InterlockedExchangePointer(&t->hUrl, NULL);
        if (h2) InternetCloseHandle(h2);
    }
    CloseHandle(t->hw);
    t->hw = INVALID_HANDLE_VALUE;
    InterlockedExchange(&t->dstate, ok ? 1 : 2);
    TrackRelease(t);
    return 0;
}

/* --------------------------------------------------- поток воспроизведения */

/* ------------------------------------------------------ эквалайзер, 10 полос */
typedef struct { double b0, b1, b2, a1, a2, z1[2], z2[2]; int on; } EqBand;
typedef struct { LONG ver; int enabled, nact, act[10]; double pre; EqBand b[10]; } Eq;
static const int g_eqFreq[10] = { 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

static void EqUpdate(Eq *e, unsigned rate)
{
    int i, any;
    e->ver = g_eqVer;
    e->pre = pow(10.0, (double)g_eqPre / 40.0);
    any = (g_eqPre != 0);
    e->nact = 0;
    for (i = 0; i < 10; i++) {
        EqBand *b = &e->b[i];
        LONG gh = g_eqGain[i];
        int on = (gh != 0) && ((double)g_eqFreq[i] < (double)rate * 0.45);
        if (on) {
            double A = pow(10.0, (double)gh / 40.0);
            double w0 = 6.283185307179586 * (double)g_eqFreq[i] / (double)rate;
            double al = sin(w0) / (2.0 * 1.4);
            double cw = cos(w0);
            double a0 = 1.0 + al / A;
            if (!b->on) { b->z1[0] = b->z1[1] = b->z2[0] = b->z2[1] = 0.0; }
            b->b0 = (1.0 + al * A) / a0;
            b->b1 = (-2.0 * cw) / a0;
            b->b2 = (1.0 - al * A) / a0;
            b->a1 = (-2.0 * cw) / a0;
            b->a2 = (1.0 - al / A) / a0;
            e->act[e->nact++] = i;
            any = 1;
        }
        b->on = on;
    }
    e->enabled = (g_eqOn && any) ? 1 : 0;
}

static void EqRun(Eq *e, unsigned rate, short *p, int frames, int ch)
{
    int k, c, q;
    if (e->ver != g_eqVer) EqUpdate(e, rate);
    if (!e->enabled) return;
    for (k = 0; k < frames; k++) {
        for (c = 0; c < ch; c++) {
            double x = (double)p[k * ch + c] * e->pre + 1e-20, y;
            for (q = 0; q < e->nact; q++) {
                EqBand *b = &e->b[e->act[q]];
                y = b->b0 * x + b->z1[c];
                b->z1[c] = b->b1 * x - b->a1 * y + b->z2[c];
                b->z2[c] = b->b2 * x - b->a2 * y;
                x = y;
            }
            if (x > 32767.0) x = 32767.0; else if (x < -32768.0) x = -32768.0;
            p[k * ch + c] = (short)(x >= 0.0 ? x + 0.5 : x - 0.5);
        }
    }
}

static void ApplyGain(short *p, int n)
{
    int g = (int)g_gain, i;
    if (g >= 256) return;
    for (i = 0; i < n; i++) p[i] = (short)((p[i] * g) >> 8);
}

/* Коды для WM_APP_END: 0 - трек доигран, 2 - ошибка вывода звука, 3 - обрыв загрузки, 4 - не удалось открыть. */
static unsigned __stdcall PlayThread(void *arg)
{
    Track *t = (Track *)arg;
    Dec *d = &t->dec;
    HWAVEOUT hwo = NULL;
    WAVEFORMATEX wf;
    HANDLE ev = NULL;
    WAVEHDR hdr[NBMAX];
    short *bufs[NBMAX], *tmp = NULL;
    int busy[NBMAX];
    int nbuf, started = 0, starved = 0;
    int outCh = 2, framesPer = 256, i, n;
    unsigned blk = 4;
    int eof = 0, paused = 0, ended = 0, err = 0, opened = 0;
    unsigned long long base = 0, total;
    LONG gen = t->gen;
    long long t0 = 0, t1;
    Eq eq;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);   /* поток лёгкий, но не должен ждать очереди */
    nbuf = (g_bufMs + 31) / 62;
    if (nbuf < 4) nbuf = 4;
    if (nbuf > NBMAX) nbuf = NBMAX;
    memset(&eq, 0, sizeof eq);
    memset(hdr, 0, sizeof hdr);
    for (i = 0; i < NBMAX; i++) { bufs[i] = NULL; busy[i] = 0; }

    if (!Dec_Open(d, t->kind, t)) {
        err = (t->dstate == 2 && t->size == 0) ? 3 : 4;
        goto done;
    }
    total = Dec_Frames(d);
    if (!total && t->durSec > 0) total = (unsigned long long)t->durSec * d->rate;
    InterlockedExchange(&t->lenMs, total ? (LONG)(total * 1000 / d->rate) : (LONG)t->durSec * 1000);

    outCh = (d->ch == 1) ? 1 : 2;
    blk = (unsigned)outCh * 2;
    framesPer = (int)(d->rate / 16);
    if (framesPer < 256) framesPer = 256;
    for (i = 0; i < nbuf; i++) { bufs[i] = (short *)malloc(framesPer * blk); if (!bufs[i]) err = 2; }
    if (d->ch > 2) { tmp = (short *)malloc(framesPer * d->ch * 2); if (!tmp) err = 2; }
    ev = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!ev) err = 2;
    if (err) goto done;

    memset(&wf, 0, sizeof wf);
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = (WORD)outCh;
    wf.nSamplesPerSec = d->rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)blk;
    wf.nAvgBytesPerSec = d->rate * blk;
    if (waveOutOpen(&hwo, WAVE_MAPPER, &wf, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        hwo = NULL; err = 2; goto done;
    }
    for (i = 0; i < nbuf; i++) {
        hdr[i].lpData = (LPSTR)bufs[i];
        hdr[i].dwBufferLength = framesPer * blk;
        waveOutPrepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
    }
    opened = 1;
    PostMessage(g_hwnd, WM_APP_OPENED, (WPARAM)gen, 0);

    while (!t->cancel) {
        LONG sk = InterlockedExchange(&t->seekMs, -1);

        if (sk >= 0) {
            waveOutReset(hwo);
            for (i = 0; i < nbuf; i++) busy[i] = 0;
            started = 0; starved = 0;
            base = (unsigned long long)sk * d->rate / 1000;
            Dec_Seek(d, base);          /* если нужное место ещё не скачано - ждёт докачки */
            eof = 0;
            if (paused) waveOutPause(hwo);
            if (t->cancel) break;
        }
        if (t->pause && !paused) { waveOutPause(hwo); paused = 1; }
        else if (!t->pause && paused) { waveOutRestart(hwo); paused = 0; }

        if (started && !eof && !paused && !t->cancel) {
            /* очередь звука опустела раньше, чем мы успели её пополнить - это слышно как заикание */
            int playing = 0;
            for (i = 0; i < nbuf; i++) if (busy[i] && !(hdr[i].dwFlags & WHDR_DONE)) playing++;
            if (playing == 0) { if (!starved) { starved = 1; InterlockedIncrement(&g_underruns); } }
            else starved = 0;
        }
        for (i = 0; i < nbuf && !eof; i++) {
            if (busy[i] && !(hdr[i].dwFlags & WHDR_DONE)) continue;
            if (g_prof) t0 = NowUs();
            n = Dec_ReadPcm(d, bufs[i], framesPer, tmp);
            if (g_prof) InterlockedExchangeAdd(&g_pfDec, (LONG)(NowUs() - t0));
            if (n <= 0) {
                if (t->cancel) break;
                if (t->dstate == 2) err = 3; else eof = 1;
                break;
            }
            if (g_prof) t0 = NowUs();
            EqRun(&eq, d->rate, bufs[i], n, outCh);
            if (g_prof) { t1 = NowUs(); InterlockedExchangeAdd(&g_pfEq, (LONG)(t1 - t0)); t0 = t1; }
            ApplyGain(bufs[i], n * outCh);
            hdr[i].dwBufferLength = n * blk;
            waveOutWrite(hwo, &hdr[i], sizeof(WAVEHDR));
            if (g_prof) InterlockedExchangeAdd(&g_pfOut, (LONG)(NowUs() - t0));
            busy[i] = 1;
            started = 1;
        }
        if (err) break;

        {
            MMTIME mt;
            unsigned long long pf = 0;
            memset(&mt, 0, sizeof mt);
            mt.wType = TIME_SAMPLES;
            if (waveOutGetPosition(hwo, &mt, sizeof mt) == MMSYSERR_NOERROR) {
                if (mt.wType == TIME_SAMPLES) pf = mt.u.sample;
                else if (mt.wType == TIME_BYTES) pf = mt.u.cb / blk;
                else if (mt.wType == TIME_MS) pf = (unsigned long long)mt.u.ms * d->rate / 1000;
                InterlockedExchange(&t->posMs, (LONG)((base + pf) * 1000 / d->rate));
            }
        }

        if (eof) {
            int all = 1;
            for (i = 0; i < nbuf; i++) if (busy[i] && !(hdr[i].dwFlags & WHDR_DONE)) all = 0;
            if (all) { ended = 1; break; }
        }
        WaitForSingleObject(ev, 30);
    }

done:
    if (hwo) {
        waveOutReset(hwo);
        for (i = 0; i < nbuf; i++)
            if (hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(hwo, &hdr[i], sizeof(WAVEHDR));
        waveOutClose(hwo);
    }
    for (i = 0; i < NBMAX; i++) free(bufs[i]);
    free(tmp);
    if (ev) CloseHandle(ev);
    Dec_Close(d);
    t->buffering = 0;
    if (!t->cancel) {
        if (!opened) PostMessage(g_hwnd, WM_APP_END, (WPARAM)gen, (LPARAM)(err ? err : 4));
        else if (ended || err) PostMessage(g_hwnd, WM_APP_END, (WPARAM)gen, (LPARAM)err);
    }
    TrackRelease(t);
    return 0;
}

static void StopPlayer(void)
{
    if (g_trk) TrackAbort(g_trk);
    if (g_pthread) {
        WaitForSingleObject(g_pthread, 3000);
        CloseHandle(g_pthread);
        g_pthread = NULL;
    }
    if (g_trk) {
        TrackRelease(g_trk);
        g_trk = NULL;
    }
}

/* ------------------------------------------------------------ управление */

static void ResetPlayUi(void)
{
    g_lastSec = g_lastLen = -1;
    SendMessage(g_seek, TBM_SETPOS, TRUE, 0);
    SetWindowTextW(g_time, L"0:00 / 0:00");
}

static void StopAll(void)
{
    InterlockedIncrement(&g_gen);
    StopPlayer();
    g_state = ST_STOPPED;
    g_len = 0;
    ResetPlayUi();
    SetWindowTextW(g_title, L"");
    SetWindowTextW(g_info, L"");
    SetWindowTextW(g_hwnd, L"nav-xp");
    TrayUpdateTip();
    SetStatus(T(L"Остановлено"));
}

/* Выбор способа получения по выбранному качеству. raw=1 - родной файл, иначе перекодирование на сервере. */
static int TransBr(void) { return g_qFmt ? g_qBr : g_bitrate; }

static int ChooseKind(const SongInfo *si, int *raw, const wchar_t **ext, const char **fmt)
{
    const char *sf = si->suffix;
    int native = 0, kind = DEC_MP3;
    const wchar_t *e = L"mp3";

    if (!_stricmp(sf, "mp3"))  { native = 1; kind = DEC_MP3;  e = L"mp3"; }
    else if (!_stricmp(sf, "flac")) { native = 1; kind = DEC_FLAC; e = L"flac"; }
#ifdef USE_OPUS
    else if (!_stricmp(sf, "opus")) { native = 1; kind = DEC_OPUS; e = L"opus"; }
#endif
    *fmt = "raw";
    if (native) {
        if (g_qFmt == 0) { *raw = 1; *ext = e; return kind; }
        /* mp3, который уже не тяжелее выбранного битрейта, перекодировать незачем */
        if (g_qFmt == 1 && kind == DEC_MP3 && si->bitrate > 0 && si->bitrate <= g_qBr) { *raw = 1; *ext = e; return kind; }
    }
    *raw = 0;
#ifdef USE_OPUS
    if (g_qFmt == 2) { *fmt = "opus"; *ext = L"opus"; return DEC_OPUS; }
#endif
    *fmt = "mp3";
    *ext = L"mp3";
    return DEC_MP3;
}

/* ------------------------------------------------------------ очередь воспроизведения */
static unsigned RandN(unsigned n)
{
    return (((unsigned)rand() << 15) ^ (unsigned)rand()) % n;
}

static int QReserve(int need)
{
    QItem *p;
    int cap;
    if (need <= g_qcap) return 1;
    cap = g_qcap ? g_qcap : 64;
    while (cap < need) cap *= 2;
    p = (QItem *)realloc(g_q, (size_t)cap * sizeof(QItem));
    if (!p) return 0;
    g_q = p;
    g_qcap = cap;
    return 1;
}

static void QSwap(int a, int b)
{
    QItem t;
    if (a == b) return;
    t = g_q[a]; g_q[a] = g_q[b]; g_q[b] = t;
}

static void QFill(QItem *q, HTREEITEM h, const Node *n)
{
    memset(q, 0, sizeof *q);
    lstrcpynA(q->id, n->id, sizeof q->id);
    q->si = *n->si;
    q->h = h;
}

/* Копирует в массив трек it или все песни, лежащие прямо под узлом it. Массив освобождает вызывающий. */
static int QGather(HTREEITEM it, QItem **out)
{
    Node *n = GetNode(it), *c;
    HTREEITEM ch;
    QItem *a;
    int cnt = 0, cap = 64;

    *out = NULL;
    if (!n) return 0;
    if (n->type == N_ARTIST || n->type == N_LIBRARY || n->type == N_PLAYLISTS || n->type == N_ROOT) return 0;
    a = (QItem *)malloc((size_t)cap * sizeof(QItem));
    if (!a) return 0;
    if (n->type == N_SONG) {
        if (!n->si) { free(a); return 0; }
        QFill(&a[0], it, n);
        *out = a;
        return 1;
    }
    TreeView_Expand(g_tree, it, TVE_EXPAND);
    ch = TreeView_GetChild(g_tree, it);
    while (ch) {
        c = GetNode(ch);
        if (c && c->type == N_SONG && c->si) {
            if (cnt >= cap) {
                QItem *b = (QItem *)realloc(a, (size_t)cap * 2 * sizeof(QItem));
                if (!b) break;
                a = b;
                cap *= 2;
            }
            QFill(&a[cnt++], ch, c);
        }
        ch = TreeView_GetNextSibling(g_tree, ch);
    }
    if (cnt == 0) { free(a); return 0; }
    *out = a;
    return cnt;
}

static int QInsert(int at, const QItem *arr, int cnt)
{
    int i;
    if (at < 0 || at > g_qn) at = g_qn;
    if (!QReserve(g_qn + cnt)) return 0;
    memmove(&g_q[at + cnt], &g_q[at], (size_t)(g_qn - at) * sizeof(QItem));
    for (i = 0; i < cnt; i++) { g_q[at + i] = arr[i]; g_q[at + i].seq = g_qseq++; }
    if (g_qpos >= at) g_qpos += cnt;
    g_qn += cnt;
    return 1;
}

/* Перемешивает очередь; элемент keep (если >= 0) ставится первым. */
static void QShuffleKeep(int keep)
{
    int i, j, start = 0;
    for (i = 0; i < g_qn; i++) g_q[i].seq = i;
    g_qseq = g_qn;
    if (keep >= 0 && keep < g_qn) { QSwap(0, keep); start = 1; }
    for (i = g_qn - 1; i > start; i--) {
        j = start + (int)RandN((unsigned)(i - start + 1));
        QSwap(i, j);
    }
}

static int QCmpSeq(const void *a, const void *b)
{
    int x = ((const QItem *)a)->seq, y = ((const QItem *)b)->seq;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Выключили «случайно»: вернуть исходный порядок. */
static void QUnshuffle(void)
{
    int cur = (g_qpos >= 0 && g_qpos < g_qn) ? g_q[g_qpos].seq : -1, i;
    if (g_qn > 1) qsort(g_q, (size_t)g_qn, sizeof(QItem), QCmpSeq);
    if (cur >= 0) {
        g_qpos = -1;
        for (i = 0; i < g_qn; i++) if (g_q[i].seq == cur) { g_qpos = i; break; }
    }
}

static void UpdateNowPlaying(void)
{
    SongInfo *si = &g_np.si;
    wchar_t info[900], line[300], cap[320], suf[24];
    int raw;
    const wchar_t *ext;
    const char *fmt;

    info[0] = 0;
    if (si->artist[0]) { wsprintfW(line, T(L"Исполнитель: %s\r\n"), si->artist); lstrcatW(info, line); }
    if (si->album[0]) { wsprintfW(line, T(L"Альбом: %s\r\n"), si->album); lstrcatW(info, line); }
    if (si->year > 0) { wsprintfW(line, T(L"Год: %d\r\n"), si->year); lstrcatW(info, line); }
    if (si->track > 0) {
        if (si->disc > 1) wsprintfW(line, T(L"Трек %d (диск %d)\r\n"), si->track, si->disc);
        else wsprintfW(line, T(L"Трек %d\r\n"), si->track);
        lstrcatW(info, line);
    }
    wsprintfW(line, T(L"В очереди: %d из %d\r\n"), g_qpos + 1, g_qn);
    lstrcatW(info, line);

    ChooseKind(si, &raw, &ext, &fmt);
    MultiByteToWideChar(CP_UTF8, 0, si->suffix, -1, suf, 24);
    CharUpperW(suf);
    if (raw) {
        if (si->bitrate > 0) wsprintfW(line, T(L"Формат: %s, %d kbps"), suf, si->bitrate);
        else wsprintfW(line, T(L"Формат: %s"), suf);
    } else {
        wsprintfW(line, T(L"Формат: %s -> %s %d kbps (с сервера)"), suf, fmt[0] == 'o' ? L"Opus" : L"MP3", TransBr());
    }
    lstrcatW(info, line);

    SetWindowTextW(g_title, si->title[0] ? si->title : T(L"(без названия)"));
    SetWindowTextW(g_info, info);
    if (si->artist[0]) wsprintfW(cap, L"%s - %s - nav-xp", si->artist, si->title);
    else wsprintfW(cap, L"%s - nav-xp", si->title);
    SetWindowTextW(g_hwnd, cap);
    RequestCover(si);
    SyncLikeUi();
    TrayUpdateTip();
}

/* Запускает загрузку и воспроизведение g_np. */
static void PlayCore(void)
{
    SongInfo *si = &g_np.si;
    Track *t;
    char extra[300], idenc[200];
    wchar_t dir[MAX_PATH];
    const wchar_t *ext;
    const char *fmt;
    int raw;
    uintptr_t th;

    InterlockedIncrement(&g_gen);
    StopPlayer();
    g_state = ST_LOADING;
    g_wasBuf = 0;
    g_len = 0;

    t = (Track *)calloc(1, sizeof(Track));
    if (!t) { g_state = ST_STOPPED; return; }
    t->refs = 3;                         /* UI, загрузка, воспроизведение */
    t->gen = g_gen;
    t->seekMs = -1;
    t->hw = INVALID_HANDLE_VALUE;
    t->hr = INVALID_HANDLE_VALUE;
    t->kind = ChooseKind(si, &raw, &ext, &fmt);
    t->raw = raw;
    t->durSec = si->duration;

    UrlEnc(g_np.id, idenc, sizeof idenc);
    if (raw) _snprintf(extra, sizeof extra, "id=%s&format=raw", idenc);
    else     _snprintf(extra, sizeof extra, "id=%s&format=%s&maxBitRate=%d", idenc, fmt, TransBr());
    extra[sizeof extra - 1] = 0;
    BuildUrl(t->url, sizeof t->url, "stream", extra);
    GetTempPathW(MAX_PATH, dir);
    wsprintfW(t->path, L"%sndxp_%lu_%ld.%s", dir, (unsigned long)GetCurrentProcessId(), (long)g_gen, ext);

    t->hw = CreateFileW(t->path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (t->hw != INVALID_HANDLE_VALUE)
        t->hr = CreateFileW(t->path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (t->hw == INVALID_HANDLE_VALUE || t->hr == INVALID_HANDLE_VALUE) {
        g_state = ST_STOPPED;
        SetStatus(T(L"Не удалось создать временный файл (проверьте папку TEMP)"));
        t->refs = 1;
        TrackRelease(t);
        return;
    }

    g_trk = t;
    ResetPlayUi();
    UpdateNowPlaying();
    SetStatus(raw ? T(L"Загрузка трека...") : T(L"Загрузка трека (перекодирование на сервере)..."));
    if (g_cur) {
        TreeView_EnsureVisible(g_tree, g_cur);
        TreeView_SelectItem(g_tree, g_cur);
        SaveLastPath(g_cur);
    }

    th = _beginthreadex(NULL, 0, DlThread, t, 0, NULL);
    if (!th) {
        g_trk = NULL;
        g_state = ST_STOPPED;
        SetStatus(T(L"Не удалось запустить загрузку"));
        t->refs = 1;
        TrackRelease(t);
        return;
    }
    CloseHandle((HANDLE)th);

    th = _beginthreadex(NULL, 0, PlayThread, t, 0, NULL);
    if (!th) {
        TrackRelease(t);                 /* ссылка потока воспроизведения, который не запустился */
        StopPlayer();                    /* отменяет загрузку и отпускает ссылку UI */
        g_state = ST_STOPPED;
        SetStatus(T(L"Не удалось запустить воспроизведение"));
        return;
    }
    g_pthread = (HANDLE)th;
}

static void PlayQ(int idx)
{
    if (idx < 0 || idx >= g_qn) return;
    g_qpos = idx;
    g_np = g_q[idx];
    g_npOk = 1;
    g_cur = g_np.h;
    PlayCore();
    QRefreshUi();
}

static int Playable(const Node *n)
{
    return n && n->type != N_ARTIST && n->type != N_LIBRARY && n->type != N_PLAYLISTS && n->type != N_ROOT;
}

/* Заменяет очередь содержимым узла (трек - весь список, в котором он лежит) и играет. */
static void PlayTree(HTREEITEM s)
{
    Node *n = GetNode(s);
    HTREEITEM par = NULL;
    QItem *a = NULL;
    int cnt, start = 0, i;

    if (!Playable(n)) return;
    if (n->type == N_SONG) {
        par = TreeView_GetParent(g_tree, s);
        cnt = par ? QGather(par, &a) : 0;
        if (cnt > 0) {
            for (i = 0; i < cnt; i++) if (a[i].h == s) { start = i; break; }
        } else {
            par = NULL;
            cnt = QGather(s, &a);
        }
    } else {
        par = s;
        cnt = QGather(s, &a);
    }
    if (cnt <= 0) { SetStatus(T(L"Здесь нет треков для воспроизведения")); return; }
    g_qn = 0;
    g_qpos = -1;
    g_qseq = 0;
    if (!QInsert(0, a, cnt)) { free(a); SetStatus(T(L"Не хватает памяти")); return; }
    free(a);
    g_qOrigin = par;
    g_qpos = start;
    if (g_shuffle) {
        QShuffleKeep(n->type == N_SONG ? start : -1);
        g_qpos = 0;
    }
    PlayQ(g_qpos);
}

/* Добавить выделенное в дереве в очередь: next=1 - сразу после играющего. */
static void EnqueueSel(int next)
{
    HTREEITEM s = TreeView_GetSelection(g_tree);
    Node *n = GetNode(s);
    QItem *a = NULL;
    int cnt, was = g_qn, at;
    wchar_t msg[100];

    if (!Playable(n)) { SetStatus(T(L"Выберите трек, альбом или плейлист")); return; }
    cnt = QGather(s, &a);
    if (cnt <= 0) { SetStatus(T(L"Здесь нет треков")); return; }
    at = next ? (g_qpos >= 0 ? g_qpos + 1 : 0) : g_qn;
    if (!QInsert(at, a, cnt)) { free(a); SetStatus(T(L"Не хватает памяти")); return; }
    free(a);
    if (was == 0) {
        g_qOrigin = (n->type == N_SONG) ? TreeView_GetParent(g_tree, s) : s;
        if (g_state == ST_STOPPED) { PlayQ(0); return; }
    }
    QRefreshUi();
    wsprintfW(msg, next ? T(L"Будет следующим: %d") : T(L"Добавлено в очередь: %d"), cnt);
    SetStatus(msg);
}

/* Конец очереди без повтора: если очередь собрана из альбома, берём следующий альбом исполнителя. */
static int QAppendNextAlbum(void)
{
    HTREEITEM na;
    Node *pn;
    QItem *a = NULL;
    int cnt;

    if (!g_qOrigin) return 0;
    pn = GetNode(g_qOrigin);
    if (!pn || pn->type != N_ALBUM) return 0;
    na = TreeView_GetNextSibling(g_tree, g_qOrigin);
    pn = GetNode(na);
    if (!pn || pn->type != N_ALBUM) return 0;
    cnt = QGather(na, &a);
    if (cnt <= 0) return 0;
    if (!QInsert(g_qn, a, cnt)) { free(a); return 0; }
    free(a);
    g_qOrigin = na;
    return 1;
}

/* Декодер открыт и звуковое устройство занято: звук пошёл. */
static void OnOpened(LONG gen)
{
    if (gen != g_gen || !g_trk || g_state != ST_LOADING) return;
    g_len = (int)g_trk->lenMs;
    SendMessage(g_seek, TBM_SETRANGE, TRUE, MAKELONG(0, g_len / 1000));
    g_state = ST_PLAYING;
    SetStatus(T(L"Воспроизведение"));
}

static void PlayNext(void)
{
    if (g_qn <= 0) { if (g_npOk) StopAll(); return; }
    if (g_qpos + 1 < g_qn) { PlayQ(g_qpos + 1); return; }
    if (g_repeat == 1) {                              /* повтор списка */
        if (g_shuffle && g_qn > 1) {
            char last[96];
            lstrcpynA(last, g_q[g_qn - 1].id, sizeof last);
            QShuffleKeep(-1);
            if (strcmp(g_q[0].id, last) == 0) QSwap(0, 1 + (int)RandN((unsigned)(g_qn - 1)));
        }
        PlayQ(0);
        return;
    }
    if (!g_shuffle && QAppendNextAlbum()) { PlayQ(g_qpos + 1); return; }
    StopAll();
}

static void DoSeek(int sec)
{
    if (!g_trk || (g_state != ST_PLAYING && g_state != ST_PAUSED)) return;
    if (sec < 0) sec = 0;
    InterlockedExchange(&g_trk->posMs, (LONG)sec * 1000);
    InterlockedExchange(&g_trk->seekMs, (LONG)sec * 1000);
}

static void SeekRel(int deltaSec)
{
    int cur = (g_trk ? (int)(g_trk->posMs / 1000) : 0) + deltaSec;
    int max = g_len / 1000 - 1;
    if (max < 0) max = 0;
    if (cur > max) cur = max;
    DoSeek(cur);
}

static void PlayPrev(void)
{
    if (!g_npOk) return;
    if ((g_state == ST_PLAYING || g_state == ST_PAUSED) && g_trk && g_trk->posMs > 3000) { DoSeek(0); return; }
    if (g_qpos > 0) PlayQ(g_qpos - 1);
    else DoSeek(0);
}

static void PlaySelected(void)
{
    HTREEITEM s = TreeView_GetSelection(g_tree);
    Node *n = GetNode(s);
    if (g_qn > 0 && g_qpos >= 0 && g_npOk && s && s == g_np.h) { PlayQ(g_qpos); return; }   /* после «Стоп» - продолжить очередь */
    if (Playable(n)) { PlayTree(s); return; }
    if (g_qn > 0) PlayQ(g_qpos >= 0 ? g_qpos : 0);
}

static void OnPlayBtn(void)
{
    if (g_state == ST_PLAYING) {
        if (g_trk) g_trk->pause = 1;
        g_state = ST_PAUSED;
        SetStatus(T(L"Пауза"));
    } else if (g_state == ST_PAUSED) {
        if (g_trk) g_trk->pause = 0;
        g_state = ST_PLAYING;
        SetStatus(T(L"Воспроизведение"));
    } else if (g_state == ST_STOPPED) {
        PlaySelected();
    }
}

static void SetVolume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    SendMessage(g_vol, TBM_SETPOS, TRUE, v);
    if (g_volLbl) { wchar_t vt[40]; wsprintfW(vt, T(L"Громкость: %d"), v); SetWindowTextW(g_volLbl, vt); }
    InterlockedExchange(&g_gain, (LONG)((v * v * 256) / 10000));
}

static void ShowLoad(void)
{
    static DWORD lastTick;
    static unsigned long long lastCpu;
    static LONG lDec, lEq, lOut, lWait;
    FILETIME c, e, k, u;
    unsigned long long cpu;
    DWORD now = GetTickCount(), dw;
    LONG vDec = g_pfDec, vEq = g_pfEq, vOut = g_pfOut, vWait = g_pfWait;
    int pProc, pDec, pEq, pOut, pWait;
    wchar_t b[340], a[40], d[40], q[40], o[40], w[40];

    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    cpu = (((unsigned long long)k.dwHighDateTime << 32) | k.dwLowDateTime) +
          (((unsigned long long)u.dwHighDateTime << 32) | u.dwLowDateTime);
    dw = now - lastTick;
    if (lastTick && dw >= 500) {
        int dd = vDec - lDec - (vWait - lWait);
        if (dd < 0) dd = 0;
        pProc = (int)((cpu - lastCpu) / ((unsigned long long)dw * 10));      /* десятые доли процента */
        pDec = (int)((unsigned)dd / dw);
        pEq = (int)((unsigned)(vEq - lEq) / dw);
        pOut = (int)((unsigned)(vOut - lOut) / dw);
        pWait = (int)((unsigned)(vWait - lWait) / dw);
        wsprintfW(a, L"%d.%d%s", pProc / 10, pProc % 10, L"%");
        wsprintfW(d, L"%d.%d%s", pDec / 10, pDec % 10, L"%");
        wsprintfW(q, L"%d.%d%s", pEq / 10, pEq % 10, L"%");
        wsprintfW(o, L"%d.%d%s", pOut / 10, pOut % 10, L"%");
        wsprintfW(w, L"%d.%d%s", pWait / 10, pWait % 10, L"%");
        wsprintfW(b, T(L"Нагрузка: вся программа %s | декодер %s | эквалайзер %s | вывод %s | ожидание данных %s | обрывов звука: %d"),
                  a, d, q, o, w, (int)g_underruns);
        SetStatus(b);
    }
    if (!lastTick || dw >= 500) {
        lastTick = now; lastCpu = cpu;
        lDec = vDec; lEq = vEq; lOut = vOut; lWait = vWait;
    }
}

static void OnTimer(void)
{
    wchar_t b[64], t1[16], t2[16];
    int pos, buf;
    static int tick;
    if (g_state != ST_PLAYING && g_state != ST_PAUSED) return;
    if (!IsWindowVisible(g_hwnd)) return;          /* свёрнуто в трей - интерфейс не обновляем */
    pos = g_trk ? (int)g_trk->posMs : 0;
    /* перерисовываем время и ползунок только когда сменилась секунда */
    if (pos / 1000 != g_lastSec || g_len != g_lastLen) {
        g_lastSec = pos / 1000;
        g_lastLen = g_len;
        if (!g_drag) SendMessage(g_seek, TBM_SETPOS, TRUE, pos / 1000);
        FmtTime(t1, pos); FmtTime(t2, g_len);
        wsprintfW(b, L"%s / %s", t1, t2);
        SetWindowTextW(g_time, b);
    }
    buf = (g_state == ST_PLAYING && g_trk && g_trk->buffering) ? 1 : 0;
    if (buf != g_wasBuf) {
        g_wasBuf = buf;
        SetStatus(buf ? T(L"Буферизация (ожидание данных с сервера)...") : T(L"Воспроизведение"));
    }
    if (g_prof && !buf && (++tick & 3) == 0) ShowLoad();
}

static void CleanupTemp(void)
{
    wchar_t dir[MAX_PATH], pat[MAX_PATH], full[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    GetTempPathW(MAX_PATH, dir);
    wsprintfW(pat, L"%sndxp_*", dir);
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        wsprintfW(full, L"%s%s", dir, fd.cFileName);
        DeleteFileW(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* ------------------------------------------------------------------- UI */

static HWND MkCtl(const wchar_t *cls, const wchar_t *txt, DWORD style, DWORD ex, int id)
{
    HWND h = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                             g_hwnd, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return h;
}

static void InitFonts(void)
{
    NONCLIENTMETRICS ncm;
    memset(&ncm, 0, sizeof ncm);
    ncm.cbSize = sizeof ncm;
    if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0)) {
        g_font = CreateFontIndirect(&ncm.lfMessageFont);
        ncm.lfMessageFont.lfWeight = FW_BOLD;
        g_fontQB = CreateFontIndirect(&ncm.lfMessageFont);
        ncm.lfMessageFont.lfHeight = (ncm.lfMessageFont.lfHeight * 13) / 10;
        g_fontB = CreateFontIndirect(&ncm.lfMessageFont);
    }
    if (!g_font) g_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if (!g_fontB) g_fontB = g_font;
    if (!g_fontQB) g_fontQB = g_font;
    {
        HDC dc = GetDC(NULL);
        TEXTMETRIC tm;
        HFONT of = (HFONT)SelectObject(dc, g_font);
        if (GetTextMetrics(dc, &tm)) g_qItemH = tm.tmHeight + 5;
        SelectObject(dc, of);
        ReleaseDC(NULL, dc);
    }
}

static void SyncModeUi(void)
{
    static const wchar_t *rt[3] = { L"Повтор: выкл", L"Повтор: список", L"Повтор: трек" };
    if (g_bShuf) SendMessage(g_bShuf, BM_SETCHECK, g_shuffle ? 1 : 0, 0);
    if (g_bRep) SetWindowTextW(g_bRep, T(rt[g_repeat]));
    if (g_mCtl) {
        CheckMenuItem(g_mCtl, IDM_SHUF, MF_BYCOMMAND | (g_shuffle ? MF_CHECKED : MF_UNCHECKED));
        CheckMenuItem(g_mCtl, IDM_REP, MF_BYCOMMAND | (g_repeat ? MF_CHECKED : MF_UNCHECKED));
    }
}

/* ---------------------------------------------------------------- избранное (лайки) */
typedef struct { char url[1400]; char id[96]; int want; } StarJob;

static unsigned __stdcall StarThread(void *p)
{
    StarJob *j = (StarJob *)p;
    DWORD len = 0;
    char *x;
    int ok = 0;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    x = HttpGet(j->url, &len);
    if (x) { ok = strstr(x, "status=\"ok\"") != NULL; free(x); }
    PostMessage(g_hwnd, WM_APP_STAR, (WPARAM)ok, (LPARAM)j);
    return 0;
}

static void SyncLikeUi(void)
{
    if (!g_bLike) return;
    EnableWindow(g_bLike, g_npOk ? TRUE : FALSE);
    SetWindowTextW(g_bLike, (g_npOk && g_np.si.starred) ? T(L"Из избранного") : T(L"В избранное"));
}

/* Ставит или снимает лайк с играющего трека. */
static void ToggleLike(void)
{
    StarJob *j;
    char idenc[200], extra[300];
    uintptr_t th;

    if (!g_npOk) { SetStatus(T(L"Сначала включите трек")); return; }
    j = (StarJob *)calloc(1, sizeof(StarJob));
    if (!j) return;
    lstrcpynA(j->id, g_np.id, sizeof j->id);
    j->want = g_np.si.starred ? 0 : 1;
    UrlEnc(g_np.id, idenc, sizeof idenc);
    _snprintf(extra, sizeof extra, "id=%s", idenc);
    extra[sizeof extra - 1] = 0;
    BuildUrl(j->url, sizeof j->url, j->want ? "star" : "unstar", extra);
    th = _beginthreadex(NULL, 0, StarThread, j, 0, NULL);
    if (!th) { free(j); return; }
    CloseHandle((HANDLE)th);
    SetStatus(j->want ? T(L"Добавляю в избранное...") : T(L"Убираю из избранного..."));
}

/* ---------------------------------------------------------------- значок и трей */
static HICON MakeAppIcon(int sz)
{
    HDC sdc = GetDC(NULL), dc;
    HBITMAP col, msk, ob;
    HBRUSH bg;
    ICONINFO ii;
    HICON ic = NULL;
    POINT pf[4];
    RECT r;

    if (!sdc) return NULL;
    dc = CreateCompatibleDC(sdc);
    col = CreateCompatibleBitmap(sdc, sz, sz);
    msk = CreateBitmap(sz, sz, 1, 1, NULL);
    if (dc && col && msk) {
        r.left = r.top = 0; r.right = r.bottom = sz;
        ob = (HBITMAP)SelectObject(dc, col);
        bg = CreateSolidBrush(RGB(24, 104, 196));
        FillRect(dc, &r, bg);
        DeleteObject(bg);
        SelectObject(dc, GetStockObject(WHITE_BRUSH));
        SelectObject(dc, GetStockObject(WHITE_PEN));
        Ellipse(dc, 3 * sz / 16, 9 * sz / 16, 9 * sz / 16, 14 * sz / 16);       /* головка ноты */
        Rectangle(dc, 8 * sz / 16, 2 * sz / 16, 10 * sz / 16, 12 * sz / 16);    /* штиль */
        pf[0].x = 10 * sz / 16; pf[0].y = 2 * sz / 16;                          /* флажок */
        pf[1].x = 14 * sz / 16; pf[1].y = 5 * sz / 16;
        pf[2].x = 14 * sz / 16; pf[2].y = 8 * sz / 16;
        pf[3].x = 10 * sz / 16; pf[3].y = 5 * sz / 16;
        Polygon(dc, pf, 4);
        SelectObject(dc, msk);
        PatBlt(dc, 0, 0, sz, sz, BLACKNESS);                                    /* маска: всё непрозрачно */
        SelectObject(dc, ob);
        ii.fIcon = TRUE; ii.xHotspot = 0; ii.yHotspot = 0; ii.hbmMask = msk; ii.hbmColor = col;
        ic = CreateIconIndirect(&ii);
    }
    if (col) DeleteObject(col);
    if (msk) DeleteObject(msk);
    if (dc) DeleteDC(dc);
    ReleaseDC(NULL, sdc);
    return ic;
}

static void TrayUpdateTip(void)
{
    if (!g_inTray) return;
    g_nid.uFlags = NIF_TIP;
    GetWindowTextW(g_hwnd, g_nid.szTip, 128);
    g_nid.szTip[127] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void TrayAdd(void)
{
    if (g_inTray) return;
    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = g_icSmall ? g_icSmall : LoadIcon(NULL, IDI_APPLICATION);
    GetWindowTextW(g_hwnd, g_nid.szTip, 128);
    g_nid.szTip[127] = 0;
    g_inTray = Shell_NotifyIconW(NIM_ADD, &g_nid) ? 1 : 0;
}

static void TrayRemove(void)
{
    if (!g_inTray) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_inTray = 0;
}

static void TrayRestore(void)
{
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
    TrayRemove();
    g_lastSec = g_lastLen = -1;          /* перерисовать время при следующем тике */
}

static void TrayMenu(void)
{
    HMENU m = CreatePopupMenu();
    POINT pt;
    if (!m) return;
    AppendMenuW(m, MF_STRING, IDM_TRAY_SHOW, T(L"Показать окно"));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_PLAY, T(L"Пуск / пауза"));
    AppendMenuW(m, MF_STRING, ID_PREV, T(L"Предыдущий трек"));
    AppendMenuW(m, MF_STRING, ID_NEXT, T(L"Следующий трек"));
    AppendMenuW(m, MF_STRING | (g_npOk ? 0 : MF_GRAYED), IDM_LIKE,
                (g_npOk && g_np.si.starred) ? T(L"Убрать из избранного") : T(L"В избранное"));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_EXIT, T(L"Выход"));
    SetMenuDefaultItem(m, IDM_TRAY_SHOW, FALSE);
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);                    /* иначе меню не закрывается кликом мимо */
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessage(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static void ToggleShuffle(void)
{
    g_shuffle = !g_shuffle;
    if (g_qn > 1) {
        if (g_shuffle) QShuffleKeep(g_qpos);
        else QUnshuffle();
        if (g_shuffle && g_qpos >= 0) g_qpos = 0;
        QRefreshUi();
    }
    SyncModeUi();
    SetStatus(g_shuffle ? T(L"Случайный порядок: очередь перемешана") : T(L"Случайный порядок: выключен, очередь в исходном порядке"));
}

static void ToggleProf(void)
{
    g_prof = !g_prof;
    g_pfDec = g_pfEq = g_pfOut = g_pfWait = 0;
    g_underruns = 0;
    if (g_mCtl) CheckMenuItem(g_mCtl, IDM_PROF, MF_BYCOMMAND | (g_prof ? MF_CHECKED : MF_UNCHECKED));
    SetStatus(g_prof ? T(L"Показ нагрузки включён: данные появятся через пару секунд") : T(L"Показ нагрузки выключен"));
}

static void CycleRepeat(void)
{
    g_repeat = (g_repeat + 1) % 3;
    SyncModeUi();
    SetStatus(g_repeat == 0 ? T(L"Повтор: выключен") : (g_repeat == 1 ? T(L"Повтор: весь список") : T(L"Повтор: один трек")));
}

static int QualitySel(void)
{
    int k;
    for (k = 0; k < QN; k++)
        if (g_qopts[k].fmt == g_qFmt && (g_qFmt == 0 || g_qopts[k].br == g_qBr)) return k;
    return 0;
}

static void SetQuality(int k)
{
    wchar_t msg[200];
    if (k < 0 || k >= QN) return;
    g_qFmt = g_qopts[k].fmt;
    g_qBr = g_qopts[k].br ? g_qopts[k].br : g_qBr;
    if (g_mQ) CheckMenuRadioItem(g_mQ, IDM_Q_BASE, IDM_Q_BASE + QN - 1, IDM_Q_BASE + k, MF_BYCOMMAND);
    wsprintfW(msg, T(T(L"Качество: %s (применится со следующего трека)")), T(g_qopts[k].name));
    SetStatus(msg);
}

/* ------------------------------------------------------------ окно эквалайзера */
#define ID_EQ_ON     400
#define ID_EQ_PRESET 401
#define ID_EQ_RESET  402
typedef struct { const wchar_t *name; signed char g[10]; } EqPreset;
static const EqPreset g_eqPresets[] = {
    { L"Плоский",          {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 } },
    { L"Усиленные басы",   { 10,  8,  6,  3,  1,  0,  0,  0,  0,  0 } },
    { L"Яркие верха",      {  0,  0,  0,  0,  0,  1,  3,  5,  7,  8 } },
    { L"Вокал",            { -2, -1,  0,  2,  4,  5,  4,  2,  0, -1 } },
    { L"Рок",              {  6,  5,  3, -1, -3, -1,  3,  5,  6,  6 } },
    { L"Поп",              { -2,  0,  3,  5,  6,  5,  3,  0, -1, -2 } },
    { L"Электроника",      {  7,  6,  2,  0, -2,  2,  1,  3,  6,  7 } },
    { L"Классика",         {  0,  0,  0,  0,  0,  0, -3, -4, -4, -6 } },
    { L"Тихо / ночью",     {  4,  3,  2,  0,  0,  0,  1,  2,  3,  2 } },
};
#define NPRESET ((int)(sizeof g_eqPresets / sizeof g_eqPresets[0]))
static HWND g_eqChk, g_eqCombo, g_eqSl[11], g_eqVal[11];

static void EqFmt(int v, wchar_t *out)
{
    int a = v < 0 ? -v : v;
    if (v == 0) { lstrcpyW(out, L"0"); return; }
    wsprintfW(out, L"%s%d.%d", v < 0 ? L"-" : L"+", a / 2, (a & 1) ? 5 : 0);
}

static void EqShowVal(int i)
{
    wchar_t t[24];
    EqFmt((int)(i == 0 ? g_eqPre : g_eqGain[i - 1]), t);
    SetWindowTextW(g_eqVal[i], t);
}

static int EqMatchPreset(void)
{
    int k, i, mb;
    for (k = 0; k < NPRESET; k++) {
        mb = 0;
        for (i = 0; i < 10; i++) {
            if (g_eqGain[i] != g_eqPresets[k].g[i]) break;
            if (g_eqPresets[k].g[i] > mb) mb = g_eqPresets[k].g[i];
        }
        if (i == 10 && g_eqPre == -(mb / 2)) return k;
    }
    return NPRESET;
}

static void EqUiRefresh(void)
{
    int i;
    if (!g_eqWnd) return;
    SendMessage(g_eqChk, BM_SETCHECK, g_eqOn ? 1 : 0, 0);
    for (i = 0; i < 11; i++) {
        SendMessage(g_eqSl[i], TBM_SETPOS, TRUE, 24 - (int)(i == 0 ? g_eqPre : g_eqGain[i - 1]));
        EqShowVal(i);
    }
    SendMessage(g_eqCombo, CB_SETCURSEL, EqMatchPreset(), 0);
}

static void EqApplyPreset(int k)
{
    int i, mb = 0;
    if (k < 0 || k >= NPRESET) return;
    for (i = 0; i < 10; i++) {
        g_eqGain[i] = g_eqPresets[k].g[i];
        if (g_eqPresets[k].g[i] > mb) mb = g_eqPresets[k].g[i];
    }
    g_eqPre = -(mb / 2);               /* запас по уровню, чтобы усиление не давало перегруза */
    if (k > 0) g_eqOn = 1;
    InterlockedIncrement(&g_eqVer);
    EqUiRefresh();
}

static HWND EqMk(HWND p, const wchar_t *cls, const wchar_t *txt, DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                             p, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessage(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static LRESULT CALLBACK EqWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    int i;
    switch (msg) {
    case WM_CREATE: {
        static const wchar_t *fl[11] = { L"Общ.", L"31", L"62", L"125", L"250", L"500", L"1k", L"2k", L"4k", L"8k", L"16k" };
        g_eqChk = EqMk(hwnd, L"BUTTON", T(L"Включить эквалайзер"), BS_AUTOCHECKBOX | WS_TABSTOP, 12, 10, 190, 22, ID_EQ_ON);
        g_eqCombo = EqMk(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 214, 8, 170, 240, ID_EQ_PRESET);
        EqMk(hwnd, L"BUTTON", T(L"Сброс"), BS_PUSHBUTTON | WS_TABSTOP, 396, 8, 80, 24, ID_EQ_RESET);
        for (i = 0; i < NPRESET; i++) SendMessage(g_eqCombo, CB_ADDSTRING, 0, (LPARAM)T(g_eqPresets[i].name));
        SendMessage(g_eqCombo, CB_ADDSTRING, 0, (LPARAM)T(L"Пользовательский"));
        for (i = 0; i < 11; i++) {
            int x = 12 + i * 52 + (i ? 10 : 0);
            g_eqVal[i] = EqMk(hwnd, L"STATIC", L"0", SS_CENTER, x, 46, 48, 16, 430 + i);
            g_eqSl[i] = EqMk(hwnd, TRACKBAR_CLASSW, L"", TBS_VERT | TBS_NOTICKS | WS_TABSTOP, x + 4, 64, 40, 150, 410 + i);
            SendMessage(g_eqSl[i], TBM_SETRANGE, TRUE, MAKELONG(0, 48));
            SendMessage(g_eqSl[i], TBM_SETPAGESIZE, 0, 2);
            SendMessage(g_eqSl[i], TBM_SETLINESIZE, 0, 1);
            EqMk(hwnd, L"STATIC", T(fl[i]), SS_CENTER, x, 218, 48, 16, 450 + i);
        }
        EqMk(hwnd, L"STATIC", T(L"Шкала: от -12 до +12 дБ, шаг 0,5 дБ. Частоты в Гц. Изменения слышны сразу."), 0, 12, 242, 580, 16, 470);
        EqUiRefresh();
        return 0; }

    case WM_VSCROLL:
        for (i = 0; i < 11; i++) {
            if (g_eqSl[i] == (HWND)lp) {
                int v = 24 - (int)SendMessage(g_eqSl[i], TBM_GETPOS, 0, 0);
                if (i == 0) g_eqPre = v; else g_eqGain[i - 1] = v;
                InterlockedIncrement(&g_eqVer);
                EqShowVal(i);
                SendMessage(g_eqCombo, CB_SETCURSEL, EqMatchPreset(), 0);
                break;
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_EQ_ON:
            g_eqOn = (SendMessage(g_eqChk, BM_GETCHECK, 0, 0) == 1) ? 1 : 0;
            InterlockedIncrement(&g_eqVer);
            break;
        case ID_EQ_PRESET:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int k = (int)SendMessage(g_eqCombo, CB_GETCURSEL, 0, 0);
                if (k >= 0 && k < NPRESET) EqApplyPreset(k);
            }
            break;
        case ID_EQ_RESET:
            EqApplyPreset(0);
            break;
        case IDCANCEL:
            ShowWindow(hwnd, SW_HIDE);
            break;
        }
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        g_eqWnd = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void EqOpen(void)
{
    if (!g_eqWnd) {
        RECT mr, rc;
        DWORD st = WS_CAPTION | WS_SYSMENU;
        GetWindowRect(g_hwnd, &mr);
        rc.left = 0; rc.top = 0; rc.right = 604; rc.bottom = 266;
        AdjustWindowRectEx(&rc, st, FALSE, 0);
        g_eqWnd = CreateWindowExW(0, L"NDEqWnd", T(L"Эквалайзер"), st, mr.left + 40, mr.top + 70,
                                  rc.right - rc.left, rc.bottom - rc.top, g_hwnd, NULL, GetModuleHandle(NULL), NULL);
        if (!g_eqWnd) return;
    }
    EqUiRefresh();
    ShowWindow(g_eqWnd, SW_SHOW);
    SetFocus(g_eqWnd);
}

/* ---------------------------------------------------------------- серверы */
typedef struct { char url[700]; HWND wnd; } PingJob;

static unsigned __stdcall PingThread(void *p)
{
    PingJob *j = (PingJob *)p;
    DWORD len = 0;
    char *x;
    int res = 0;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    x = HttpGet(j->url, &len);
    if (x) {
        if (strstr(x, "status=\"ok\"")) res = 1;
        else if (strstr(x, "code=\"40\"") || strstr(x, "code=\"41\"")) res = 2;
        else res = 3;
        free(x);
    }
    if (!PostMessage(j->wnd, WM_APP_PING, (WPARAM)res, (LPARAM)j)) free(j);
    return 0;
}

static HWND SvCtl(const wchar_t *cls, const wchar_t *txt, DWORD st, DWORD ex, int id, int x, int y, int w, int h)
{
    HWND c = CreateWindowExW(ex, cls, txt, WS_CHILD | WS_VISIBLE | st, x, y, w, h,
                             g_svWnd, (HMENU)(INT_PTR)id, GetModuleHandle(NULL), NULL);
    SendMessage(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void SvMsg(const wchar_t *t) { if (g_svMsg) SetWindowTextW(g_svMsg, t); }

static void SvStore(int i)
{
    wchar_t w[300];
    Server *sv;
    if (i < 0 || i >= g_nsrv || !g_svName) return;
    sv = &g_srv[i];
    GetWindowTextW(g_svName, w, 64);
    lstrcpynW(sv->name, w, 64);
    GetWindowTextW(g_svUrl, w, 300);
    ToUtf8(w, sv->url, sizeof sv->url);
    NormalizeUrl(sv->url);
    GetWindowTextW(g_svUser, w, 128);
    ToUtf8(w, sv->user, sizeof sv->user);
    GetWindowTextW(g_svPass, w, 128);
    ToUtf8(w, sv->pass, sizeof sv->pass);
    if (!sv->name[0]) wsprintfW(sv->name, T(L"Сервер %d"), i + 1);
}

static void SvLoad(void)
{
    wchar_t w[300];
    int ok = (g_svSel >= 0 && g_svSel < g_nsrv);
    EnableWindow(g_svName, ok);
    EnableWindow(g_svUrl, ok);
    EnableWindow(g_svUser, ok);
    EnableWindow(g_svPass, ok);
    if (!ok) {
        SetWindowTextW(g_svName, L""); SetWindowTextW(g_svUrl, L"");
        SetWindowTextW(g_svUser, L""); SetWindowTextW(g_svPass, L"");
        return;
    }
    SetWindowTextW(g_svName, g_srv[g_svSel].name);
    MultiByteToWideChar(CP_UTF8, 0, g_srv[g_svSel].url, -1, w, 300);   SetWindowTextW(g_svUrl, w);
    MultiByteToWideChar(CP_UTF8, 0, g_srv[g_svSel].user, -1, w, 300);  SetWindowTextW(g_svUser, w);
    MultiByteToWideChar(CP_UTF8, 0, g_srv[g_svSel].pass, -1, w, 300);  SetWindowTextW(g_svPass, w);
}

static void SvFillList(void)
{
    int i;
    wchar_t w[100];
    SendMessage(g_svList, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < g_nsrv; i++) {
        if (i == g_srvActive) wsprintfW(w, T(L"%s  (активный)"), g_srv[i].name);
        else lstrcpynW(w, g_srv[i].name, 100);
        SendMessage(g_svList, LB_ADDSTRING, 0, (LPARAM)w);
    }
    if (g_svSel >= 0 && g_svSel < g_nsrv) SendMessage(g_svList, LB_SETCURSEL, (WPARAM)g_svSel, 0);
}

static void SvAddNew(void)
{
    Server *sv;
    if (g_nsrv >= MAXSRV) { SvMsg(T(L"Достигнуто максимальное число серверов")); return; }
    SvStore(g_svSel);
    sv = &g_srv[g_nsrv];
    memset(sv, 0, sizeof *sv);
    wsprintfW(sv->name, T(L"Сервер %d"), g_nsrv + 1);
    lstrcpyA(sv->url, "http://");
    g_nsrv++;
    g_svSel = g_nsrv - 1;
    SvFillList();
    SvLoad();
    SetFocus(g_svName);
    SendMessage(g_svName, EM_SETSEL, 0, -1);
}

static void SvDelete(void)
{
    int i;
    if (g_svSel < 0 || g_svSel >= g_nsrv) return;
    for (i = g_svSel; i < g_nsrv - 1; i++) g_srv[i] = g_srv[i + 1];
    g_nsrv--;
    if (g_srvActive == g_svSel) g_srvActive = 0;
    else if (g_srvActive > g_svSel) g_srvActive--;
    if (g_svSel >= g_nsrv) g_svSel = g_nsrv - 1;
    SvFillList();
    SvLoad();
    SvMsg(T(L"Сервер удалён"));
}

static void SvTest(void)
{
    PingJob *j;
    Server *sv;
    char in[300], tok[40], salt[24], ue[400];
    uintptr_t th;

    if (g_svSel < 0 || g_svSel >= g_nsrv) { SvMsg(T(L"Сначала добавьте сервер")); return; }
    SvStore(g_svSel);
    sv = &g_srv[g_svSel];
    if (strlen(sv->url) < 8) { SvMsg(T(L"Введите адрес сервера, например http://192.168.1.10:4533")); return; }
    j = (PingJob *)calloc(1, sizeof *j);
    if (!j) return;
    _snprintf(salt, sizeof salt, "%08x%04x", (unsigned)GetTickCount(), (unsigned)(rand() & 0xFFFF));
    salt[sizeof salt - 1] = 0;
    _snprintf(in, sizeof in, "%s%s", sv->pass, salt);
    in[sizeof in - 1] = 0;
    if (!Md5Hex(in, tok)) { free(j); SvMsg(T(L"Не удалось посчитать подпись (CryptoAPI)")); return; }
    UrlEnc(sv->user, ue, sizeof ue);
    _snprintf(j->url, sizeof j->url, "%s/rest/ping.view?u=%s&t=%s&s=%s&v=1.13.0&c=nav-xp", sv->url, ue, tok, salt);
    j->url[sizeof j->url - 1] = 0;
    j->wnd = g_svWnd;
    th = _beginthreadex(NULL, 0, PingThread, j, 0, NULL);
    if (!th) { free(j); SvMsg(T(L"Не удалось запустить проверку")); return; }
    CloseHandle((HANDLE)th);
    EnableWindow(GetDlgItem(g_svWnd, SV_TEST), FALSE);
    SvMsg(T(L"Проверка соединения..."));
}

static void SvConnect(void)
{
    Server *sv;
    int changed;
    if (g_svSel < 0 || g_svSel >= g_nsrv) { SvMsg(T(L"Сначала добавьте сервер")); return; }
    SvStore(g_svSel);
    sv = &g_srv[g_svSel];
    if (strlen(sv->url) < 8) { SvMsg(T(L"Введите адрес сервера, например http://192.168.1.10:4533")); return; }
    changed = (g_svSel != g_srvActive) || strcmp(sv->url, g_url) != 0 || strcmp(sv->user, g_user) != 0 || strcmp(sv->pass, g_pass) != 0;
    if (changed && (g_npOk || g_qn > 0)) {          /* треки старого сервера на новом не воспроизвести */
        StopAll();
        g_qn = 0;
        g_qpos = -1;
        g_qOrigin = NULL;
        g_npOk = 0;
        QRefreshUi();
        SyncLikeUi();
    }
    g_srvActive = g_svSel;
    SaveState(g_hwnd);
    DestroyWindow(g_svWnd);
    PostMessage(g_hwnd, WM_APP_LOAD, 0, 0);
}

static LRESULT CALLBACK SvWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_svWnd = hwnd;
        g_svList = SvCtl(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, SV_LIST, 8, 8, 160, 206);
        SvCtl(L"BUTTON", T(L"Добавить"), BS_PUSHBUTTON | WS_TABSTOP, 0, SV_ADD, 8, 220, 76, 24);
        SvCtl(L"BUTTON", T(L"Удалить"), BS_PUSHBUTTON | WS_TABSTOP, 0, SV_DEL, 92, 220, 76, 24);
        SvCtl(L"STATIC", T(L"Название:"), SS_LEFT, 0, 0, 180, 13, 70, 18);
        g_svName = SvCtl(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, SV_NAME, 254, 10, 300, 22);
        SvCtl(L"STATIC", T(L"Адрес:"), SS_LEFT, 0, 0, 180, 45, 70, 18);
        g_svUrl = SvCtl(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, SV_URL, 254, 42, 300, 22);
        SvCtl(L"STATIC", T(L"Логин:"), SS_LEFT, 0, 0, 180, 77, 70, 18);
        g_svUser = SvCtl(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, SV_USER, 254, 74, 300, 22);
        SvCtl(L"STATIC", T(L"Пароль:"), SS_LEFT, 0, 0, 180, 109, 70, 18);
        g_svPass = SvCtl(L"EDIT", L"", ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP, WS_EX_CLIENTEDGE, SV_PASS, 254, 106, 300, 22);
        g_svShow = SvCtl(L"BUTTON", T(L"Показать пароль"), BS_AUTOCHECKBOX | WS_TABSTOP, 0, SV_SHOW, 254, 134, 200, 20);
        SvCtl(L"BUTTON", T(L"Проверить"), BS_PUSHBUTTON | WS_TABSTOP, 0, SV_TEST, 254, 164, 130, 26);
        SvCtl(L"BUTTON", T(L"Подключиться"), BS_DEFPUSHBUTTON | WS_TABSTOP, 0, SV_CONN, 392, 164, 162, 26);
        g_svMsg = SvCtl(L"STATIC", T(L"Адрес вида http://192.168.1.10:4533 (https не поддерживается). Пароль хранится в файле .sav рядом с программой."), SS_LEFT, 0, SV_MSG, 180, 200, 374, 44);
        SvCtl(L"BUTTON", T(L"Закрыть"), BS_PUSHBUTTON | WS_TABSTOP, 0, SV_CLOSE, 478, 256, 76, 26);
        SendMessage(g_svName, EM_LIMITTEXT, 60, 0);
        SendMessage(g_svUrl, EM_LIMITTEXT, 250, 0);
        SendMessage(g_svUser, EM_LIMITTEXT, 120, 0);
        SendMessage(g_svPass, EM_LIMITTEXT, 120, 0);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case SV_LIST:
            if (HIWORD(wp) == LBN_SELCHANGE) {
                int ns = (int)SendMessage(g_svList, LB_GETCURSEL, 0, 0);
                if (ns >= 0 && ns != g_svSel) {
                    SvStore(g_svSel);
                    g_svSel = ns;
                    SvFillList();
                    SvLoad();
                }
            }
            break;
        case SV_ADD: SvAddNew(); break;
        case SV_DEL: SvDelete(); break;
        case SV_TEST: SvTest(); break;
        case SV_CONN: SvConnect(); break;
        case SV_SHOW:
            SendMessage(g_svPass, EM_SETPASSWORDCHAR, (SendMessage(g_svShow, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 0 : (WPARAM)L'*', 0);
            InvalidateRect(g_svPass, NULL, TRUE);
            break;
        case SV_CLOSE: case IDCANCEL:
            SendMessage(hwnd, WM_CLOSE, 0, 0);
            break;
        }
        return 0;

    case WM_APP_PING: {
        PingJob *j = (PingJob *)lp;
        EnableWindow(GetDlgItem(hwnd, SV_TEST), TRUE);
        if (wp == 1) SvMsg(T(L"Соединение установлено, логин и пароль приняты."));
        else if (wp == 2) SvMsg(T(L"Сервер отвечает, но логин или пароль неверные."));
        else if (wp == 3) SvMsg(T(L"Сервер ответил, но это не похоже на Navidrome / Subsonic. Проверьте адрес и порт."));
        else SvMsg(T(L"Нет ответа. Проверьте адрес, порт и что сервер запущен и доступен по сети."));
        free(j);
        return 0; }

    case WM_CLOSE:
        SvStore(g_svSel);
        SaveState(g_hwnd);
        if (g_srvActive >= g_nsrv) g_srvActive = 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_svWnd = g_svList = g_svName = g_svUrl = g_svUser = g_svPass = g_svShow = g_svMsg = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void SrvOpen(void)
{
    if (g_svWnd) {
        ShowWindow(g_svWnd, SW_SHOW);
        SetFocus(g_svWnd);
        return;
    }
    {
        RECT mr, rc;
        DWORD st = WS_CAPTION | WS_SYSMENU;
        GetWindowRect(g_hwnd, &mr);
        rc.left = 0; rc.top = 0; rc.right = 562; rc.bottom = 292;
        AdjustWindowRectEx(&rc, st, FALSE, 0);
        CreateWindowExW(0, L"NDSrvWnd", T(L"Серверы"), st, mr.left + 60, mr.top + 80,
                        rc.right - rc.left, rc.bottom - rc.top, g_hwnd, NULL, GetModuleHandle(NULL), NULL);
    }
    if (!g_svWnd) return;
    g_svSel = (g_nsrv > 0) ? ((g_srvActive >= 0 && g_srvActive < g_nsrv) ? g_srvActive : 0) : -1;
    SvFillList();
    SvLoad();
    if (g_nsrv == 0) SvAddNew();
    ShowWindow(g_svWnd, SW_SHOW);
    SetFocus(g_nsrv ? g_svName : g_svWnd);
}

/* ------------------------------------------------- мультимедиа-клавиши */
static void RegMediaKeys(int on)
{
    static const UINT keys[4] = { VK_MEDIA_PLAY_PAUSE, VK_MEDIA_NEXT_TRACK, VK_MEDIA_PREV_TRACK, VK_MEDIA_STOP };
    static int regd[4];
    int i;
    for (i = 0; i < 4; i++) {
        if (regd[i]) { UnregisterHotKey(g_hwnd, 9001 + i); regd[i] = 0; }
        if (on) regd[i] = RegisterHotKey(g_hwnd, 9001 + i, 0, keys[i]) ? 1 : 0;
    }
    if (g_mCtl) CheckMenuItem(g_mCtl, IDM_GMEDIA, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
}

/* ---------------------------------------------------------------- панель очереди */
static void QRefreshUi(void)
{
    wchar_t t[64];
    int i, sel, cnt;
    RECT rc;
    if (!g_qlist) return;
    cnt = (int)SendMessage(g_qlist, LB_GETCOUNT, 0, 0);
    if (cnt != g_qn) {
        sel = (int)SendMessage(g_qlist, LB_GETCURSEL, 0, 0);
        SendMessage(g_qlist, WM_SETREDRAW, FALSE, 0);
        SendMessage(g_qlist, LB_RESETCONTENT, 0, 0);
        SendMessage(g_qlist, LB_INITSTORAGE, (WPARAM)g_qn, (LPARAM)g_qn * 8);
        for (i = 0; i < g_qn; i++) SendMessage(g_qlist, LB_ADDSTRING, 0, (LPARAM)L"");
        if (sel >= 0 && sel < g_qn) SendMessage(g_qlist, LB_SETCURSEL, (WPARAM)sel, 0);
        SendMessage(g_qlist, WM_SETREDRAW, TRUE, 0);
    }
    if (g_qpos >= 0 && g_qpos < g_qn) {
        int top = (int)SendMessage(g_qlist, LB_GETTOPINDEX, 0, 0), vis;
        GetClientRect(g_qlist, &rc);
        vis = (rc.bottom - rc.top) / (g_qItemH > 0 ? g_qItemH : 16);
        if (vis < 1) vis = 1;
        if (g_qpos < top || g_qpos >= top + vis) {
            top = g_qpos - vis / 2;
            if (top < 0) top = 0;
            SendMessage(g_qlist, LB_SETTOPINDEX, (WPARAM)top, 0);
        }
    }
    InvalidateRect(g_qlist, NULL, FALSE);
    if (g_qn > 0 && g_qpos >= 0) wsprintfW(t, T(L"Очередь: %d из %d"), g_qpos + 1, g_qn);
    else wsprintfW(t, T(L"Очередь: %d"), g_qn);
    SetWindowTextW(g_qhead, t);
    EnableWindow(g_bQClr, g_qn > 0);
}

static void QPlaySel(void)
{
    int sel = (int)SendMessage(g_qlist, LB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < g_qn) PlayQ(sel);
}

static void QRemoveSel(void)
{
    int sel = (int)SendMessage(g_qlist, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= g_qn) return;
    if (sel == g_qpos && (g_state != ST_STOPPED || g_npOk)) { SetStatus(T(L"Играющий трек из очереди не убирается: выберите другой")); return; }
    memmove(&g_q[sel], &g_q[sel + 1], (size_t)(g_qn - sel - 1) * sizeof(QItem));
    g_qn--;
    if (sel < g_qpos) g_qpos--;
    else if (sel == g_qpos) g_qpos = -1;
    SendMessage(g_qlist, LB_DELETESTRING, (WPARAM)sel, 0);
    if (g_qn > 0) SendMessage(g_qlist, LB_SETCURSEL, (WPARAM)(sel < g_qn ? sel : g_qn - 1), 0);
    QRefreshUi();
}

static void QMove(int dir)
{
    int sel = (int)SendMessage(g_qlist, LB_GETCURSEL, 0, 0), t = sel + dir;
    if (sel < 0 || t < 0 || t >= g_qn) return;
    QSwap(sel, t);
    if (g_qpos == sel) g_qpos = t;
    else if (g_qpos == t) g_qpos = sel;
    SendMessage(g_qlist, LB_SETCURSEL, (WPARAM)t, 0);
    QRefreshUi();
}

static void QClear(void)
{
    if (g_npOk && g_qpos >= 0 && g_qpos < g_qn && g_state != ST_STOPPED) {
        if (g_qpos != 0) g_q[0] = g_q[g_qpos];
        g_qn = 1;
        g_qpos = 0;
    } else {
        g_qn = 0;
        g_qpos = -1;
    }
    g_qOrigin = NULL;
    QRefreshUi();
    SetStatus(T(L"Очередь очищена"));
}

static void QDrawItem(const DRAWITEMSTRUCT *d)
{
    int i = (int)d->itemID, sel, play, secs, cy;
    HDC dc = d->hDC;
    RECT r = d->rcItem, tr;
    COLORREF bg, fg;
    HBRUSH br, oldb;
    HPEN oldp;
    HFONT oldf;
    wchar_t buf[420], dur[24];
    POINT tri[3];

    if (i < 0 || i >= g_qn) { FillRect(dc, &r, (HBRUSH)(INT_PTR)(COLOR_WINDOW + 1)); return; }
    sel = (d->itemState & ODS_SELECTED) != 0;
    play = (i == g_qpos);
    if (sel) { bg = GetSysColor(COLOR_HIGHLIGHT); fg = GetSysColor(COLOR_HIGHLIGHTTEXT); }
    else if (play) { bg = RGB(255, 232, 150); fg = RGB(0, 0, 0); }
    else { bg = GetSysColor(COLOR_WINDOW); fg = GetSysColor(COLOR_WINDOWTEXT); }
    br = CreateSolidBrush(bg);
    FillRect(dc, &r, br);
    DeleteObject(br);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    oldf = (HFONT)SelectObject(dc, play ? g_fontQB : g_font);
    if (play) {                                         /* значок "играет" */
        cy = (r.top + r.bottom) / 2;
        tri[0].x = r.left + 4;  tri[0].y = cy - 5;
        tri[1].x = r.left + 4;  tri[1].y = cy + 5;
        tri[2].x = r.left + 12; tri[2].y = cy;
        br = CreateSolidBrush(sel ? fg : RGB(200, 30, 0));
        oldb = (HBRUSH)SelectObject(dc, br);
        oldp = (HPEN)SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, tri, 3);
        SelectObject(dc, oldp);
        SelectObject(dc, oldb);
        DeleteObject(br);
    }
    secs = g_q[i].si.duration;
    if (secs > 0) wsprintfW(dur, L"%d:%02d", secs / 60, secs % 60); else dur[0] = 0;
    if (g_q[i].si.artist[0]) wsprintfW(buf, L"%d. %s - %s", i + 1, g_q[i].si.artist, g_q[i].si.title);
    else wsprintfW(buf, L"%d. %s", i + 1, g_q[i].si.title);
    tr = r;
    tr.left += 18;
    tr.right -= 46;
    DrawTextW(dc, buf, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    tr = r;
    tr.left = r.right - 44;
    tr.right -= 4;
    DrawTextW(dc, dur, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    SelectObject(dc, oldf);
    if (d->itemState & ODS_FOCUS) DrawFocusRect(dc, &r);
}

static LRESULT CALLBACK QListProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_KEYDOWN) {
        if (w == VK_DELETE) { QRemoveSel(); return 0; }
        if (w == VK_RETURN) { QPlaySel(); return 0; }
    } else if (m == WM_RBUTTONDOWN) {
        POINT pt;
        DWORD it = (DWORD)SendMessage(h, LB_ITEMFROMPOINT, 0, l);
        HMENU pm;
        SetFocus(h);
        if (HIWORD(it) == 0 && (int)LOWORD(it) < g_qn) SendMessage(h, LB_SETCURSEL, LOWORD(it), 0);
        pm = CreatePopupMenu();
        if (pm) {
            int has = (int)SendMessage(h, LB_GETCURSEL, 0, 0) >= 0;
            AppendMenuW(pm, MF_STRING | (has ? 0 : MF_GRAYED), IDM_QPLAY, T(L"Воспроизвести"));
            AppendMenuW(pm, MF_STRING | (has ? 0 : MF_GRAYED), IDM_QDEL, T(L"Убрать из очереди\tDel"));
            AppendMenuW(pm, MF_SEPARATOR, 0, NULL);
            AppendMenuW(pm, MF_STRING | (g_qn ? 0 : MF_GRAYED), ID_QCLR, T(L"Очистить очередь"));
            SetMenuDefaultItem(pm, IDM_QPLAY, FALSE);
            GetCursorPos(&pt);
            SetForegroundWindow(g_hwnd);
            TrackPopupMenu(pm, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
            PostMessage(g_hwnd, WM_NULL, 0, 0);
            DestroyMenu(pm);
        }
        return 0;
    }
    return CallWindowProc(g_oldQList, h, m, w, l);
}

static void Layout(int w, int h)
{
    int rc = COVER_PX + 12;          /* правая колонка: обложка и громкость */
    int cx = w - rc;
    int lw = cx - 8;
    int top = COVER_PX + 62;
    if (lw < 200) lw = 200;
    MoveWindow(g_bPrev, 4,   4, 60, 24, TRUE);
    MoveWindow(g_bPlay, 68,  4, 90, 24, TRUE);
    MoveWindow(g_bStop, 162, 4, 60, 24, TRUE);
    MoveWindow(g_bNext, 226, 4, 60, 24, TRUE);
    MoveWindow(g_bShuf, 296, 4, 84, 24, TRUE);
    MoveWindow(g_bRep, 384, 4, 116, 24, TRUE);
    MoveWindow(g_search, 4, 33, lw - 74, 22, TRUE);
    MoveWindow(g_bFind, lw - 66, 32, 70, 24, TRUE);
    MoveWindow(g_title, 4, 62, lw - 112, 22, TRUE);
    MoveWindow(g_bLike, lw - 104, 60, 108, 24, TRUE);
    MoveWindow(g_info, 4, 86, lw, 100, TRUE);
    MoveWindow(g_seek, 4, 192, lw - 96, 24, TRUE);
    MoveWindow(g_time, lw - 88, 196, 92, 18, TRUE);
    MoveWindow(g_cover, cx + 4, 4, COVER_PX + 4, COVER_PX + 4, TRUE);
    MoveWindow(g_volLbl, cx + 4, COVER_PX + 12, COVER_PX + 4, 16, TRUE);
    MoveWindow(g_vol, cx + 4, COVER_PX + 30, COVER_PX + 4, 24, TRUE);
    {
        int qw = g_qW, tw, qx, ly;
        if (qw > w - 260) qw = w - 260;
        if (qw < 180) qw = 180;
        tw = w - qw - 5;
        qx = tw + 5;
        g_splitX = tw;
        g_splitTop = top + 4;
        ly = top + 52;
        MoveWindow(g_tree, 0, top + 4, tw, h - top - 24, TRUE);
        MoveWindow(g_qhead, qx + 2, top + 8, qw - 4, 18, TRUE);
        MoveWindow(g_bQClr, qx, top + 26, 84, 22, TRUE);
        MoveWindow(g_bQUp, qx + 90, top + 26, 60, 22, TRUE);
        MoveWindow(g_bQDn, qx + 154, top + 26, 60, 22, TRUE);
        MoveWindow(g_qlist, qx, ly, qw, h - 20 - ly, TRUE);
    }
    RedrawWindow(g_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    MoveWindow(g_status, 4, h - 18, w - 8, 16, TRUE);
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CHAR && w == VK_RETURN) {
        PostMessage(g_hwnd, WM_COMMAND, MAKEWPARAM(ID_FIND, BN_CLICKED), (LPARAM)g_bFind);
        return 0;
    }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { SetFocus(g_tree); return 0; }
    return CallWindowProc(g_oldEdit, h, m, w, l);
}

/* Строит главное меню на текущем языке. */
static HMENU BuildMenus(void)
{
    HMENU mb, mf, mp, mq, ml;
    int qi;
    mb = CreateMenu();
    mf = CreatePopupMenu();
    AppendMenuW(mf, MF_STRING, IDM_SERVERS, T(L"Серверы...\tF2"));
    AppendMenuW(mf, MF_STRING, IDM_RELOAD, T(L"Обновить библиотеку\tF5"));
    AppendMenuW(mf, MF_STRING, IDM_SETTINGS, T(L"Дополнительно (ini-файл)"));
    AppendMenuW(mf, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mf, MF_STRING, IDM_EXIT, T(L"Выход"));
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mf, T(L"Файл"));
    mp = CreatePopupMenu();
    AppendMenuW(mp, MF_STRING, ID_PLAY, T(L"Пуск / пауза\tПробел"));
    AppendMenuW(mp, MF_STRING, ID_STOP, T(L"Стоп"));
    AppendMenuW(mp, MF_STRING, ID_NEXT, T(L"Следующий трек\tCtrl+Вправо"));
    AppendMenuW(mp, MF_STRING, ID_PREV, T(L"Предыдущий трек\tCtrl+Влево"));
    AppendMenuW(mp, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mp, MF_STRING, IDM_SEEKF, T(L"Перемотка +10 с\tShift+Вправо"));
    AppendMenuW(mp, MF_STRING, IDM_SEEKB, T(L"Перемотка -10 с\tShift+Влево"));
    AppendMenuW(mp, MF_STRING, IDM_VOLUP, T(L"Громче\tCtrl+Вверх"));
    AppendMenuW(mp, MF_STRING, IDM_VOLDN, T(L"Тише\tCtrl+Вниз"));
    AppendMenuW(mp, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mp, MF_STRING, IDM_LIKE, T(L"В избранное / убрать из избранного\tCtrl+L"));
    AppendMenuW(mp, MF_STRING, IDM_SHUF, T(L"Случайный порядок\tCtrl+H"));
    AppendMenuW(mp, MF_STRING, IDM_REP, T(L"Повтор (выкл / список / трек)\tCtrl+R"));
    AppendMenuW(mp, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mp, MF_STRING, IDM_PROF, T(L"Показывать нагрузку на CPU\tCtrl+D"));
    AppendMenuW(mp, MF_STRING, IDM_FINDFOCUS, T(L"Поиск\tCtrl+F"));
    AppendMenuW(mp, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mp, MF_STRING, IDM_PLAYNEXT, T(L"Играть выбранное следующим"));
    AppendMenuW(mp, MF_STRING, IDM_ENQ, T(L"Добавить выбранное в очередь\tIns"));
    AppendMenuW(mp, MF_STRING, IDM_GMEDIA, T(L"Мультимедиа-клавиши во всех окнах"));
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mp, T(L"Управление"));
    g_mCtl = mp;
    mq = CreatePopupMenu();
    AppendMenuW(mq, MF_STRING, IDM_EQ, T(L"Эквалайзер...\tCtrl+E"));
    AppendMenuW(mq, MF_SEPARATOR, 0, NULL);
    AppendMenuW(mq, MF_STRING | MF_GRAYED, 0, T(L"Качество потока (со следующего трека):"));
    for (qi = 0; qi < QN; qi++) AppendMenuW(mq, MF_STRING, IDM_Q_BASE + qi, T(g_qopts[qi].name));
    CheckMenuRadioItem(mq, IDM_Q_BASE, IDM_Q_BASE + QN - 1, IDM_Q_BASE + QualitySel(), MF_BYCOMMAND);
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)mq, T(L"Звук"));
    g_mQ = mq;
    ml = CreatePopupMenu();
    AppendMenuW(ml, MF_STRING, IDM_LANG_RU, L"Русский");
    AppendMenuW(ml, MF_STRING, IDM_LANG_EN, L"English");
    CheckMenuRadioItem(ml, IDM_LANG_RU, IDM_LANG_EN, g_lang ? IDM_LANG_EN : IDM_LANG_RU, MF_BYCOMMAND);
    AppendMenuW(mb, MF_POPUP, (UINT_PTR)ml, L"Язык / Language");
    CheckMenuItem(g_mCtl, IDM_PROF, MF_BYCOMMAND | (g_prof ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(g_mCtl, IDM_GMEDIA, MF_BYCOMMAND | (g_globalMedia ? MF_CHECKED : MF_UNCHECKED));
    SyncModeUi();
    return mb;
}

static void RelabelRoots(void)
{
    HTREEITEM r = TreeView_GetRoot(g_tree);
    while (r) {
        Node *n = GetNode(r);
        const wchar_t *t = NULL;
        if (n && n->type == N_LIBRARY) t = T(L"Библиотека");
        else if (n && n->type == N_STARRED) t = T(L"Избранное");
        else if (n && n->type == N_PLAYLISTS) t = T(L"Плейлисты");
        if (t) {
            TVITEMW it;
            memset(&it, 0, sizeof it);
            it.mask = TVIF_TEXT;
            it.hItem = r;
            it.pszText = (LPWSTR)t;
            SendMessageW(g_tree, TVM_SETITEMW, 0, (LPARAM)&it);
        }
        r = TreeView_GetNextSibling(g_tree, r);
    }
}

static void SetLang(int l)
{
    HMENU old = GetMenu(g_hwnd);
    if (l == g_lang) return;
    g_lang = l;
    if (g_eqWnd) DestroyWindow(g_eqWnd);            /* эти окна создаются заново уже на новом языке */
    if (g_svWnd) DestroyWindow(g_svWnd);
    SetMenu(g_hwnd, BuildMenus());
    if (old) DestroyMenu(old);
    DrawMenuBar(g_hwnd);
    SetWindowTextW(g_bShuf, T(L"Случайно"));
    SetWindowTextW(g_bFind, T(L"Найти"));
    SetWindowTextW(g_bQClr, T(L"Очистить"));
    SetWindowTextW(g_bQUp, T(L"Вверх"));
    SetWindowTextW(g_bQDn, T(L"Вниз"));
    SendMessage(g_search, EM_SETCUEBANNER, 0, (LPARAM)T(L"Поиск (Ctrl+F)"));
    SetVolume((int)SendMessage(g_vol, TBM_GETPOS, 0, 0));
    SyncModeUi();
    SyncLikeUi();
    QRefreshUi();
    RelabelRoots();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_msgTaskbar && msg == g_msgTaskbar) {          /* проводник перезапущен - вернуть значок в трей */
        if (g_inTray) { g_inTray = 0; TrayAdd(); }
        return 0;
    }
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        InitFonts();
        g_bPrev = MkCtl(L"BUTTON", L"Prev", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PREV);
        g_bPlay = MkCtl(L"BUTTON", L"Play/Pause", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PLAY);
        g_bStop = MkCtl(L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_STOP);
        g_bNext = MkCtl(L"BUTTON", L"Next", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_NEXT);
        g_bShuf = MkCtl(L"BUTTON", T(L"Случайно"), BS_CHECKBOX | BS_PUSHLIKE | WS_TABSTOP, 0, ID_SHUF);
        g_bRep = MkCtl(L"BUTTON", T(L"Повтор: выкл"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_REP);
        g_search = MkCtl(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_SEARCH);
        g_bFind = MkCtl(L"BUTTON", T(L"Найти"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_FIND);
        g_title = MkCtl(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0, ID_TITLE);
        g_bLike = MkCtl(L"BUTTON", T(L"В избранное"), BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, 0, ID_LIKE);
        SendMessage(g_title, WM_SETFONT, (WPARAM)g_fontB, TRUE);
        g_info = MkCtl(L"STATIC", L"", SS_LEFT, 0, ID_INFO);
        g_seek = MkCtl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_SEEK);
        g_time = MkCtl(L"STATIC", L"0:00 / 0:00", SS_RIGHT, 0, ID_TIME);
        g_cover = MkCtl(L"STATIC", L"", SS_BITMAP | SS_CENTERIMAGE, WS_EX_CLIENTEDGE, ID_COVER);
        g_volLbl = MkCtl(L"STATIC", T(L"Громкость: 0"), SS_LEFTNOWORDWRAP, 0, ID_VOLLBL);
        g_vol = MkCtl(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_VOL);
        g_tree = MkCtl(WC_TREEVIEWW, L"",
                       WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                       WS_EX_CLIENTEDGE, ID_TREE);
        g_status = MkCtl(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0, ID_STAT);
        g_qhead = MkCtl(L"STATIC", T(L"Очередь: 0"), SS_LEFTNOWORDWRAP, 0, ID_QHEAD);
        SendMessage(g_qhead, WM_SETFONT, (WPARAM)g_fontQB, TRUE);
        g_bQClr = MkCtl(L"BUTTON", T(L"Очистить"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_QCLR);
        g_bQUp = MkCtl(L"BUTTON", T(L"Вверх"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_QUP);
        g_bQDn = MkCtl(L"BUTTON", T(L"Вниз"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_QDN);
        g_qlist = MkCtl(L"LISTBOX", L"", LBS_OWNERDRAWFIXED | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP,
                        WS_EX_CLIENTEDGE, ID_QLIST);
        g_oldQList = (WNDPROC)SetWindowLongPtrW(g_qlist, GWLP_WNDPROC, (LONG_PTR)QListProc);
        QRefreshUi();
        g_oldEdit = (WNDPROC)SetWindowLongPtrW(g_search, GWLP_WNDPROC, (LONG_PTR)EditProc);
        SendMessage(g_search, EM_SETCUEBANNER, 0, (LPARAM)T(L"Поиск (Ctrl+F)"));
        SendMessage(g_vol, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessage(g_seek, TBM_SETRANGE, TRUE, MAKELONG(0, 0));
        SetVolume(g_stVol);
        SyncModeUi();
        SyncLikeUi();
        SetTimer(hwnd, IDT_PLAY, 250, NULL);
        RegMediaKeys(g_globalMedia);
        return 0;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)lp;
        if (mi->CtlType == ODT_LISTBOX && mi->CtlID == ID_QLIST) { mi->itemHeight = (UINT)g_qItemH; return TRUE; }
        break; }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlType == ODT_LISTBOX && di->CtlID == ID_QLIST) { QDrawItem(di); return TRUE; }
        break; }

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (pt.x >= g_splitX && pt.x < g_splitX + 5 && pt.y >= g_splitTop) {
                SetCursor(LoadCursor(NULL, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN:
        if ((short)LOWORD(lp) >= g_splitX && (short)LOWORD(lp) < g_splitX + 5 && (short)HIWORD(lp) >= g_splitTop) {
            g_splitDrag = 1;
            SetCapture(hwnd);
        }
        return 0;

    case WM_MOUSEMOVE:
        if (g_splitDrag) {
            RECT rc;
            int nw;
            GetClientRect(hwnd, &rc);
            nw = rc.right - (short)LOWORD(lp) - 2;
            if (nw > rc.right - 260) nw = rc.right - 260;
            if (nw < 180) nw = 180;
            if (nw != g_qW) {
                g_qW = nw;
                Layout(rc.right, rc.bottom);
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_splitDrag) { g_splitDrag = 0; ReleaseCapture(); }
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)lp;
        mm->ptMinTrackSize.x = 700;
        mm->ptMinTrackSize.y = 460;
        return 0; }

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {                     /* сворачивание - в трей */
            TrayAdd();
            if (g_inTray) ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        Layout(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP: case WM_LBUTTONDBLCLK: TrayRestore(); break;
        case WM_RBUTTONUP: TrayMenu(); break;
        }
        return 0;

    case WM_APP_STAR: {
        StarJob *j = (StarJob *)lp;
        if (wp) {
            Node *n = g_cur ? GetNode(g_cur) : NULL;
            int qi;
            if (n && n->si && strcmp(n->id, j->id) == 0) n->si->starred = j->want;
            if (g_npOk && strcmp(g_np.id, j->id) == 0) g_np.si.starred = j->want;
            for (qi = 0; qi < g_qn; qi++) if (strcmp(g_q[qi].id, j->id) == 0) g_q[qi].si.starred = j->want;
            SyncLikeUi();
            RefreshStarred();
            SetStatus(j->want ? T(L"Добавлено в избранное") : T(L"Убрано из избранного"));
        } else {
            SetStatus(T(L"Не удалось изменить избранное (ошибка сервера)"));
        }
        free(j);
        return 0; }

    case WM_APP_LOAD:
        ReloadLibrary();
        return 0;

    case WM_APP_OPENED:
        OnOpened((LONG)wp);
        return 0;

    case WM_APP_END:
        if ((LONG)wp != g_gen) return 0;
        if ((int)lp == 0) { if (g_repeat == 2 && g_npOk && g_qpos >= 0) PlayQ(g_qpos); else PlayNext(); }
        else {
            StopAll();
            if ((int)lp == 3) SetStatus(T(L"Обрыв загрузки трека"));
            else if ((int)lp == 4) SetStatus(T(L"Не удалось открыть трек (повреждён, неподдерживаемый формат или ошибка сервера)"));
            else SetStatus(T(L"Ошибка вывода звука"));
        }
        return 0;

    case WM_APP_COVER: {
        HBITMAP hb = (HBITMAP)lp;
        if ((LONG)wp != g_coverGen) { if (hb) DeleteObject(hb); return 0; }
        SetCover(hb);
        return 0; }

    case WM_HOTKEY:
        switch ((int)wp) {
        case 9001: OnPlayBtn(); break;
        case 9002: PlayNext(); break;
        case 9003: PlayPrev(); break;
        case 9004: StopAll(); break;
        }
        return 0;

    case WM_APPCOMMAND:
        switch (GET_APPCOMMAND_LPARAM(lp)) {
        case APPCOMMAND_MEDIA_PLAY_PAUSE: OnPlayBtn(); return TRUE;
        case APPCOMMAND_MEDIA_PLAY: if (g_state == ST_PAUSED || g_state == ST_STOPPED) OnPlayBtn(); return TRUE;
        case APPCOMMAND_MEDIA_PAUSE: if (g_state == ST_PLAYING) OnPlayBtn(); return TRUE;
        case APPCOMMAND_MEDIA_NEXTTRACK:  PlayNext(); return TRUE;
        case APPCOMMAND_MEDIA_PREVIOUSTRACK: PlayPrev(); return TRUE;
        case APPCOMMAND_MEDIA_STOP: StopAll(); return TRUE;
        }
        break;

    case WM_TIMER:
        if (wp == IDT_PLAY) OnTimer();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PLAY: OnPlayBtn(); break;
        case ID_STOP: StopAll(); break;
        case ID_NEXT: PlayNext(); break;
        case ID_PREV: PlayPrev(); break;
        case ID_SHUF: case IDM_SHUF: ToggleShuffle(); break;
        case ID_REP: case IDM_REP: CycleRepeat(); break;
        case IDM_EQ: EqOpen(); break;
        case IDM_PROF: ToggleProf(); break;
        case ID_LIKE: case IDM_LIKE: ToggleLike(); break;
        case IDM_TRAY_SHOW: TrayRestore(); break;
        case ID_FIND: RunSearch(); break;
        case IDM_FINDFOCUS: SetFocus(g_search); SendMessage(g_search, EM_SETSEL, 0, -1); break;
        case IDM_VOLUP: SetVolume((int)SendMessage(g_vol, TBM_GETPOS, 0, 0) + 5); break;
        case IDM_VOLDN: SetVolume((int)SendMessage(g_vol, TBM_GETPOS, 0, 0) - 5); break;
        case IDM_SEEKF: SeekRel(10); break;
        case IDM_SEEKB: SeekRel(-10); break;
        case IDM_RELOAD: ReloadLibrary(); break;
        case IDM_PLAYSEL: PlaySelected(); break;
        case IDM_PLAYNEXT: EnqueueSel(1); break;
        case IDM_ENQ: EnqueueSel(0); break;
        case ID_QCLR: QClear(); break;
        case ID_QUP: QMove(-1); break;
        case ID_QDN: QMove(1); break;
        case IDM_QDEL: QRemoveSel(); break;
        case IDM_QPLAY: QPlaySel(); break;
        case ID_QLIST:
            if (HIWORD(wp) == LBN_DBLCLK) QPlaySel();
            break;
        case IDM_SERVERS: SrvOpen(); break;
        case IDM_LANG_RU: SetLang(0); break;
        case IDM_LANG_EN: SetLang(1); break;
        case IDM_GMEDIA:
            g_globalMedia = !g_globalMedia;
            RegMediaKeys(g_globalMedia);
            SetStatus(g_globalMedia ? T(L"Мультимедиа-клавиши работают во всех окнах") : T(L"Мультимедиа-клавиши работают только когда плеер активен"));
            break;
        case IDM_SETTINGS: {
            wchar_t arg[MAX_PATH + 4];
            if (GetFileAttributesW(g_ini) == INVALID_FILE_ATTRIBUTES) WriteDefaultIni();
            wsprintfW(arg, L"\"%s\"", g_ini);
            ShellExecuteW(hwnd, L"open", L"notepad.exe", arg, NULL, SW_SHOW);
            SetStatus(T(L"Дополнительные настройки: после правки сохраните файл и нажмите F5"));
            break; }
        case IDM_EXIT: SendMessage(hwnd, WM_CLOSE, 0, 0); break;
        default:
            if (LOWORD(wp) >= IDM_Q_BASE && LOWORD(wp) < IDM_Q_BASE + QN) SetQuality(LOWORD(wp) - IDM_Q_BASE);
            break;
        }
        return 0;

    case WM_HSCROLL:
        if ((HWND)lp == g_seek) {
            int code = LOWORD(wp);
            if (code == TB_THUMBTRACK) g_drag = 1;
            else if (code == TB_ENDTRACK) {
                int pos = (int)SendMessage(g_seek, TBM_GETPOS, 0, 0);
                g_drag = 0;
                DoSeek(pos);
            }
        } else if ((HWND)lp == g_vol) {
            SetVolume((int)SendMessage(g_vol, TBM_GETPOS, 0, 0));
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)lp;
        if (nh->hwndFrom != g_tree) break;
        switch (nh->code) {
        case TVN_ITEMEXPANDING: {
            NMTREEVIEW *nm = (NMTREEVIEW *)lp;
            if (nm->action == TVE_EXPAND) {
                Node *n = (Node *)nm->itemNew.lParam;
                if (n && n->loaded == 2 && !IsUnder(g_cur, nm->itemNew.hItem)) {   /* "Избранное" устарело */
                    DeleteChildren(nm->itemNew.hItem);
                    n->loaded = 0;
                }
                if (n && !n->loaded) {
                    n->loaded = 1;
                    if (!LoadChildren(nm->itemNew.hItem, n)) { n->loaded = 0; return TRUE; }
                }
            }
            return FALSE; }
        case TVN_DELETEITEM: {
            NMTREEVIEW *nm = (NMTREEVIEW *)lp;
            {
                int q;
                HTREEITEM dh = nm->itemOld.hItem;
                if (dh == g_cur) g_cur = NULL;
                if (dh == g_qOrigin) g_qOrigin = NULL;
                if (g_np.h == dh) g_np.h = NULL;
                for (q = 0; q < g_qn; q++) if (g_q[q].h == dh) g_q[q].h = NULL;
            }
            if (nm->itemOld.hItem == g_searchItem) g_searchItem = NULL;
            FreeNode((Node *)nm->itemOld.lParam);
            return 0; }
        case NM_DBLCLK: {
            HTREEITEM s = TreeView_GetSelection(g_tree);
            Node *n = GetNode(s);
            if (n && n->type == N_SONG) { PlayTree(s); return 1; }
            return 0; }
        case NM_RCLICK: {
            TVHITTESTINFO ht;
            HTREEITEM h;
            POINT pt;
            GetCursorPos(&pt);
            ht.pt = pt;
            ScreenToClient(g_tree, &ht.pt);
            h = TreeView_HitTest(g_tree, &ht);
            if (h) {
                HMENU pm = CreatePopupMenu();
                Node *rn;
                TreeView_SelectItem(g_tree, h);
                rn = GetNode(h);
                if (pm && Playable(rn)) {
                    AppendMenuW(pm, MF_STRING, IDM_PLAYSEL, T(L"Воспроизвести"));
                    AppendMenuW(pm, MF_STRING, IDM_PLAYNEXT, T(L"Играть следующим"));
                    AppendMenuW(pm, MF_STRING, IDM_ENQ, T(L"Добавить в очередь\tIns"));
                    SetMenuDefaultItem(pm, IDM_PLAYSEL, FALSE);
                    SetForegroundWindow(hwnd);
                    TrackPopupMenu(pm, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                    PostMessage(hwnd, WM_NULL, 0, 0);
                }
                if (pm) DestroyMenu(pm);
            }
            return 0; }
        case TVN_KEYDOWN: {
            NMTVKEYDOWN *kd = (NMTVKEYDOWN *)lp;
            if (kd->wVKey == VK_RETURN) {
                HTREEITEM s = TreeView_GetSelection(g_tree);
                Node *n = GetNode(s);
                if (n && n->type == N_SONG) PlayTree(s);
                else if (s) TreeView_Expand(g_tree, s, TVE_TOGGLE);
            } else if (kd->wVKey == VK_INSERT) {
                EnqueueSel(0);
            }
            return 0; }
        }
        break; }

    case WM_CLOSE:
        SaveState(hwnd);
        if (g_eqWnd) DestroyWindow(g_eqWnd);
        if (g_svWnd) DestroyWindow(g_svWnd);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, IDT_PLAY);
        RegMediaKeys(0);
        TrayRemove();
        InterlockedIncrement(&g_gen);
        StopPlayer();
        free(g_q);
        g_q = NULL;
        SetCover(NULL);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int StateRectOnScreen(int x, int y, int w, int h)
{
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) { vx = 0; vy = 0; vw = GetSystemMetrics(SM_CXSCREEN); vh = GetSystemMetrics(SM_CYSCREEN); }
    return x < vx + vw - 60 && y < vy + vh - 60 && x + w > vx + 60 && y + h > vy + 60;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX icc;
    HWND hwnd;
    HMENU mb;
    HACCEL acc;
    ACCEL ak[15];
    MSG m;
    wchar_t *dot;
    DWORD to;
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = 820, h = 640;
    (void)hp; (void)cmd;

    {
        LARGE_INTEGER fq;
        if (!QueryPerformanceFrequency(&fq) || fq.QuadPart <= 0) fq.QuadPart = 1000000;
        g_qpcFreq = fq.QuadPart;
    }
    srand((unsigned)GetTickCount());
    GetModuleFileNameW(NULL, g_ini, MAX_PATH);
    lstrcpynW(g_sav, g_ini, MAX_PATH);
    dot = wcsrchr(g_ini, L'.');
    if (dot) lstrcpyW(dot, L".ini"); else lstrcatW(g_ini, L".ini");
    dot = wcsrchr(g_sav, L'.');
    if (dot) lstrcpyW(dot, L".sav"); else lstrcatW(g_sav, L".sav");

    g_lang = (PRIMARYLANGID(GetUserDefaultLangID()) == LANG_RUSSIAN) ? 0 : 1;   /* по умолчанию - язык системы */
    LoadState();
    CleanupTemp();
    SetProcessWorkingSetSize(GetCurrentProcess(), 16u * 1024 * 1024, 64u * 1024 * 1024);   /* чтобы XP не выгружала рабочий набор плеера в своп */
    if (g_stW >= 560 && g_stH >= 420 && StateRectOnScreen(g_stX, g_stY, g_stW, g_stH)) {
        x = g_stX; y = g_stY; w = g_stW; h = g_stH;
    }

    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitialize(NULL);
    InitGdip();

    g_inet = InternetOpenA("nav-xp/4.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    to = 5000;  InternetSetOptionA(g_inet, INTERNET_OPTION_CONNECT_TIMEOUT, &to, sizeof to);
    to = 30000; InternetSetOptionA(g_inet, INTERNET_OPTION_RECEIVE_TIMEOUT, &to, sizeof to);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    g_icBig = MakeAppIcon(32);
    g_icSmall = MakeAppIcon(16);
    wc.hIcon = g_icBig ? g_icBig : LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = g_icSmall ? g_icSmall : wc.hIcon;
    g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"NDClientXPWnd";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = EqWndProc;
    wc.lpszClassName = L"NDEqWnd";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = SvWndProc;
    wc.lpszClassName = L"NDSrvWnd";
    RegisterClassExW(&wc);
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = L"NDClientXPWnd";

    mb = BuildMenus();

    hwnd = CreateWindowExW(0, L"NDClientXPWnd", L"nav-xp", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           x, y, w, h, NULL, mb, hi, NULL);
    if (!hwnd) return 1;

    ak[0].fVirt = FVIRTKEY;            ak[0].key = VK_F5;     ak[0].cmd = IDM_RELOAD;
    ak[1].fVirt = FVIRTKEY | FCONTROL; ak[1].key = 'F';       ak[1].cmd = IDM_FINDFOCUS;
    ak[2].fVirt = FVIRTKEY;            ak[2].key = VK_SPACE;  ak[2].cmd = ID_PLAY;
    ak[3].fVirt = FVIRTKEY | FCONTROL; ak[3].key = VK_RIGHT;  ak[3].cmd = ID_NEXT;
    ak[4].fVirt = FVIRTKEY | FCONTROL; ak[4].key = VK_LEFT;   ak[4].cmd = ID_PREV;
    ak[5].fVirt = FVIRTKEY | FCONTROL; ak[5].key = VK_UP;     ak[5].cmd = IDM_VOLUP;
    ak[6].fVirt = FVIRTKEY | FCONTROL; ak[6].key = VK_DOWN;   ak[6].cmd = IDM_VOLDN;
    ak[7].fVirt = FVIRTKEY | FSHIFT;   ak[7].key = VK_RIGHT;  ak[7].cmd = IDM_SEEKF;
    ak[8].fVirt = FVIRTKEY | FSHIFT;   ak[8].key = VK_LEFT;   ak[8].cmd = IDM_SEEKB;
    ak[9].fVirt = FVIRTKEY | FCONTROL;  ak[9].key = 'H';      ak[9].cmd = IDM_SHUF;
    ak[10].fVirt = FVIRTKEY | FCONTROL; ak[10].key = 'R';      ak[10].cmd = IDM_REP;
    ak[11].fVirt = FVIRTKEY | FCONTROL; ak[11].key = 'E';      ak[11].cmd = IDM_EQ;
    ak[12].fVirt = FVIRTKEY | FCONTROL; ak[12].key = 'D';      ak[12].cmd = IDM_PROF;
    ak[13].fVirt = FVIRTKEY | FCONTROL; ak[13].key = 'L';      ak[13].cmd = IDM_LIKE;
    ak[14].fVirt = FVIRTKEY;            ak[14].key = VK_F2;    ak[14].cmd = IDM_SERVERS;
    acc = CreateAcceleratorTable(ak, 15);

    ShowWindow(hwnd, g_stMax ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hwnd);
    PostMessage(hwnd, WM_APP_LOAD, 0, 0);

    while (GetMessage(&m, NULL, 0, 0) > 0) {
        if (g_eqWnd && IsWindowVisible(g_eqWnd) && IsDialogMessage(g_eqWnd, &m)) continue;
        if (g_svWnd && IsDialogMessage(g_svWnd, &m)) continue;
        /* пока фокус в строке поиска, горячие клавиши не перехватываем */
        if (GetFocus() != g_search && TranslateAccelerator(hwnd, acc, &m)) continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    ShutdownGdip();
    CoUninitialize();
    InternetCloseHandle(g_inet);
    return (int)m.wParam;
}
