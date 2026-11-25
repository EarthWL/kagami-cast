// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Test for the dnssd.dll shim, using the calls the way UxPlay's lib/dnssd.c
 * does.
 *
 *   shimtest txt  <shim.dll>
 *       Build UxPlay's RAOP and AirPlay TXT records (PIN modes 0..3) with the
 *       shim; fail if any differs from what Bonjour builds (recorded values,
 *       plus a byte-for-byte comparison when Bonjour's System32\dnssd.dll is
 *       installed). build-all.sh runs this.
 *
 *   shimtest reg  <shim.dll> <name> <seconds> [stop|kill]
 *       Register _raop._tcp "021122334455@<name>" and _airplay._tcp "<name>"
 *       with UxPlay's TXT, keep them for <seconds>, then withdraw them with
 *       DNSServiceRefDeallocate ("stop", default) or end the process without
 *       withdrawing ("kill", like the app's QProcess::kill()).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _DNSServiceRef_t *DNSServiceRef;
typedef union {
    char PrivateData[16];
    char *ForceNaturalAlignment;
} TXTRecordRef;
typedef int32_t(__stdcall *Register_t)(DNSServiceRef *, uint32_t, uint32_t, const char *,
                                       const char *, const char *, const char *, uint16_t,
                                       uint16_t, const void *, void *, void *);
typedef void(__stdcall *Dealloc_t)(DNSServiceRef);
typedef void(__stdcall *Create_t)(TXTRecordRef *, uint16_t, void *);
typedef void(__stdcall *TxtDealloc_t)(TXTRecordRef *);
typedef int32_t(__stdcall *Set_t)(TXTRecordRef *, const char *, uint8_t, const void *);
typedef uint16_t(__stdcall *Len_t)(const TXTRecordRef *);
typedef const void *(__stdcall *Ptr_t)(const TXTRecordRef *);

typedef struct {
    Register_t Register;
    Dealloc_t Dealloc;
    Create_t Create;
    TxtDealloc_t TxtDealloc;
    Set_t Set;
    Len_t Len;
    Ptr_t Ptr;
} Api;

static int load(Api *a, const char *path)
{
    HMODULE m = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!m) {
        fprintf(stderr, "cannot load %s (error %lu)\n", path, GetLastError());
        return 0;
    }
    a->Register = (Register_t)(void *)GetProcAddress(m, "DNSServiceRegister");
    a->Dealloc = (Dealloc_t)(void *)GetProcAddress(m, "DNSServiceRefDeallocate");
    a->Create = (Create_t)(void *)GetProcAddress(m, "TXTRecordCreate");
    a->TxtDealloc = (TxtDealloc_t)(void *)GetProcAddress(m, "TXTRecordDeallocate");
    a->Set = (Set_t)(void *)GetProcAddress(m, "TXTRecordSetValue");
    a->Len = (Len_t)(void *)GetProcAddress(m, "TXTRecordGetLength");
    a->Ptr = (Ptr_t)(void *)GetProcAddress(m, "TXTRecordGetBytesPtr");
    if (!a->Register || !a->Dealloc || !a->Create || !a->TxtDealloc || !a->Set || !a->Len ||
        !a->Ptr) {
        fprintf(stderr, "%s lacks a DNS-SD export\n", path);
        return 0;
    }
    return 1;
}

/* Values from UxPlay lib/dnssdint.h and lib/global.h. */
#define PK "b07727d6f6cd6e08b58ede525ec3cdeaa252ad9f683feb212ef8a205246554e7"
#define FEATURES "0x5A7FFEE6,0x0"
#define MODEL "AppleTV3,2"
#define VERSION "220.68"
#define DEVICE_ID "02:11:22:33:44:55"

#define SET(r, k, v) a->Set(r, k, (uint8_t)strlen(v), v)

/* Same calls, same order as dnssd_register_raop() in UxPlay lib/dnssd.c. */
static void raop_txt(const Api *a, TXTRecordRef *r, int pin_pw)
{
    a->Create(r, 0, NULL);
    SET(r, "ch", "2");
    SET(r, "cn", "0,1,2,3");
    SET(r, "da", "true");
    SET(r, "et", "0,3,5");
    SET(r, "vv", "2");
    SET(r, "ft", FEATURES);
    SET(r, "am", MODEL);
    SET(r, "md", "0,1,2");
    SET(r, "rhd", "5.6.0.0");
    switch (pin_pw) {
    case 2:
    case 3:
        SET(r, "pw", "true");
        a->Set(r, "sf", 4, "0x84");
        break;
    case 1:
        SET(r, "pw", "true");
        a->Set(r, "sf", 3, "0x8c");
        break;
    default:
        SET(r, "pw", "false");
        SET(r, "sf", "0x4");
        break;
    }
    SET(r, "sr", "44100");
    SET(r, "ss", "16");
    SET(r, "sv", "false");
    SET(r, "tp", "UDP");
    SET(r, "txtvers", "1");
    SET(r, "sf", "0x4");
    SET(r, "vs", VERSION);
    SET(r, "vn", "65537");
    SET(r, "pk", PK);
}

/* Same calls, same order as dnssd_register_airplay(). */
static void airplay_txt(const Api *a, TXTRecordRef *r, int pin_pw)
{
    a->Create(r, 0, NULL);
    SET(r, "deviceid", DEVICE_ID);
    SET(r, "features", FEATURES);
    SET(r, "pw", pin_pw ? "true" : "false");
    a->Set(r, "flags", 3, "0x4");
    SET(r, "model", MODEL);
    SET(r, "pk", PK);
    SET(r, "pi", "2e388006-13ba-4041-9a67-25dd4a43d536");
    SET(r, "srcvers", VERSION);
    SET(r, "vv", "2");
}

static void hex(const char *label, const void *p, int n)
{
    printf("%s (%d bytes): ", label, n);
    for (int i = 0; i < n; i++)
        printf("%02x", ((const uint8_t *)p)[i]);
    printf("\n");
}

static int same(const Api *x, TXTRecordRef *rx, const Api *y, TXTRecordRef *ry,
                const char *what)
{
    int lx = x->Len(rx), ly = y->Len(ry);
    int ok = lx == ly && memcmp(x->Ptr(rx), y->Ptr(ry), (size_t)lx) == 0;
    printf("%-14s %s (%d bytes)\n", what, ok ? "same" : "DIFFERENT", lx);
    if (!ok) {
        hex("  shim   ", x->Ptr(rx), lx);
        hex("  bonjour", y->Ptr(ry), ly);
    }
    return ok;
}

static uint64_t fnv1a(const void *p, int n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < n; i++)
        h = (h ^ ((const uint8_t *)p)[i]) * 0x100000001b3ull;
    return h;
}

/* Length and FNV-1a hash of each record as Apple Bonjour 3.1.0.1 builds it
 * (recorded with "shimtest txt" on a PC with Bonjour installed), so the check
 * also runs where Bonjour is absent, e.g. on CI. Index: pin * 2 + airplay. */
static const struct {
    int len;
    uint64_t hash;
} kGolden[8] = {
    {235, 0xb4bf54f70de7a2d7ull}, {215, 0x79c673080a1ad7faull}, /* pin 0 */
    {234, 0x0bbfc121234da125ull}, {214, 0xd12a7e8767c27b2cull}, /* pin 1 */
    {234, 0x0bbfc121234da125ull}, {214, 0xd12a7e8767c27b2cull}, /* pin 2 */
    {234, 0x0bbfc121234da125ull}, {214, 0xd12a7e8767c27b2cull}, /* pin 3 */
};

static int test_golden(const Api *s)
{
    int ok = 1;
    for (int i = 0; i < 8; i++) {
        TXTRecordRef r;
        if (i % 2)
            airplay_txt(s, &r, i / 2);
        else
            raop_txt(s, &r, i / 2);
        int len = s->Len(&r);
        uint64_t h = fnv1a(s->Ptr(&r), len);
        int same = len == kGolden[i].len && h == kGolden[i].hash;
        printf("%-7s pin=%d  %s  {%d, 0x%016llxull}\n", i % 2 ? "airplay" : "raop", i / 2,
               same ? "matches Bonjour" : "DIFFERS from Bonjour", len, (unsigned long long)h);
        ok &= same;
        s->TxtDealloc(&r);
    }
    return ok;
}

static int test_txt(const char *shim_path)
{
    Api s, b;
    if (!load(&s, shim_path))
        return 2;
    int ok = test_golden(&s);

    char bonjour[MAX_PATH];
    GetSystemDirectoryA(bonjour, MAX_PATH);
    strcat(bonjour, "\\dnssd.dll");
    if (GetFileAttributesA(bonjour) == INVALID_FILE_ATTRIBUTES) {
        printf(ok ? "TXT: identical (Bonjour not installed: compared with recorded values)\n"
                  : "TXT: MISMATCH\n");
        return ok ? 0 : 1;
    }
    if (!load(&b, bonjour))
        return 2;

    for (int pin = 0; pin <= 3; pin++) {
        TXTRecordRef rs, rb;
        char what[32];
        raop_txt(&s, &rs, pin);
        raop_txt(&b, &rb, pin);
        snprintf(what, sizeof what, "raop pin=%d", pin);
        ok &= same(&s, &rs, &b, &rb, what);
        s.TxtDealloc(&rs);
        b.TxtDealloc(&rb);

        airplay_txt(&s, &rs, pin);
        airplay_txt(&b, &rb, pin);
        snprintf(what, sizeof what, "airplay pin=%d", pin);
        ok &= same(&s, &rs, &b, &rb, what);
        s.TxtDealloc(&rs);
        b.TxtDealloc(&rb);
    }

    /* Error paths must match too. */
    TXTRecordRef rs, rb;
    s.Create(&rs, 0, NULL);
    b.Create(&rb, 0, NULL);
    const char *bad[] = {"", "a=b", "tab\tkey"};
    for (int i = 0; i < 3; i++) {
        int es = s.Set(&rs, bad[i], 1, "x"), eb = b.Set(&rb, bad[i], 1, "x");
        printf("bad key %-9s shim %d, bonjour %d%s\n", i == 0 ? "\"\"" : bad[i], es, eb,
               es == eb ? "" : "  DIFFERENT");
        ok &= es == eb;
    }
    char big[300];
    memset(big, 'v', sizeof big);
    int es = s.Set(&rs, "k", 255, big), eb = b.Set(&rb, "k", 255, big);
    printf("oversize value shim %d, bonjour %d%s\n", es, eb, es == eb ? "" : "  DIFFERENT");
    ok &= es == eb;
    es = s.Set(&rs, "flag", 0, NULL), eb = b.Set(&rb, "flag", 0, NULL);
    ok &= es == eb;
    ok &= same(&s, &rs, &b, &rb, "key, no value");
    s.TxtDealloc(&rs);
    b.TxtDealloc(&rb);

    printf(ok ? "TXT: all identical\n" : "TXT: MISMATCH\n");
    return ok ? 0 : 1;
}

static uint16_t htons_(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

static int test_reg(const char *shim_path, const char *name, int seconds, int kill)
{
    Api s;
    if (!load(&s, shim_path))
        return 2;

    TXTRecordRef raop, airplay;
    raop_txt(&s, &raop, 0);
    airplay_txt(&s, &airplay, 0);
    hex("raop TXT", s.Ptr(&raop), s.Len(&raop));
    hex("airplay TXT", s.Ptr(&airplay), s.Len(&airplay));

    char raop_name[256];
    snprintf(raop_name, sizeof raop_name, "021122334455@%s", name);

    DNSServiceRef r1 = NULL, r2 = NULL;
    ULONGLONG t0 = GetTickCount64();
    int e1 = s.Register(&r1, 0, 0, raop_name, "_raop._tcp", NULL, NULL, htons_(7000),
                        s.Len(&raop), s.Ptr(&raop), NULL, NULL);
    int e2 = s.Register(&r2, 0, 0, name, "_airplay._tcp", NULL, NULL, htons_(7001),
                        s.Len(&airplay), s.Ptr(&airplay), NULL, NULL);
    printf("DNSServiceRegister: raop %d, airplay %d (%llu ms)\n", e1, e2,
           (unsigned long long)(GetTickCount64() - t0));
    fflush(stdout);

    Sleep((DWORD)seconds * 1000);

    if (kill) {
        printf("ending the process without withdrawing\n");
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    t0 = GetTickCount64();
    s.Dealloc(r1);
    s.Dealloc(r2);
    printf("DNSServiceRefDeallocate: %llu ms\n", (unsigned long long)(GetTickCount64() - t0));
    s.TxtDealloc(&raop);
    s.TxtDealloc(&airplay);
    return (e1 || e2) ? 1 : 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc >= 3 && strcmp(argv[1], "txt") == 0)
        return test_txt(argv[2]);
    if (argc >= 5 && strcmp(argv[1], "reg") == 0)
        return test_reg(argv[2], argv[3], atoi(argv[4]), argc > 5 && strcmp(argv[5], "kill") == 0);
    fprintf(stderr, "usage: shimtest txt <shim.dll>\n"
                    "       shimtest reg <shim.dll> <name> <seconds> [stop|kill]\n");
    return 2;
}
