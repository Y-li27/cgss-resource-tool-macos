#ifndef CGSS_COMPAT_WINDOWS_H
#define CGSS_COMPAT_WINDOWS_H

/* Minimal Win32 shim so the original CGSS sources can compile on macOS/Linux. */
#ifndef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>


#ifndef _O_BINARY
#define _O_BINARY 0
#define _O_TEXT 0
#endif
#ifndef _fileno
#define _fileno fileno
#endif
#ifndef _setmode
#define _setmode(fd, mode) ((void)(fd), (void)(mode), 0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned int DWORD;
typedef unsigned int UINT;
typedef int INT;
typedef long LONG;
typedef unsigned long ULONG;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t DWORD_PTR;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
typedef void *HANDLE;
typedef void *HINSTANCE;
typedef void *HMODULE;
typedef void *HWND;
typedef void *HDC;
typedef wchar_t WCHAR;
typedef WCHAR *LPWSTR;
typedef const WCHAR *LPCWSTR;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef DWORD *LPDWORD;
typedef unsigned int UINT32;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define CONST const
#define WINAPI
#define CALLBACK
#define MAX_PATH 260
#define CP_UTF8 65001
#define CP_ACP 0

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFF)
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010
#define FILE_ATTRIBUTE_NORMAL    0x00000080
#define GENERIC_WRITE 0x40000000
#define GENERIC_READ  0x80000000
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define ERROR_ALREADY_EXISTS 183
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_SUCCESS 0
#define SW_SHOWNORMAL 1
#define INFINITE 0xFFFFFFFF
#define MOVEFILE_REPLACE_EXISTING 0x00000001
#define FILE_SHARE_READ 0x00000001
#define FILE_SHARE_WRITE 0x00000002

#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004

typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _SECURITY_ATTRIBUTES {
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct _WIN32_FIND_DATAW {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    WCHAR cFileName[MAX_PATH];
    WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef struct _WIN32_FIND_DATAA {
    DWORD dwFileAttributes;
    FILETIME ftCreationTime;
    FILETIME ftLastAccessTime;
    FILETIME ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    char cFileName[MAX_PATH];
    char cAlternateFileName[14];
} WIN32_FIND_DATAA;

typedef struct _STARTUPINFOW {
    DWORD cb;
    LPWSTR lpReserved;
    LPWSTR lpDesktop;
    LPWSTR lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize;
    DWORD dwXCountChars, dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
    WORD cbReserved2;
    BYTE *lpReserved2;
    HANDLE hStdInput, hStdOutput, hStdError;
} STARTUPINFOW;

typedef struct _PROCESS_INFORMATION {
    HANDLE hProcess;
    HANDLE hThread;
    DWORD dwProcessId;
    DWORD dwThreadId;
} PROCESS_INFORMATION;

typedef short SHORT;

typedef struct _CONSOLE_SCREEN_BUFFER_INFO {
    struct { SHORT X; SHORT Y; } dwSize;
    struct { SHORT X; SHORT Y; } dwCursorPosition;
    WORD wAttributes;
    struct { SHORT Left; SHORT Top; SHORT Right; SHORT Bottom; } srWindow;
    struct { SHORT X; SHORT Y; } dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO;


int MultiByteToWideChar(UINT cp, DWORD flags, const char *src, int slen, wchar_t *dst, int dlen);
int WideCharToMultiByte(UINT cp, DWORD flags, const wchar_t *src, int slen, char *dst, int dlen,
                        const char *defchar, BOOL *used);
DWORD GetModuleFileNameW(HMODULE mod, wchar_t *buf, DWORD n);
BOOL CreateDirectoryW(LPCWSTR path, SECURITY_ATTRIBUTES *sa);
BOOL RemoveDirectoryW(LPCWSTR path);
BOOL DeleteFileW(LPCWSTR path);
BOOL CopyFileW(LPCWSTR src, LPCWSTR dst, BOOL fail_if_exists);
BOOL MoveFileW(LPCWSTR src, LPCWSTR dst);
BOOL MoveFileExW(LPCWSTR src, LPCWSTR dst, DWORD flags);
DWORD GetFileAttributesW(LPCWSTR path);
DWORD GetFileAttributesA(LPCSTR path);
HANDLE FindFirstFileW(LPCWSTR pattern, WIN32_FIND_DATAW *fd);
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd);
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd);
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd);
BOOL FindClose(HANDLE h);
HANDLE CreateFileW(LPCWSTR path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *sa,
                   DWORD disp, DWORD flags, HANDLE tmpl);
BOOL WriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, void *ovl);
BOOL CloseHandle(HANDLE h);
DWORD GetLastError(void);
void SetLastError(DWORD e);
BOOL CreateProcessW(LPCWSTR app, LPWSTR cmdline, SECURITY_ATTRIBUTES *pa, SECURITY_ATTRIBUTES *ta,
                    BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd,
                    STARTUPINFOW *si, PROCESS_INFORMATION *pi);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
BOOL GetExitCodeProcess(HANDLE h, DWORD *code);
HINSTANCE ShellExecuteW(HWND hwnd, LPCWSTR op, LPCWSTR file, LPCWSTR params, LPCWSTR dir, INT show);
UINT GetConsoleCP(void);
UINT GetConsoleOutputCP(void);
UINT GetACP(void);
BOOL SetConsoleCP(UINT cp);
BOOL SetConsoleOutputCP(UINT cp);
HANDLE GetStdHandle(DWORD id);
BOOL GetConsoleMode(HANDLE h, DWORD *mode);
BOOL SetConsoleMode(HANDLE h, DWORD mode);
BOOL GetConsoleScreenBufferInfo(HANDLE h, CONSOLE_SCREEN_BUFFER_INFO *info);
void Sleep(DWORD ms);
DWORD GetCurrentProcessId(void);

FILE *_wfopen(const wchar_t *path, const wchar_t *mode);
int _wremove(const wchar_t *path);
int _wcsicmp(const wchar_t *a, const wchar_t *b);
int _stricmp(const char *a, const char *b);

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifdef __cplusplus
}
#endif

#endif /* !_WIN32 */
#endif /* CGSS_COMPAT_WINDOWS_H */
