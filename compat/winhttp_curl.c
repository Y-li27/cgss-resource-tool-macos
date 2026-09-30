#include "winhttp.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { H_SESS = 1, H_CONN = 2, H_REQ = 3 } HKind;

typedef struct {
    HKind kind;
    char host[256];
    int port;
    int timeout_ms;
    char extra_headers[1024];
} HttpSess;

typedef struct {
    HKind kind;
    HttpSess *sess;
    char host[256];
    int port;
} HttpConn;

typedef struct {
    HKind kind;
    HttpConn *conn;
    char verb[16];
    char path[1024];
    char extra_headers[2048];
    int secure;
    unsigned char *body;
    size_t body_len;
    size_t body_pos;
    long status;
    int received;
} HttpReq;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata){
    HttpReq *r = (HttpReq*)userdata;
    size_t n = size * nmemb;
    unsigned char *nb = (unsigned char*)realloc(r->body, r->body_len + n);
    if (!nb) return 0;
    r->body = nb;
    memcpy(r->body + r->body_len, ptr, n);
    r->body_len += n;
    return n;
}

HINTERNET WinHttpOpen(LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass, DWORD flags){
    (void)agent; (void)access; (void)proxy; (void)bypass; (void)flags;
    static int inited = 0;
    if (!inited){ curl_global_init(CURL_GLOBAL_DEFAULT); inited = 1; }
    HttpSess *s = (HttpSess*)calloc(1, sizeof *s);
    s->kind = H_SESS;
    s->timeout_ms = 60000;
    return (HINTERNET)s;
}

BOOL WinHttpSetTimeouts(HINTERNET h, int resolve, int connect, int send, int recv){
    (void)resolve; (void)connect; (void)send;
    if (!h) return FALSE;
    ((HttpSess*)h)->timeout_ms = recv > 0 ? recv : 60000;
    return TRUE;
}

HINTERNET WinHttpConnect(HINTERNET sess, LPCWSTR server, UINT port, DWORD reserved){
    (void)reserved;
    if (!sess || !server) return NULL;
    HttpConn *c = (HttpConn*)calloc(1, sizeof *c);
    c->kind = H_CONN;
    c->sess = (HttpSess*)sess;
    WideCharToMultiByte(CP_UTF8, 0, server, -1, c->host, sizeof c->host, NULL, NULL);
    c->port = (int)port;
    return (HINTERNET)c;
}

HINTERNET WinHttpOpenRequest(HINTERNET conn, LPCWSTR verb, LPCWSTR path, LPCWSTR ver,
                             LPCWSTR referer, LPCWSTR *accept, DWORD flags){
    (void)ver; (void)referer; (void)accept;
    if (!conn) return NULL;
    HttpReq *r = (HttpReq*)calloc(1, sizeof *r);
    r->kind = H_REQ;
    r->conn = (HttpConn*)conn;
    if (verb) WideCharToMultiByte(CP_UTF8, 0, verb, -1, r->verb, sizeof r->verb, NULL, NULL);
    else snprintf(r->verb, sizeof r->verb, "GET");
    if (path) WideCharToMultiByte(CP_UTF8, 0, path, -1, r->path, sizeof r->path, NULL, NULL);
    r->secure = (flags & WINHTTP_FLAG_SECURE) ? 1 : 0;
    return (HINTERNET)r;
}

BOOL WinHttpAddRequestHeaders(HINTERNET req, LPCWSTR headers, DWORD len, DWORD modifiers){
    (void)len; (void)modifiers;
    if (!req || !headers) return FALSE;
    HttpReq *r = (HttpReq*)req;
    char tmp[1024];
    WideCharToMultiByte(CP_UTF8, 0, headers, -1, tmp, sizeof tmp, NULL, NULL);
    size_t used = strlen(r->extra_headers);
    snprintf(r->extra_headers + used, sizeof r->extra_headers - used, "%s", tmp);
    return TRUE;
}

BOOL WinHttpSendRequest(HINTERNET req, LPCWSTR headers, DWORD hlen, LPVOID optional,
                        DWORD olen, DWORD total, DWORD_PTR ctx){
    (void)hlen; (void)optional; (void)olen; (void)total; (void)ctx;
    if (!req) return FALSE;
    HttpReq *r = (HttpReq*)req;
    if (headers) WinHttpAddRequestHeaders(req, headers, (DWORD)-1, 0);

    char url[1600];
    const char *scheme = r->secure ? "https" : "http";
    snprintf(url, sizeof url, "%s://%s:%d%s", scheme, r->conn->host, r->conn->port, r->path);

    CURL *curl = curl_easy_init();
    if (!curl) return FALSE;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
    int to = r->conn->sess ? r->conn->sess->timeout_ms / 1000 : 60;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)to);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "UnityPlayer/2022.3.56f1 (UnityWebRequest/1.0, libcurl/8.10.1-DEV)");

    struct curl_slist *hdrs = NULL;
    hdrs = curl_slist_append(hdrs, "X-Unity-Version: 2022.3.56f1");
    if (r->extra_headers[0]){
        char *save = NULL;
        char *line = strtok_r(r->extra_headers, "\r\n", &save);
        while (line){
            if (line[0]) hdrs = curl_slist_append(hdrs, line);
            line = strtok_r(NULL, "\r\n", &save);
        }
    }
    if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r->status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    r->received = (rc == CURLE_OK);
    r->body_pos = 0;
    if (rc != CURLE_OK){
        fprintf(stderr, "通信失敗: %s\n%s\n", curl_easy_strerror(rc), url);
        SetLastError(1);
    }
    return r->received ? TRUE : FALSE;
}

BOOL WinHttpReceiveResponse(HINTERNET req, LPVOID reserved){
    (void)reserved;
    if (!req) return FALSE;
    return ((HttpReq*)req)->received ? TRUE : FALSE;
}

BOOL WinHttpQueryHeaders(HINTERNET req, DWORD info, LPCWSTR name, LPVOID buf, LPDWORD buflen, LPDWORD index){
    (void)name; (void)index;
    if (!req || !buf || !buflen) return FALSE;
    HttpReq *r = (HttpReq*)req;
    if (info & WINHTTP_QUERY_FLAG_NUMBER){
        *(DWORD*)buf = (DWORD)r->status;
        *buflen = sizeof(DWORD);
        return TRUE;
    }
    return FALSE;
}

BOOL WinHttpQueryDataAvailable(HINTERNET req, LPDWORD available){
    if (!req || !available) return FALSE;
    HttpReq *r = (HttpReq*)req;
    size_t left = (r->body_len > r->body_pos) ? (r->body_len - r->body_pos) : 0;
    *available = (DWORD)(left > 0x20000 ? 0x20000 : left);
    return TRUE;
}

BOOL WinHttpReadData(HINTERNET req, LPVOID buf, DWORD size, LPDWORD read){
    if (!req || !buf) return FALSE;
    HttpReq *r = (HttpReq*)req;
    size_t left = (r->body_len > r->body_pos) ? (r->body_len - r->body_pos) : 0;
    size_t n = left < size ? left : size;
    if (n) memcpy(buf, r->body + r->body_pos, n);
    r->body_pos += n;
    if (read) *read = (DWORD)n;
    return TRUE;
}

BOOL WinHttpCloseHandle(HINTERNET h){
    if (!h) return FALSE;
    HKind k = *(HKind*)h;
    if (k == H_REQ){
        HttpReq *r = (HttpReq*)h;
        free(r->body);
        free(r);
    } else if (k == H_CONN){
        free(h);
    } else if (k == H_SESS){
        free(h);
    } else {
        free(h);
    }
    return TRUE;
}
