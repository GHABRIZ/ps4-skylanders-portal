/*
 * host_sim.c - runs the real plugin code (main.c) on a Linux PC and plays the part of the game + phone.
 * The PS4 system calls are replaced by small shims below. Build & run: test/run_host_sim.sh <folder with .bin>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>

static char ROOT[512];
static void remap(const char *p, char *out) { snprintf(out, 1024, "%s%s", ROOT, p); }

/* ---- PS4 kernel shims ---- */
static DIR *g_dirs[1024];
int sceKernelOpen(const char *p, int fl, int mode)
{
    char r[1024];
    int lf = 0;
    remap(p, r);
    if (fl & 0x20000) lf = O_RDONLY | O_DIRECTORY;
    else if ((fl & 3) == 1) lf = O_WRONLY;
    else if ((fl & 3) == 2) lf = O_RDWR;
    if (fl & 0x200) lf |= O_CREAT;
    if (fl & 0x400) lf |= O_TRUNC;
    int fd = open(r, lf, mode);
    return fd < 0 ? -1 : fd;
}
int64_t sceKernelRead(int fd, void *b, size_t n) { return read(fd, b, n); }
int64_t sceKernelWrite(int fd, const void *b, size_t n) { return write(fd, b, n); }
int sceKernelClose(int fd)
{
    if (fd >= 0 && fd < 1024 && g_dirs[fd]) { closedir(g_dirs[fd]); g_dirs[fd] = NULL; return 0; }
    return close(fd);
}
int sceKernelMkdir(const char *p, int m) { char r[1024]; remap(p, r); return mkdir(r, m); }
int sceKernelUnlink(const char *p) { char r[1024]; remap(p, r); return unlink(r); }
int sceKernelGetdents(int fd, char *buf, int size) /* FreeBSD dirent records */
{
    struct dirent *d;
    int n = 0;
    if (!g_dirs[fd]) g_dirs[fd] = fdopendir(fd);
    if (!g_dirs[fd]) return -1;
    while (1) {
        long pos = telldir(g_dirs[fd]);
        d = readdir(g_dirs[fd]);
        if (!d) break;
        int nl = (int)strlen(d->d_name), rl = (8 + nl + 1 + 3) & ~3;
        if (n + rl > size) { seekdir(g_dirs[fd], pos); break; }
        memset(buf + n, 0, rl);
        *(uint32_t *)(buf + n) = 1;
        *(uint16_t *)(buf + n + 4) = (uint16_t)rl;
        buf[n + 6] = d->d_type;
        buf[n + 7] = (char)nl;
        memcpy(buf + n + 8, d->d_name, nl);
        n += rl;
    }
    return n;
}
int sceKernelUsleep(unsigned us) { return usleep(us); }
#include "orbis/libkernel.h"
int sceKernelGetModuleInfo(OrbisKernelModule m, OrbisKernelModuleInfo *i) { return -1; }
int sceKernelGetModuleList(OrbisKernelModule *h, size_t n, size_t *o) { *o = 0; return -1; }
int sceKernelDlsym(int h, const char *n, void **a) { *a = NULL; return -1; }
int sceKernelVirtualQuery(const void *a, int f, OrbisKernelVirtualQueryInfo *i, size_t s) { return -1; }
int sceKernelMprotect(const void *a, size_t s, int p) { return -1; }
int scePthreadCreate(OrbisPthread *t, const OrbisPthreadAttr *a, void *(*f)(void *), void *arg, const char *n)
{
    return pthread_create(t, a, f, arg);
}
int scePthreadAttrInit(OrbisPthreadAttr *a) { return pthread_attr_init(a); }
int scePthreadAttrSetstacksize(OrbisPthreadAttr *a, size_t s) { return pthread_attr_setstacksize(a, s); }
int scePthreadAttrDestroy(OrbisPthreadAttr *a) { return pthread_attr_destroy(a); }
int scePthreadAttrSetdetachstate(OrbisPthreadAttr *a, int d) { return pthread_attr_setdetachstate(a, d ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE); }

/* ---- BSD socket shims (FreeBSD sockaddr has a length byte first; option numbers differ) ---- */
int sim_socket(int a, int b, int c) { return socket(a, b, c); }
int sim_bind(int s, const void *sa, unsigned l)
{
    const uint8_t *p = sa;
    struct sockaddr_in in;
    memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    memcpy(&in.sin_port, p + 2, 2);
    in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    return bind(s, (struct sockaddr *)&in, sizeof(in));
}
int sim_listen(int s, int n) { return listen(s, n); }
int sim_accept(int s, void *a, unsigned *l) { return accept(s, NULL, NULL); }
int sim_recv(int s, void *b, size_t n, int f) { return (int)recv(s, b, n, 0); }
int sim_send(int s, const void *b, size_t n, int f) { return (int)send(s, b, n, MSG_NOSIGNAL); }
int sim_setsockopt(int s, int l, int o, const void *v, unsigned n) { return 0; }
int sim_close(int s) { return close(s); }
#define socket sim_socket
#define bind sim_bind
#define listen sim_listen
#define accept sim_accept
#define recv sim_recv
#define send sim_send
#define setsockopt sim_setsockopt
#define close sim_close

#define WEB_PORT_OVERRIDE
#include "../source/main.c"

/* ======================================================================= the "game" */
static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } else printf("ok   %s\n", msg); } while (0)

typedef struct {
    uint64_t handle; uint8_t flags, ep, type, pad; uint32_t timeout;
    int32_t status, length, actual, pad2; uint64_t cb, user, buf; uint8_t rest[16];
} Xfer; /* libusb_transfer layout: +9 ep, +10 type, +16 status, +20 length, +24 actual, +32 cb, +48 buffer */

static volatile int g_done;
static uint64_t game_cb(uint64_t t) { g_done++; return 0; }
static uint64_t real_list(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h)
{
    static uint64_t empty[1] = {0};
    *(uint64_t *)a = (uint64_t)empty;
    return 0;
}
static uint64_t real_other(uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g, uint64_t h) { return 0xDEAD; }

enum { I_LIST, I_DESC, I_OPEN, I_SUBMIT, I_CTRL, I_INTR, I_CANCEL, I_VIDPID, I_KDA, I_KDD, I_HE, I_N };
static uint64_t call(int i, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f, uint64_t g)
{
    return ((raw8_fn)g_gw[i])(a, b, c, d, e, f, g, 0);
}

static int wait_done(int target)
{
    int i;
    for (i = 0; i < 500 && g_done < target; i++) usleep(1000);
    return g_done >= target;
}

static uint8_t g_buf[64], g_inbuf[64];
static Xfer g_x, g_in;

static void send_cmd(const uint8_t *d, int n) /* async SET_REPORT */
{
    int target = g_done + 1;
    memset(g_buf, 0, sizeof(g_buf));
    g_buf[0] = 0x21; g_buf[1] = 0x09; g_buf[2] = 0x00; g_buf[3] = 0x02; g_buf[6] = 32;
    memcpy(g_buf + 8, d, n);
    memset(&g_x, 0, sizeof(g_x));
    g_x.handle = FAKE_HANDLE; g_x.ep = 0; g_x.type = 0; g_x.length = 40; g_x.cb = (uint64_t)game_cb; g_x.buf = (uint64_t)g_buf;
    call(I_SUBMIT, (uint64_t)&g_x, 0, 0, 0, 0, 0, 0);
    wait_done(target);
}

static void read_in(uint8_t *out) /* async interrupt IN */
{
    int target = g_done + 1;
    memset(&g_in, 0, sizeof(g_in));
    memset(g_inbuf, 0, sizeof(g_inbuf));
    g_in.handle = FAKE_HANDLE; g_in.ep = 0x81; g_in.type = 3; g_in.length = 32; g_in.cb = (uint64_t)game_cb; g_in.buf = (uint64_t)g_inbuf;
    call(I_SUBMIT, (uint64_t)&g_in, 0, 0, 0, 0, 0, 0);
    wait_done(target);
    memcpy(out, g_inbuf, 32);
}

/* reads IN reports until one starting with letter c (skipping status reports) */
static int read_until(char c, uint8_t *out)
{
    int i;
    for (i = 0; i < 20; i++) {
        read_in(out);
        if (out[0] == (uint8_t)c) return 1;
    }
    return 0;
}

static int st(const uint8_t *r, int s) { return (r[1 + s / 4] >> (2 * (s % 4))) & 3; }

static char *http(const char *args)
{
    static char out[65536];
    char cmd[2048];
    FILE *p;
    size_t n;
    snprintf(cmd, sizeof(cmd), "curl -s --max-time 5 %s", args);
    p = popen(cmd, "r");
    n = fread(out, 1, sizeof(out) - 1, p);
    out[n] = 0;
    pclose(p);
    return out;
}

int main(int argc, char **argv)
{
    uint8_t r[32], orig[1024], sv[1024];
    char path[1024], *s;
    int i;
    if (argc < 3) { printf("usage: host_sim <root> <a figure .bin inside root figures/>\n"); return 2; }
    snprintf(ROOT, sizeof(ROOT), "%s", argv[1]);
    {
        FILE *f = fopen(argv[2], "rb");
        if (!f || fread(orig, 1, 1024, f) != 1024) { printf("bad figure\n"); return 2; }
        fclose(f);
    }

    plugin_load(0, NULL);
    sleep(1); /* web thread scans the library */
    CHECK(g_lib_n > 0, "library scanned");
    printf("     (%d figure files)\n", g_lib_n);

    /* pretend the hook found these pointers in the game */
    g_text_lo = 0; g_text_hi = ~0ULL;
    {
        int roles[I_N] = {R_LIST, R_DESC, R_OPEN, R_SUBMIT, R_CTRL, R_INTR, R_CANCEL, R_VIDPID, R_KDACTIVE, R_KDDETACH, R_HANDLE};
        for (i = 0; i < I_N; i++) {
            g_slot[i].orig = (uint64_t)(i == I_LIST ? real_list : real_other);
            g_slot[i].role = roles[i];
        }
        g_nslot = I_N;
        g_installed = 1;
    }

    /* device discovery */
    {
        uint64_t list = 0, h = 0;
        uint8_t dd[18];
        CHECK(call(I_LIST, (uint64_t)&list, 0, 0, 0, 0, 0, 0) == 1 && ((uint64_t *)list)[0] == FAKE_DEV, "device list gets the virtual portal");
        call(I_DESC, FAKE_DEV, (uint64_t)dd, 0, 0, 0, 0, 0);
        CHECK(dd[8] == 0x30 && dd[9] == 0x14 && dd[10] == 0x50 && dd[11] == 0x01, "device descriptor is 1430:0150");
        call(I_OPEN, FAKE_DEV, (uint64_t)&h, 0, 0, 0, 0, 0);
        CHECK(h == FAKE_HANDLE, "open gives our handle");
        CHECK(call(I_VIDPID, 0, 0x1430, 0x0150, 0, 0, 0, 0) == FAKE_HANDLE, "open by VID/PID");
        CHECK(call(I_VIDPID, 0, 0x054c, 0x05c4, 0, 0, 0, 0) == 0xDEAD, "other devices pass through");
    }

    /* like shadPS4: reads get status reports even before the game activates the portal */
    read_in(r);
    CHECK(r[0] == 'S' && r[6] == 0, "status report before activation (active = 0)");
    {
        uint8_t rr[32];
        int tr = 0;
        call(I_INTR, FAKE_HANDLE, 0x81, (uint64_t)rr, 32, (uint64_t)&tr, 30, 0);
        CHECK(rr[0] == 'S' && tr == 32, "sync read gets a status report too");
    }
    CHECK(call(I_KDA, FAKE_HANDLE, 0, 0, 0, 0, 0, 0) == 1, "kernel driver reported active at first");
    call(I_KDD, FAKE_HANDLE, 0, 0, 0, 0, 0, 0);
    CHECK(call(I_KDA, FAKE_HANDLE, 0, 0, 0, 0, 0, 0) == 0, "after detach: not active");

    /* async start-up like a game: R, A, then status */
    send_cmd((const uint8_t *)"R", 1);
    CHECK(read_until('R', r) && r[1] == 0x02 && r[2] == 0x1b, "R reply (async)");
    send_cmd((const uint8_t *)"A\x01", 2);
    CHECK(read_until('A', r) && r[2] == 0xFF, "A reply (async)");
    read_in(r);
    CHECK(r[0] == 'S' && r[6] == 1, "status report when idle");

    /* phone places a figure (library entry 0 is argv[2] - see run script) */
    s = http("'http://127.0.0.1:8080/api/place?id=0&slot=-1'");
    CHECK(strstr(s, "\"slots\":[{\"id\":0") != NULL, "web: place figure");
    {
        int added = 0, ready = 0;
        for (i = 0; i < 10; i++) { read_in(r); if (r[0] == 'S') { if (st(r, 0) == 3) added = 1; if (added && st(r, 0) == 1) { ready = 1; break; } } }
        CHECK(added && ready, "game sees ADDED then READY");
    }

    /* game reads block 1 */
    send_cmd((const uint8_t *)"Q\x10\x01", 3);
    CHECK(read_until('Q', r) && r[1] == 0x10 && memcmp(r + 3, orig + 16, 16) == 0, "Q block 1 matches the .bin");

    /* game writes block 8 -> saved ~1 s later */
    {
        uint8_t w[19] = {'W', 0x10, 8};
        for (i = 0; i < 16; i++) w[3 + i] = (uint8_t)(0xC0 + i);
        send_cmd(w, 19);
        CHECK(read_until('W', r) && r[1] == 0x10, "W reply");
        sleep(2);
        save_path_for(lib_rel(0), path, sizeof(path));
        {
            char rp[1024];
            FILE *f;
            remap(path, rp);
            f = fopen(rp, "rb");
            CHECK(f && fread(sv, 1, 1024, f) == 1024 && memcmp(sv + 128, w + 3, 16) == 0 && memcmp(sv, orig, 16) == 0, "progress saved to saves/");
            if (f) fclose(f);
        }
        {
            char op[1024];
            FILE *f;
            snprintf(op, sizeof(op), "%s", argv[2]);
            f = fopen(op, "rb");
            CHECK(f && fread(sv, 1, 1024, f) == 1024 && memcmp(sv, orig, 1024) == 0, "original .bin untouched");
            if (f) fclose(f);
        }
    }

    /* sync API path */
    {
        uint8_t q[32] = {'Q', 0x10, 8};
        int tr = 0;
        CHECK(call(I_CTRL, FAKE_HANDLE, 0x21, 0x09, 0x200, 0, (uint64_t)q, 32) == 32, "sync control transfer");
        memset(r, 0, 32);
        call(I_INTR, FAKE_HANDLE, 0x81, (uint64_t)r, 32, (uint64_t)&tr, 100, 0);
        for (i = 0; i < 10 && r[0] != 'Q'; i++) call(I_INTR, FAKE_HANDLE, 0x81, (uint64_t)r, 32, (uint64_t)&tr, 100, 0);
        CHECK(r[0] == 'Q' && tr == 32 && r[3] == 0xC0, "sync interrupt read returns written data");
        CHECK(call(I_CTRL, 0x1234, 0x21, 0x09, 0, 0, (uint64_t)q, 32) == 0xDEAD, "other handles pass through");
    }

    /* reset refused while on portal */
    s = http("'http://127.0.0.1:8080/api/reset?id=0'");
    CHECK(strstr(s, "Take the figure off") != NULL, "reset refused while on portal");

    /* remove -> REMOVING */
    s = http("'http://127.0.0.1:8080/api/remove?slot=0'");
    CHECK(strstr(s, "\"slots\":[null") != NULL, "web: remove");
    {
        int removing = 0, gone = 0;
        for (i = 0; i < 10; i++) { read_in(r); if (r[0] == 'S') { if (st(r, 0) == 2) removing = 1; if (removing && st(r, 0) == 0) { gone = 1; break; } } }
        CHECK(removing && gone, "game sees REMOVING then empty");
    }

    /* put it back: progress comes back from the save */
    http("'http://127.0.0.1:8080/api/place?id=0&slot=3'");
    send_cmd((const uint8_t *)"Q\x13\x08", 3);
    CHECK(read_until('Q', r) && r[1] == 0x13 && r[3] == 0xC0, "placed on chosen spot 4, progress restored");
    http("'http://127.0.0.1:8080/api/remove?slot=3'");
    s = http("'http://127.0.0.1:8080/api/reset?id=0'");
    CHECK(strstr(s, "Progress reset") != NULL, "reset after removal");
    {
        char rp[1024];
        remap(path, rp);
        CHECK(access(rp, F_OK) != 0, "save file deleted");
    }

    /* cancel a pending IN transfer */
    {
        int target;
        g_portal.tick += 0; /* status every 8 ms: submit and cancel quickly */
        memset(&g_in, 0, sizeof(g_in));
        g_in.handle = FAKE_HANDLE; g_in.ep = 0x81; g_in.type = 3; g_in.length = 32; g_in.cb = (uint64_t)game_cb; g_in.buf = (uint64_t)g_inbuf;
        target = g_done + 1;
        call(I_SUBMIT, (uint64_t)&g_in, 0, 0, 0, 0, 0, 0);
        call(I_CANCEL, (uint64_t)&g_in, 0, 0, 0, 0, 0, 0);
        wait_done(target);
        CHECK(g_in.status == 3 || g_in.status == 0, "cancel completes the transfer");
        printf("     (cancel status %d - 0 means the report went out first, also fine)\n", g_in.status);
    }

    /* upload from the phone */
    {
        int before = g_lib_n;
        char a[1200];
        snprintf(a, sizeof(a), "-X POST --data-binary @'%s' 'http://127.0.0.1:8080/api/upload?name=My%%20Spyro.bin'", argv[2]);
        s = http(a);
        CHECK(strstr(s, "\"msg\":\"uploaded\"") != NULL && g_lib_n == before + 1, "web: upload a .bin");
        s = http("-X POST --data-binary 'short' 'http://127.0.0.1:8080/api/upload?name=x.bin'");
        CHECK(strstr(s, "exactly 1024") != NULL, "web: upload rejects wrong size");
        snprintf(a, sizeof(a), "-X POST --data-binary @'%s' 'http://127.0.0.1:8080/api/upload?name=..%%2F..%%2Fevil.bin'", argv[2]);
        http(a);
        snprintf(path, sizeof(path), "%s/data/GoldHEN/evil.bin", ROOT);
        CHECK(access(path, F_OK) != 0, "web: upload cannot escape the folder");
    }

    /* game drives events itself: transfers complete inside its HandleEvents call, on its own thread */
    {
        int k, got = 0;
        uint8_t hb[64];
        Xfer hx;
        memset(&hx, 0, sizeof(hx));
        memset(hb, 0, sizeof(hb));
        hx.handle = FAKE_HANDLE; hx.ep = 0x81; hx.type = 3; hx.length = 32; hx.cb = (uint64_t)game_cb; hx.buf = (uint64_t)hb;
        call(I_HE, 0, 0, 0, 0, 0, 0, 0); /* first call switches the mode */
        for (k = 0; k < 5; k++) {
            int before = g_done;
            call(I_SUBMIT, (uint64_t)&hx, 0, 0, 0, 0, 0, 0);
            usleep(20000); /* the worker must NOT complete it now */
            if (g_done != before) break;
            call(I_HE, 0, 0, 0, 0, 0, 0, 0);
            if (g_done == before + 1 && hb[0] == 'S') got++;
        }
        CHECK(got == 5, "HandleEvents completes the reads (worker leaves them alone)");
        sleep(2); /* game stops calling HandleEvents -> worker takes over again */
        read_in(r);
        CHECK(r[0] == 'S', "worker takes over after the game stops calling HandleEvents");
    }

    /* speaker sound on endpoint 0x02 must never be taken as a command (it once wrote junk into a figure) */
    {
        uint8_t snd[32], before[16];
        Xfer ox;
        int target = g_done + 1;
        http("'http://127.0.0.1:8080/api/place?id=1&slot=5'");
        memcpy(before, g_portal.slot[5].data + 16 * 8, 16);
        memset(snd, 0x99, sizeof(snd));
        snd[0] = 'W'; snd[1] = 0x15; snd[2] = 8; /* looks exactly like "write block 8 of spot 6" */
        memset(&ox, 0, sizeof(ox));
        ox.handle = FAKE_HANDLE; ox.ep = 0x02; ox.type = 3; ox.length = 32; ox.cb = (uint64_t)game_cb; ox.buf = (uint64_t)snd;
        call(I_SUBMIT, (uint64_t)&ox, 0, 0, 0, 0, 0, 0);
        wait_done(target);
        CHECK(ox.status == 0 && ox.actual == 32, "speaker packet accepted");
        CHECK(memcmp(before, g_portal.slot[5].data + 16 * 8, 16) == 0, "speaker packet did NOT change the figure");
        snd[0] = 'R';
        target = g_done + 1;
        call(I_SUBMIT, (uint64_t)&ox, 0, 0, 0, 0, 0, 0);
        wait_done(target);
        CHECK(g_portal.activated == 1, "speaker packet starting with 'R' did NOT reset the portal");
        http("'http://127.0.0.1:8080/api/remove?slot=5'");
    }

    /* phone browsers: idle spare connections must not block real requests */
    {
        int idle[4], k;
        struct timespec t0, t1;
        for (k = 0; k < 4; k++) {
            struct sockaddr_in a;
            idle[k] = socket(AF_INET, SOCK_STREAM, 0);
            memset(&a, 0, sizeof(a));
            a.sin_family = AF_INET; a.sin_port = htons(8080); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            connect(idle[k], (struct sockaddr *)&a, sizeof(a));
        }
        usleep(100000);
        clock_gettime(CLOCK_MONOTONIC, &t0);
        s = http("--max-time 2 http://127.0.0.1:8080/api/state");
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        CHECK(strstr(s, "\"slots\"") != NULL && ms < 1000, "web: answers while 4 idle connections are open");
        printf("     (answered in %.0f ms)\n", ms);
        s = http("--max-time 4 --parallel --parallel-max 10 -o /dev/null -w '%{http_code}\\n' http://127.0.0.1:8080/api/state http://127.0.0.1:8080/ http://127.0.0.1:8080/api/lib http://127.0.0.1:8080/api/state http://127.0.0.1:8080/ http://127.0.0.1:8080/api/state http://127.0.0.1:8080/api/lib http://127.0.0.1:8080/api/state");
        {
            int n200 = 0; char *q = s;
            while ((q = strstr(q, "200"))) { n200++; q += 3; }
            CHECK(n200 == 8, "web: 8 requests at the same time all answered");
            printf("     (%d/8 ok)\n", n200);
        }
        for (k = 0; k < 4; k++) close(idle[k]);
    }

    /* page, library, log */
    s = http("http://127.0.0.1:8080/");
    CHECK(strstr(s, "Skylanders Portal") != NULL, "web: page served");
    s = http("http://127.0.0.1:8080/api/lib");
    CHECK(s[0] == '[' && strstr(s, ".bin") != NULL, "web: library json");
    s = http("http://127.0.0.1:8080/api/log");
    CHECK(strstr(s, "placed slot") != NULL, "web: log");

    /* the portal fills up at 8 */
    {
        int placed = 0;
        for (i = 0; i < 9 && i < g_lib_n; i++) {
            s = http(i == 0 ? "'http://127.0.0.1:8080/api/clear'" : "'http://127.0.0.1:8080/api/state'");
        }
        for (i = 0; i < 9 && i < g_lib_n; i++) {
            char u[128];
            snprintf(u, sizeof(u), "'http://127.0.0.1:8080/api/place?id=%d'", i);
            s = http(u);
            if (strstr(s, "Portal is full") == NULL && strstr(s, "not a 1KB") == NULL) placed++;
        }
        CHECK(placed <= 8, "never more than 8 on the portal");
        CHECK(i < 9 || strstr(s, "Portal is full") != NULL || strstr(s, "not a 1KB") != NULL, "9th figure refused");
    }

    /* state survives a restart */
    {
        char rp[1024], b[8192];
        FILE *f;
        snprintf(rp, sizeof(rp), "%s/data/GoldHEN/skylanders/state.txt", ROOT);
        f = fopen(rp, "rb");
        size_t n = f ? fread(b, 1, sizeof(b) - 1, f) : 0;
        if (f) fclose(f);
        b[n] = 0;
        CHECK(n > 0 && strstr(b, ".bin") != NULL, "state.txt written");
    }

    printf("\n%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
