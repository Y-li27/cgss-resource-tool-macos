#ifndef CGSS_COMPAT_WINHTTP_H
#define CGSS_COMPAT_WINHTTP_H

#include "windows.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef HANDLE HINTERNET;

#define WINHTTP_ACCESS_TYPE_NO_PROXY 1
#define WINHTTP_NO_PROXY_NAME NULL
#define WINHTTP_NO_PROXY_BYPASS NULL
#define WINHTTP_NO_REFERER NULL
#define WINHTTP_DEFAULT_ACCEPT_TYPES NULL
#define WINHTTP_NO_ADDITIONAL_HEADERS NULL
#define WINHTTP_NO_REQUEST_DATA NULL
#define WINHTTP_FLAG_SECURE 0x00800000
#define WINHTTP_ADDREQ_FLAG_ADD 0x20000000
#define WINHTTP_ADDREQ_FLAG_REPLACE 0x80000000
#define WINHTTP_QUERY_STATUS_CODE 19
#define WINHTTP_QUERY_FLAG_NUMBER 0x20000000
#define WINHTTP_HEADER_NAME_BY_INDEX NULL
#define WINHTTP_NO_HEADER_INDEX NULL
#define INTERNET_DEFAULT_HTTPS_PORT 443
#define INTERNET_DEFAULT_HTTP_PORT 80

HINTERNET WinHttpOpen(LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass, DWORD flags);
BOOL WinHttpSetTimeouts(HINTERNET h, int resolve, int connect, int send, int recv);
HINTERNET WinHttpConnect(HINTERNET sess, LPCWSTR server, UINT port, DWORD reserved);
HINTERNET WinHttpOpenRequest(HINTERNET conn, LPCWSTR verb, LPCWSTR path, LPCWSTR ver,
                             LPCWSTR referer, LPCWSTR *accept, DWORD flags);
BOOL WinHttpAddRequestHeaders(HINTERNET req, LPCWSTR headers, DWORD len, DWORD modifiers);
BOOL WinHttpSendRequest(HINTERNET req, LPCWSTR headers, DWORD hlen, LPVOID optional,
                        DWORD olen, DWORD total, DWORD_PTR ctx);
BOOL WinHttpReceiveResponse(HINTERNET req, LPVOID reserved);
BOOL WinHttpQueryHeaders(HINTERNET req, DWORD info, LPCWSTR name, LPVOID buf, LPDWORD buflen, LPDWORD index);
BOOL WinHttpQueryDataAvailable(HINTERNET req, LPDWORD available);
BOOL WinHttpReadData(HINTERNET req, LPVOID buf, DWORD size, LPDWORD read);
BOOL WinHttpCloseHandle(HINTERNET h);

#ifdef __cplusplus
}
#endif

#endif
