// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * dnssd.dll for uxplay.exe without Apple Bonjour.
 *
 * UxPlay loads "dnssd.dll" at run time (LoadLibraryA) and uses seven calls of
 * Apple's DNS-SD client API: DNSServiceRegister, DNSServiceRefDeallocate and
 * the TXTRecord* helpers. This DLL provides exactly those, on top of the DNS-SD
 * API built into Windows 10 and later (DnsServiceRegister in dnsapi.dll).
 * Placed next to uxplay.exe it is found before Bonjour's System32 copy.
 *
 * Mode (environment variable KAGAMICAST_DNSSD):
 *   unset / "auto"  Windows API if dnsapi.dll has it, else Bonjour if installed
 *   "native"        Windows API only
 *   "bonjour"       forward registrations to Bonjour's System32\dnssd.dll
 * The TXTRecord* helpers are always this DLL's own; they build the same bytes
 * as Bonjour's (same layout, same replace-on-set behaviour).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define DNSSD_API __stdcall /* no effect on x64; matches Apple's dns_sd.h */

/* ---- Apple DNS-SD types (the subset UxPlay uses) ---------------------- */

typedef uint32_t DNSServiceFlags;
typedef int32_t DNSServiceErrorType;
typedef struct _DNSServiceRef_t *DNSServiceRef;
typedef union _TXTRecordRef_t {
    char PrivateData[16];
    char *ForceNaturalAlignment;
} TXTRecordRef;
typedef void(DNSSD_API *DNSServiceRegisterReply)(DNSServiceRef sdRef, DNSServiceFlags flags,
                                                 DNSServiceErrorType errorCode, const char *name,
                                                 const char *regtype, const char *domain,
                                                 void *context);

enum {
    kDNSServiceErr_NoError = 0,
    kDNSServiceErr_Unknown = -65537,
    kDNSServiceErr_NoMemory = -65539,
    kDNSServiceErr_BadParam = -65540,
    kDNSServiceErr_BadReference = -65541,
    kDNSServiceErr_Unsupported = -65544,
    kDNSServiceErr_NameConflict = -65548,
    kDNSServiceErr_Invalid = -65549,
    kDNSServiceErr_ServiceNotRunning = -65563,
};
#define kDNSServiceFlagsAdd 0x2

/* ---- Windows DNS-SD types (windns.h, Windows SDK 10.0.26100) ----------
 * Declared here so the build does not depend on how complete MinGW's
 * windns.h is. Layouts copied from the SDK header. */

typedef struct {
    LPWSTR pszInstanceName;
    LPWSTR pszHostName;
    DWORD *ip4Address;
    void *ip6Address;
    WORD wPort;
    WORD wPriority;
    WORD wWeight;
    DWORD dwPropertyCount;
    PWSTR *keys;
    PWSTR *values;
    DWORD dwInterfaceIndex;
} WinServiceInstance;

typedef struct {
    PVOID reserved;
} WinServiceCancel;

typedef VOID(WINAPI *WinRegisterComplete)(DWORD Status, PVOID pQueryContext,
                                          WinServiceInstance *pInstance);

typedef struct {
    ULONG Version;
    ULONG InterfaceIndex;
    WinServiceInstance *pServiceInstance;
    WinRegisterComplete pRegisterCompletionCallback;
    PVOID pQueryContext;
    HANDLE hCredentials;
    BOOL unicastEnabled;
} WinRegisterRequest;

#define WIN_DNS_QUERY_REQUEST_VERSION1 1
#define WIN_DNS_REQUEST_PENDING 9506

typedef WinServiceInstance *(WINAPI *pfnConstructInstance)(PCWSTR, PCWSTR, DWORD *, void *, WORD,
                                                           WORD, WORD, DWORD, PCWSTR *, PCWSTR *);
typedef VOID(WINAPI *pfnFreeInstance)(WinServiceInstance *);
typedef DWORD(WINAPI *pfnRegister)(WinRegisterRequest *, WinServiceCancel *);
typedef DWORD(WINAPI *pfnDeRegister)(WinRegisterRequest *, WinServiceCancel *);
typedef DWORD(WINAPI *pfnRegisterCancel)(WinServiceCancel *);

/* Bonjour's own entry points, for the forwarding mode. */
typedef DNSServiceErrorType(DNSSD_API *pfnBjRegister)(DNSServiceRef *, DNSServiceFlags, uint32_t,
                                                      const char *, const char *, const char *,
                                                      const char *, uint16_t, uint16_t,
                                                      const void *, DNSServiceRegisterReply,
                                                      void *);
typedef void(DNSSD_API *pfnBjDeallocate)(DNSServiceRef);

/* How long DNSServiceRegister waits for Windows to confirm (it probes the
 * name first), and how long DNSServiceRefDeallocate waits for withdrawal. */
#define REGISTER_WAIT_MS 5000
#define DEREGISTER_WAIT_MS 2000

enum { MODE_NONE, MODE_NATIVE, MODE_BONJOUR };

static INIT_ONCE g_init = INIT_ONCE_STATIC_INIT;
static int g_mode = MODE_NONE;
static pfnConstructInstance p_construct;
static pfnFreeInstance p_free;
static pfnRegister p_register;
static pfnDeRegister p_deregister;
static pfnRegisterCancel p_cancel;
static pfnBjRegister bj_register;
static pfnBjDeallocate bj_deallocate;

static void shim_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("dnssd-shim: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
}

static int load_native(void)
{
    HMODULE m = LoadLibraryExW(L"dnsapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m)
        return 0;
    p_construct = (pfnConstructInstance)(void *)GetProcAddress(m, "DnsServiceConstructInstance");
    p_free = (pfnFreeInstance)(void *)GetProcAddress(m, "DnsServiceFreeInstance");
    p_register = (pfnRegister)(void *)GetProcAddress(m, "DnsServiceRegister");
    p_deregister = (pfnDeRegister)(void *)GetProcAddress(m, "DnsServiceDeRegister");
    p_cancel = (pfnRegisterCancel)(void *)GetProcAddress(m, "DnsServiceRegisterCancel");
    return p_construct && p_free && p_register && p_deregister;
}

static int load_bonjour(void)
{
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + 11 >= MAX_PATH)
        return 0;
    wcscat(path, L"\\dnssd.dll");
    HMODULE m = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m)
        return 0;
    bj_register = (pfnBjRegister)(void *)GetProcAddress(m, "DNSServiceRegister");
    bj_deallocate = (pfnBjDeallocate)(void *)GetProcAddress(m, "DNSServiceRefDeallocate");
    return bj_register && bj_deallocate;
}

static BOOL CALLBACK init_once(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    (void)once;
    (void)param;
    (void)ctx;

    /* Windows may call our completion callbacks after uxplay has called
     * FreeLibrary on us; keep the code mapped for the life of the process. */
    HMODULE self;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                       (LPCWSTR)(void *)&init_once, &self);

    char want[16] = "";
    GetEnvironmentVariableA("KAGAMICAST_DNSSD", want, sizeof(want));

    if (_stricmp(want, "bonjour") == 0) {
        if (load_bonjour())
            g_mode = MODE_BONJOUR;
    } else if (load_native()) {
        g_mode = MODE_NATIVE;
    } else if (_stricmp(want, "native") != 0 && load_bonjour()) {
        g_mode = MODE_BONJOUR;
    }

    shim_log("mode %s", g_mode == MODE_NATIVE    ? "windows"
                        : g_mode == MODE_BONJOUR ? "bonjour"
                                                 : "none (no DNS-SD available)");
    return TRUE;
}

static int mode(void)
{
    InitOnceExecuteOnce(&g_init, init_once, NULL, NULL);
    return g_mode;
}

/* ---- TXT records (same bytes as Apple's dnssd_clientlib.c) ------------- */

typedef struct {
    uint8_t *buffer;
    uint16_t buflen;
    uint16_t datalen;
    uint8_t malloced;
} TxtRec;

_Static_assert(sizeof(TxtRec) <= sizeof(TXTRecordRef), "TXTRecordRef is too small");

static uint8_t *txt_find(const TxtRec *t, const char *key)
{
    size_t keylen = strlen(key);
    uint8_t *p = t->buffer;
    uint8_t *e = t->buffer + t->datalen;
    while (p && p < e) {
        uint8_t *x = p;
        p += 1 + p[0];
        if (p <= e && keylen <= x[0] && _strnicmp(key, (const char *)x + 1, keylen) == 0 &&
            (keylen == x[0] || x[1 + keylen] == '='))
            return x;
    }
    return NULL;
}

void DNSSD_API TXTRecordCreate(TXTRecordRef *txtRecord, uint16_t bufferLen, void *buffer)
{
    TxtRec *t = (TxtRec *)txtRecord;
    t->buffer = (uint8_t *)buffer;
    t->buflen = buffer ? bufferLen : 0;
    t->datalen = 0;
    t->malloced = 0;
}

void DNSSD_API TXTRecordDeallocate(TXTRecordRef *txtRecord)
{
    TxtRec *t = (TxtRec *)txtRecord;
    if (t->malloced)
        free(t->buffer);
}

DNSServiceErrorType DNSSD_API TXTRecordSetValue(TXTRecordRef *txtRecord, const char *key,
                                                uint8_t valueSize, const void *value)
{
    TxtRec *t = (TxtRec *)txtRecord;
    const char *k;
    for (k = key; *k; k++)
        if (*k < 0x20 || *k > 0x7E || *k == '=')
            return kDNSServiceErr_Invalid;
    size_t keysize = (size_t)(k - key);
    size_t keyvalsize = 1 + keysize + (value ? (1 + (size_t)valueSize) : 0);
    if (keysize < 1 || keyvalsize > 256)
        return kDNSServiceErr_Invalid;

    /* Setting a key again replaces it: the old item is removed and the new one
     * goes to the end, as in Bonjour. UxPlay relies on this ("sf"). */
    uint8_t *old = txt_find(t, key);
    if (old) {
        size_t itemlen = 1 + (size_t)old[0];
        size_t rest = (size_t)(t->buffer + t->datalen - (old + itemlen));
        memmove(old, old + itemlen, rest);
        t->datalen = (uint16_t)(t->datalen - itemlen);
    }

    if ((size_t)t->datalen + keyvalsize > t->buflen) {
        size_t newlen = (size_t)t->datalen + keyvalsize;
        if (newlen > 0xFFFF)
            return kDNSServiceErr_Invalid;
        uint8_t *nb = (uint8_t *)malloc(newlen);
        if (!nb)
            return kDNSServiceErr_NoMemory;
        if (t->datalen)
            memcpy(nb, t->buffer, t->datalen);
        if (t->malloced)
            free(t->buffer);
        t->buffer = nb;
        t->buflen = (uint16_t)newlen;
        t->malloced = 1;
    }

    uint8_t *start = t->buffer + t->datalen;
    uint8_t *p = start + 1;
    memcpy(p, key, keysize);
    p += keysize;
    if (value) {
        *p++ = '=';
        memcpy(p, value, valueSize);
        p += valueSize;
    }
    *start = (uint8_t)(p - start - 1);
    t->datalen = (uint16_t)(t->datalen + (p - start));
    return kDNSServiceErr_NoError;
}

uint16_t DNSSD_API TXTRecordGetLength(const TXTRecordRef *txtRecord)
{
    return ((const TxtRec *)txtRecord)->datalen;
}

const void *DNSSD_API TXTRecordGetBytesPtr(const TXTRecordRef *txtRecord)
{
    return ((const TxtRec *)txtRecord)->buffer;
}

/* ---- Registration on the Windows API ----------------------------------- */

typedef struct {
    HANDLE event;
    volatile LONG fired;
    volatile LONG status;
    WinServiceInstance *result; /* what Windows reports back; freed by the waiter */
} Wait;

struct _DNSServiceRef_t {
    WinRegisterRequest req;
    WinServiceCancel cancel;
    WinServiceInstance *inst;
    Wait reg;   /* completion of DnsServiceRegister */
    Wait dereg; /* completion of DnsServiceDeRegister */
};

static VOID WINAPI on_complete(DWORD status, PVOID context, WinServiceInstance *inst)
{
    Wait *w = (Wait *)context;
    w->result = inst;
    InterlockedExchange(&w->status, (LONG)status);
    InterlockedExchange(&w->fired, 1);
    SetEvent(w->event);
}

/* Withdraw what Windows actually registered. Windows renames an instance
 * whose name is taken ("Name" -> "Name(1)"), and DnsServiceDeRegister with the
 * originally requested name then fails with DNS_ERROR_RECORD_DOES_NOT_EXIST,
 * leaving the renamed records published even after the process exits. So the
 * request is pointed at the instance from the registration callback. */
static int withdraw(DNSServiceRef ref)
{
    if (ref->reg.result)
        ref->req.pServiceInstance = ref->reg.result;
    ref->req.pQueryContext = &ref->dereg;
    ULONGLONG t0 = GetTickCount64();
    DWORD rc = p_deregister(&ref->req, NULL);
    if (rc != WIN_DNS_REQUEST_PENDING) {
        shim_log("DnsServiceDeRegister failed: %lu", (unsigned long)rc);
        return 1;
    }
    if (WaitForSingleObject(ref->dereg.event, DEREGISTER_WAIT_MS) != WAIT_OBJECT_0) {
        shim_log("withdrawal not confirmed after %d ms", DEREGISTER_WAIT_MS);
        return 0; /* the callback may still come: the caller must keep the memory */
    }
    shim_log("withdrawn: status %lu after %llu ms", (unsigned long)ref->dereg.status,
             (unsigned long long)(GetTickCount64() - t0));
    return 1;
}

static void free_ref(DNSServiceRef ref)
{
    if (ref->inst)
        p_free(ref->inst);
    if (ref->reg.result)
        p_free(ref->reg.result);
    if (ref->dereg.result)
        p_free(ref->dereg.result);
    if (ref->reg.event)
        CloseHandle(ref->reg.event);
    if (ref->dereg.event)
        CloseHandle(ref->dereg.event);
    free(ref);
}

/* UTF-8 (len bytes, or NUL-terminated if len < 0) to a malloc'ed wide string. */
static wchar_t *widen(const char *s, int len)
{
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, NULL, 0);
    if (n <= 0 && len != 0)
        return NULL;
    wchar_t *w = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w)
        return NULL;
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, len, w, n);
    w[n] = 0;
    return w;
}

/* "<instance>.<regtype>.<domain>". Windows splits the instance at every '.'
 * and does not understand the "\." escape of RFC 6763 (the backslash is kept
 * and the name still splits), so a '.' in the instance name becomes '-'.
 * (A look-alike such as U+2024 works, but any non-ASCII character makes
 * Windows lower-case the whole name.) */
static wchar_t *service_fqdn(const char *name, const char *regtype, const char *domain)
{
    size_t nl = strlen(name), rl = strlen(regtype), dl = strlen(domain);
    char *s = (char *)malloc(nl + rl + dl + 3);
    if (!s)
        return NULL;
    char *p = s;
    for (size_t i = 0; i < nl; i++)
        *p++ = name[i] == '.' ? '-' : name[i];
    *p++ = '.';
    memcpy(p, regtype, rl);
    p += rl;
    if (rl && regtype[rl - 1] == '.')
        p--;
    *p++ = '.';
    memcpy(p, domain, dl);
    p += dl;
    if (dl && domain[dl - 1] == '.')
        p--;
    *p = 0;
    wchar_t *w = widen(s, -1);
    free(s);
    return w;
}

/* "<computer name>.local": the host the system already publishes A/AAAA
 * records for. Windows does not publish addresses for any other host name. */
static wchar_t *local_host_name(void)
{
    wchar_t host[256];
    DWORD n = 256 - 7;
    if (!GetComputerNameExW(ComputerNameDnsHostname, host, &n) || n == 0)
        return NULL;
    wcscat(host, L".local");
    return _wcsdup(host);
}

static int split_txt(uint16_t txtLen, const uint8_t *txt, PWSTR **keys_out, PWSTR **values_out,
                     DWORD *count_out)
{
    DWORD count = 0;
    for (const uint8_t *p = txt, *e = txt + txtLen; p < e; p += 1 + p[0]) {
        if (p + 1 + p[0] > e)
            return 0;
        if (p[0])
            count++;
    }
    PWSTR *keys = (PWSTR *)calloc(count ? count : 1, sizeof(PWSTR));
    PWSTR *values = (PWSTR *)calloc(count ? count : 1, sizeof(PWSTR));
    if (!keys || !values) {
        free(keys);
        free(values);
        return 0;
    }
    DWORD i = 0;
    int ok = 1;
    for (const uint8_t *p = txt, *e = txt + txtLen; p < e && ok; p += 1 + p[0]) {
        if (!p[0])
            continue;
        const char *item = (const char *)p + 1;
        const char *eq = memchr(item, '=', p[0]);
        int keylen = eq ? (int)(eq - item) : p[0];
        keys[i] = widen(item, keylen);
        values[i] = eq ? widen(eq + 1, (int)(p[0] - keylen - 1)) : widen("", 0);
        if (!keys[i] || !values[i])
            ok = 0; /* binary values cannot be passed as wide strings */
        i++;
    }
    *keys_out = keys;
    *values_out = values;
    *count_out = i;
    return ok;
}

static void free_strings(PWSTR *list, DWORD count)
{
    if (!list)
        return;
    for (DWORD i = 0; i < count; i++)
        free(list[i]);
    free(list);
}

static DNSServiceErrorType map_status(DWORD status)
{
    switch (status) {
    case ERROR_SUCCESS:
        return kDNSServiceErr_NoError;
    case ERROR_ALREADY_EXISTS:
    case 9711: /* DNS_ERROR_RECORD_ALREADY_EXISTS */
    case 9709: /* DNS_ERROR_RCODE_YXDOMAIN */
        return kDNSServiceErr_NameConflict;
    case ERROR_SERVICE_NOT_ACTIVE:
    case RPC_S_SERVER_UNAVAILABLE:
        return kDNSServiceErr_ServiceNotRunning;
    case ERROR_INVALID_PARAMETER:
        return kDNSServiceErr_BadParam;
    default:
        return kDNSServiceErr_Unknown;
    }
}

static DNSServiceErrorType native_register(DNSServiceRef *sdRef, uint32_t interfaceIndex,
                                           const char *name, const char *regtype,
                                           const char *domain, const char *host, uint16_t port,
                                           uint16_t txtLen, const void *txtRecord)
{
    if (interfaceIndex >= 0xFFFFFFF0u) /* LocalOnly, Unicast, P2P, BLE */
        return kDNSServiceErr_Unsupported;

    DNSServiceErrorType err = kDNSServiceErr_NoMemory;
    wchar_t *wname = NULL, *whost = NULL;
    PWSTR *keys = NULL, *values = NULL;
    DWORD count = 0;
    DNSServiceRef ref = NULL;

    char computer[256];
    if (!name || !*name) { /* Bonjour default: the computer name */
        DWORD n = sizeof(computer);
        if (!GetComputerNameExA(ComputerNameDnsHostname, computer, &n))
            goto out;
        name = computer;
    }
    wname = service_fqdn(name, regtype, domain && *domain ? domain : "local");
    if (host && *host) {
        size_t hl = strlen(host);
        whost = widen(host, (int)(host[hl - 1] == '.' ? hl - 1 : hl));
    } else {
        whost = local_host_name();
    }
    if (!wname || !whost)
        goto out;
    if (!split_txt(txtLen, (const uint8_t *)txtRecord, &keys, &values, &count)) {
        err = kDNSServiceErr_Invalid;
        goto out;
    }

    ref = (DNSServiceRef)calloc(1, sizeof(*ref));
    if (!ref)
        goto out;
    ref->reg.event = CreateEventW(NULL, TRUE, FALSE, NULL);
    ref->dereg.event = CreateEventW(NULL, TRUE, FALSE, NULL);
    /* Apple passes the port in network byte order; Windows wants host order. */
    WORD hport = (WORD)((port >> 8) | (port << 8));
    ref->inst = p_construct(wname, whost, NULL, NULL, hport, 0, 0, count, (PCWSTR *)keys,
                            (PCWSTR *)values);
    if (!ref->reg.event || !ref->dereg.event || !ref->inst)
        goto out;

    ref->req.Version = WIN_DNS_QUERY_REQUEST_VERSION1;
    ref->req.InterfaceIndex = interfaceIndex;
    ref->req.pServiceInstance = ref->inst;
    ref->req.pRegisterCompletionCallback = on_complete;
    ref->req.pQueryContext = &ref->reg;
    ref->req.unicastEnabled = FALSE;

    ULONGLONG t0 = GetTickCount64();
    DWORD rc = p_register(&ref->req, &ref->cancel);
    if (rc != WIN_DNS_REQUEST_PENDING) {
        shim_log("DnsServiceRegister(%s) failed: %lu", regtype, (unsigned long)rc);
        err = map_status(rc);
        goto out;
    }
    if (WaitForSingleObject(ref->reg.event, REGISTER_WAIT_MS) != WAIT_OBJECT_0) {
        /* Still pending: carry on, as Bonjour's DNSServiceRegister would. */
        shim_log("registering %s: no answer from Windows after %d ms, continuing", regtype,
                 REGISTER_WAIT_MS);
        *sdRef = ref;
        ref = NULL;
        err = kDNSServiceErr_NoError;
        goto out;
    }
    DWORD status = (DWORD)ref->reg.status;
    shim_log("registered %s: status %lu after %llu ms", regtype, (unsigned long)status,
             (unsigned long long)(GetTickCount64() - t0));
    if (status != ERROR_SUCCESS) {
        err = map_status(status);
        goto out;
    }
    WinServiceInstance *got = ref->reg.result;
    if (got && got->pszInstanceName && _wcsicmp(got->pszInstanceName, wname) != 0) {
        /* The name is taken on the network and Windows picked "Name(1)".
         * Bonjour would keep the new name, but a renamed instance is not
         * cleaned up when the process ends (see withdraw()), and the app ends
         * uxplay with a kill. Withdraw it now and report the conflict. */
        shim_log("name already in use on the network: asked for \"%ls\", Windows offered \"%ls\"",
                 wname, got->pszInstanceName);
        if (!withdraw(ref))
            ref = NULL; /* callback still pending: keep the memory */
        err = kDNSServiceErr_NameConflict;
        goto out;
    }
    *sdRef = ref;
    ref = NULL;
    err = kDNSServiceErr_NoError;

out:
    if (ref)
        free_ref(ref);
    free(wname);
    free(whost);
    free_strings(keys, count);
    free_strings(values, count);
    return err;
}

static void native_deallocate(DNSServiceRef ref)
{
    if (!ref->reg.fired &&
        WaitForSingleObject(ref->reg.event, DEREGISTER_WAIT_MS) != WAIT_OBJECT_0) {
        /* Never confirmed: cancel it. The callback may still run later, so
         * the memory is deliberately not freed. */
        if (p_cancel)
            p_cancel(&ref->cancel);
        shim_log("cancelled a registration that never completed");
        return;
    }
    if (ref->reg.status == ERROR_SUCCESS && !withdraw(ref))
        return; /* callback still pending: keep the memory */
    free_ref(ref);
}

/* ---- Exported registration calls --------------------------------------- */

DNSServiceErrorType DNSSD_API DNSServiceRegister(DNSServiceRef *sdRef, DNSServiceFlags flags,
                                                 uint32_t interfaceIndex, const char *name,
                                                 const char *regtype, const char *domain,
                                                 const char *host, uint16_t port, uint16_t txtLen,
                                                 const void *txtRecord,
                                                 DNSServiceRegisterReply callBack, void *context)
{
    if (!sdRef || !regtype)
        return kDNSServiceErr_BadParam;
    *sdRef = NULL;

    switch (mode()) {
    case MODE_BONJOUR:
        return bj_register(sdRef, flags, interfaceIndex, name, regtype, domain, host, port,
                           txtLen, txtRecord, callBack, context);
    case MODE_NATIVE:
        break;
    default:
        return kDNSServiceErr_ServiceNotRunning;
    }

    DNSServiceErrorType err = native_register(sdRef, interfaceIndex, name, regtype, domain, host,
                                              port, txtLen, txtRecord);
    /* Bonjour delivers the reply from DNSServiceProcessResult, which this DLL
     * does not provide; report success here instead (UxPlay passes no reply). */
    if (err == kDNSServiceErr_NoError && callBack)
        callBack(*sdRef, kDNSServiceFlagsAdd, err, name, regtype,
                 domain && *domain ? domain : "local.", context);
    return err;
}

void DNSSD_API DNSServiceRefDeallocate(DNSServiceRef sdRef)
{
    if (!sdRef)
        return;
    if (mode() == MODE_BONJOUR)
        bj_deallocate(sdRef);
    else if (mode() == MODE_NATIVE)
        native_deallocate(sdRef);
}
