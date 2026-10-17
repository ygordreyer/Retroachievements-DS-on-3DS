#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include "ra_login.h"
#define RA_NET_HOST_TEST
#include "../retail/arm9/source/ra_net.c"

static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static unsigned tailChecks, journalChecks;
static off_t syncedLength;
static int syncedFd = -1;
static int observedSync(int fd) {
    struct stat st;
    int result = fsync(fd);
    if (!result && !fstat(fd, &st)) { syncedLength = st.st_size; syncedFd = fd; }
    return result;
}
static int observedTruncate(int fd, off_t length) {
    struct stat st;
    CHECK(!fstat(fd, &st));
    if (st.st_size > length) {
        unsigned char bytes[256];
        CHECK(syncedFd == fd && syncedLength == st.st_size);
        for (off_t pos = length; pos < st.st_size;) {
            size_t n = st.st_size - pos < (off_t)sizeof(bytes) ? (size_t)(st.st_size - pos) : sizeof(bytes);
            CHECK(pread(fd, bytes, n, pos) == (ssize_t)n);
            for (size_t i = 0; i < n; ++i) CHECK(!bytes[i]);
            pos += n;
        }
        ++tailChecks;
    }
    return ftruncate(fd, length);
}
static int observedRemove(void* ctx, const char* name) {
    (void)ctx;
    if (!strcmp(name, "cfg.txn")) {
        FILE* f = fopen(name, "rb");
        if (f) {
            int c;
            while ((c = fgetc(f)) != EOF) CHECK(c == 0);
            CHECK(!ferror(f) && !fclose(f));
            ++journalChecks;
        }
    }
    return remove(name) == 0 || errno == ENOENT;
}
#define fsync observedSync
#define ftruncate observedTruncate
#include "../retail/arm9/source/ra_queue_io.c"
#undef fsync
#undef ftruncate

static const char path[] = "cfg";
static const char old[] = "# keep this comment\r\nusername=testuser\r\n password \t=fakepass\r\n"
    "password=fakepass\n# password=do not edit comments\nunknown= keep exactly \t\r\ntoken=\r\nsync=1";
static const char next[] = "# keep this comment\r\nusername=testuser\r\n password \t=\r\n"
    "password=\n# password=do not edit comments\nunknown= keep exactly \t\r\ntoken=faketoken0123\r\nsync=1";
static const char success[] = "{\"Success\":true,\"User\":\"TestUser\",\"Token\":\"faketoken0123\"}";
static const char invalid[] = "{\"Success\":false,\"Code\":\"invalid_token\",\"Error\":\"fakepass faketoken0123\"}";
typedef struct Image { int exists, cluster; size_t size; char data[RA_CFG_FILE_MAX + 32]; } Image;
static Image files[3], interrupted[3];
static const char *wantedOld = old, *wantedNext = next;
static int step, crash, targetError, journalError, readError, shortWrite, targetWrites;
static jmp_buf powerCut;
static char scratch[RA_CFG_FILE_MAX];
static void checkpoint(void) { if (++step == crash) longjmp(powerCut, 1); }
static Image* find(const char* name) {
    if (!strcmp(name, path)) return files;
    if (!strcmp(name, "cfg.new")) return files + 1;
    CHECK(!strcmp(name, "cfg.txn"));
    return files + 2;
}
static int readFile(void* ctx, const char* name, void* head, size_t headSize, void* data, size_t size) {
    Image* f = find(name);
    size_t n, h;
    (void)ctx;
    if (readError) return -2;
    if (!f->exists) return -1;
    n = f->size < headSize + size ? f->size : headSize + size;
    h = n < headSize ? n : headSize;
    if (h) memcpy(head, f->data, h);
    if (n > h) memcpy(data, f->data + h, n - h);
    checkpoint();
    return f->size > n ? (int)n + 1 : (int)n;
}
static void part(Image* f, size_t at, const void* data, size_t size) {
    CHECK(at + size <= sizeof(f->data));
    if (size) memcpy(f->data + at, data, size);
    if (f->size < at + size) f->size = at + size;
    checkpoint();
}
static int writeFile(void* ctx, const char* name, const void* head, size_t headSize,
                     const void* data, size_t size, int inplace) {
    Image* f = find(name);
    (void)ctx;
    int target = !strcmp(name, path);
    CHECK(inplace || !target);
    if ((target && targetError) || (!target && journalError)) return 0;
    if (target) ++targetWrites;
    if (!f->exists) { f->exists = 1; f->cluster = 17; f->size = 0; }
    if (!inplace) f->size = 0;
    checkpoint();
    if (headSize) {
        part(f, 0, head, headSize / 2);
        if (shortWrite == 1) return 0;
        part(f, 0, head, headSize);
    }
    part(f, headSize, data, size / 2);
    if (shortWrite == (inplace ? 3 : 2)) return 0;
    part(f, headSize, data, size);
    checkpoint();
    if (inplace) {
        if (f->size > size) {
            memset(f->data + size, 0, f->size - size);
            checkpoint();
            for (size_t i = size; i < f->size; ++i) CHECK(!f->data[i]);
            checkpoint();
        }
        f->size = size;
        checkpoint();
    }
    checkpoint();
    checkpoint();
    return 1;
}
static int removeFile(void* ctx, const char* name) {
    Image* f = find(name);
    (void)ctx;
    CHECK(strcmp(name, path));
    checkpoint();
    if (!strcmp(name, "cfg.txn") && f->exists) {
        for (size_t i = 0; i < f->size; ++i) CHECK(!f->data[i]);
    }
    f->exists = 0;
    checkpoint();
    return 1;
}
static raQueueFileOps io = {0, readFile, writeFile, removeFile};
static void reset(void) {
    memset(files, 0, sizeof(files));
    wantedOld = old; wantedNext = next;
    files[0].exists = 1; files[0].cluster = 42; files[0].size = sizeof(old) - 1;
    memcpy(files[0].data, old, sizeof(old) - 1);
    step = crash = targetError = journalError = readError = shortWrite = targetWrites = 0;
}
static void complete(void) {
    CHECK(files[0].exists && files[0].cluster == 42);
    CHECK((files[0].size == strlen(wantedOld) && !memcmp(files[0].data, wantedOld, strlen(wantedOld)))
       || (files[0].size == strlen(wantedNext) && !memcmp(files[0].data, wantedNext, strlen(wantedNext))));
    CHECK(!files[1].exists && !files[2].exists);
}
static void recoverCuts(void) {
    memcpy(interrupted, files, sizeof(files));
    step = crash = 0;
    CHECK(raFileRecover(path, scratch, sizeof(scratch), &io));
    int steps = step;
    complete();
    for (int cut = 1; cut <= steps; ++cut) {
        memcpy(files, interrupted, sizeof(files));
        step = 0; crash = cut;
        if (!setjmp(powerCut)) raFileRecover(path, scratch, sizeof(scratch), &io);
        crash = 0;
        CHECK(raFileRecover(path, scratch, sizeof(scratch), &io));
        complete();
        CHECK(raFileRecover(path, scratch, sizeof(scratch), &io));
        complete();
    }
}
static char logs[4096];
static int requests, passwordRequests, tokenRequests, reject, transportError;
static const char *rejection, *loginSuccess;
static void logLine(void* ctx, const char* text) {
    (void)ctx;
    CHECK(strlen(logs) + strlen(text) + 2 < sizeof(logs));
    strcat(logs, text); strcat(logs, "\n");
}
static int request(void* ctx, const char* url, char* reply, int size) {
    int password = strstr(url, "&p=") != 0;
    (void)ctx;
    ++requests;
    CHECK(strstr(url, "r=login2&u=testuser&") != 0);
    CHECK(password != (strstr(url, "&t=") != 0));
    if (password) { ++passwordRequests; CHECK(strstr(url, "&p=fakepass") != 0); }
    else { ++tokenRequests; CHECK(strstr(url, "&t=faketoken0123") != 0); }
    if (transportError) return -1;
    const char* body = (reject == 2 || (reject == 3 && password)) ? "{\"Success\":false,\"Error\":\"fakepass faketoken0123\"}"
             : (!password && reject) ? rejection : loginSuccess;
    int length = snprintf(reply, size, "HTTP/1.1 %s\r\nContent-Length: %u\r\n\r\n%s",
        (!password && reject) ? "401 Unauthorized" : "200 OK", (unsigned)strlen(body), body);
    CHECK(raLoginReplyDecode(reply, length, 0));
    return strlen(reply);
}
static void setup(raConfig* cfg, int token, int password) {
    reset();
    memset(cfg, 0, sizeof(*cfg));
    strcpy(cfg->username, "testuser");
    if (token) strcpy(cfg->token, "faketoken0123");
    if (password) strcpy(cfg->password, "fakepass");
    requests = passwordRequests = tokenRequests = reject = transportError = 0;
    logs[0] = 0;
    rejection = invalid;
    loginSuccess = success;
}
static void safe(const raConfig* cfg) {
    CHECK(!strstr(logs, "fakepass") && !strstr(logs, "faketoken0123"));
    for (unsigned i = 0; i < sizeof(cfg->password); ++i) CHECK(!cfg->password[i]);
}
static void testLogin(void) {
    raConfig cfg;
    char user[33], token[64];
    const raLoginOps ops = {0, request, logLine, &io};
    setup(&cfg, 1, 1);
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(requests == 1 && tokenRequests == 1 && !passwordRequests && !targetWrites);
    CHECK(!strcmp(user, "TestUser") && !strcmp(token, "faketoken0123"));
    safe(&cfg);
    setup(&cfg, 1, 0);
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(requests == 1 && !passwordRequests);
    safe(&cfg);
    setup(&cfg, 0, 1);
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(requests == 1 && passwordRequests == 1 && !tokenRequests);
    CHECK(files[0].size == sizeof(next) - 1 && !memcmp(files[0].data, next, sizeof(next) - 1));
    safe(&cfg);
    setup(&cfg, 1, 1); reject = 1;
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(requests == 2 && tokenRequests == 1 && passwordRequests == 1);
    safe(&cfg);
    setup(&cfg, 1, 0); reject = 1;
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_NEEDS_PASSWORD);
    CHECK(requests == 1 && !passwordRequests && !token[0]);
    CHECK(strstr(logs, "password= back in ra.cfg for one boot"));
    safe(&cfg);
    const char* rejected[] = {
        "{\"Success\":false,\"Code\":\"expired_token\"}",
        "{\"Success\":false,\"Code\":\"invalid_credentials\"}",
        "{\"Success\":false,\"Error\":\"Invalid token\"}"
    };
    for (unsigned i = 0; i < sizeof(rejected) / sizeof(*rejected); ++i) {
        setup(&cfg, 1, 1); reject = 1; rejection = rejected[i];
        CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
        CHECK(requests == 2 && passwordRequests == 1);
        safe(&cfg);
        setup(&cfg, 1, 0); reject = 1; rejection = rejected[i];
        CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_NEEDS_PASSWORD);
        CHECK(requests == 1 && !passwordRequests);
        safe(&cfg);
    }
    setup(&cfg, 1, 1); reject = 2;
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_FAILED);
    CHECK(requests == 1 && !passwordRequests);
    safe(&cfg);
    setup(&cfg, 1, 1); reject = 3;
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_FAILED);
    CHECK(requests == 2 && tokenRequests == 1 && passwordRequests == 1 && !targetWrites && !token[0]);
    safe(&cfg);
    setup(&cfg, 0, 1); targetError = 1;
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(strstr(logs, "password is still on the card") && token[0]);
    CHECK(files[2].exists);
    safe(&cfg);
    targetError = 0;
    CHECK(raFileRecover(path, scratch, sizeof(scratch), &io));
    CHECK(files[0].size == sizeof(next) - 1 && !memcmp(files[0].data, next, sizeof(next) - 1));
    setup(&cfg, 0, 1); journalError = 1;
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(!memcmp(files[0].data, old, sizeof(old) - 1));
    safe(&cfg);
    setup(&cfg, 1, 1); transportError = 1;
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_FAILED);
    CHECK(requests == 1 && !passwordRequests);
    safe(&cfg);
    setup(&cfg, 0, 0);
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_FAILED && !requests);
    safe(&cfg);
    char masked[] = "testuser:fakepass/faketoken0123/*";
    raSecretMask(masked, "fakepass"); raSecretMask(masked, "faketoken0123"); raSecretMask(masked, "*");
    CHECK(!strstr(masked, "fakepass") && !strstr(masked, "faketoken0123"));
    raSecretMask(masked, "");
    puts("login: token preference, one password login, canonical user, rejected-token fallback, offline and rewrite failures passed");
    puts("login: canned failures echo both secrets; callback logs disclose neither; config password buffers cleared");
}
static void testMalformed(void) {
    const char* replies[] = {
        "{\"Success\":true,\"Token\":\"faketoken0123",
        "{\"Success\":true,\"Token\":\"faketoken0123!\"}",
        "{\"Success\":true,\"Token\":\"\"}",
        "{\"Success\":true,\"Token\":\"faketoken0123faketoken0123faketoken0123faketoken0123faketoken0123fake\"}"
    };
    raConfig cfg;
    char user[33], token[64];
    const raLoginOps ops = {0, request, logLine, &io};
    for (unsigned i = 0; i < sizeof(replies) / sizeof(*replies); ++i) {
        setup(&cfg, 0, 1);
        loginSuccess = replies[i];
        CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_FAILED);
        CHECK(!targetWrites && !token[0] && !cfg.token[0]);
        CHECK(!files[2].exists && files[0].size == sizeof(old) - 1);
        CHECK(!memcmp(files[0].data, old, sizeof(old) - 1));
        CHECK(strstr(logs, "login reply malformed; password kept"));
        safe(&cfg);
    }
    setup(&cfg, 0, 1);
    size_t n = (size_t)sprintf(files[0].data, "username=testuser\npassword=fakepass\n");
    memset(files[0].data + n, ' ', 188); n += 188;
    n += (size_t)sprintf(files[0].data + n, "token=\n");
    files[0].size = n;
    memcpy(scratch, files[0].data, n);
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(!targetWrites && !files[2].exists && files[0].size == n);
    CHECK(!memcmp(files[0].data, scratch, n));
    CHECK(strstr(logs, "password is still on the card"));
    safe(&cfg);
    setup(&cfg, 0, 1);
    n = (size_t)sprintf(files[0].data, "username=testuser\ntoken=\n#");
    memset(files[0].data + n, ' ', 190); n += 190;
    n += (size_t)sprintf(files[0].data + n, "password=fakepass\n");
    files[0].size = n;
    memcpy(scratch, files[0].data, n);
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(!targetWrites && !files[2].exists && files[0].size == n);
    CHECK(!memcmp(files[0].data, scratch, n));
    CHECK(strstr(logs, "password is still on the card"));
    safe(&cfg);
    setup(&cfg, 1, 1); reject = 1;
    rejection = "{\"Success\":false,\"Code\":\"invalid_token";
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_FAILED);
    CHECK(requests == 1 && !passwordRequests && !targetWrites);
    safe(&cfg);
    setup(&cfg, 1, 0);
    loginSuccess = "{\"Success\":true,\"Token\":\"faketoken0123\",\"User\":\"testuser";
    CHECK(raLogin(&cfg, path, user, token, &ops) == RA_LOGIN_OK);
    CHECK(!strcmp(user, "testuser"));
    safe(&cfg);
    puts("login validation: malformed Token leaves cfg untouched; long-prefix token and hidden effective password refuse writes; malformed Code prevents fallback, malformed User uses configured name");
}
static char* formatSecrets(const char* fmt, ...) {
    const char* secrets[] = {"fakepass", "faketoken0123"};
    va_list args;
    va_start(args, fmt);
    char* text = raSecretFormat(fmt, args, secrets, 2);
    va_end(args);
    return text;
}
static void testLogBoundary(void) {
    char prefix[189];
    memset(prefix, 'x', sizeof(prefix) - 1); prefix[sizeof(prefix) - 1] = 0;
    char* text = formatSecrets("%s%s/%s", prefix, "fakepass", "faketoken0123");
    CHECK(text && strlen(text) > 192);
    CHECK(!strstr(text, "fakepass") && !strstr(text, "faketoken0123"));
    CHECK(!memcmp(text + 188, "********/*************", 21));
    size_t length = strlen(text);
    text[191] = 0;
    CHECK(!strcmp(text + 188, "***"));
    raSecureClear(text, length); free(text);
    text = formatSecrets("context: %s%s", prefix, "faketoken0123");
    CHECK(text && !strstr(text, "faketoken0123") && text[197] == '*');
    raSecureClear(text, strlen(text)); free(text);
    puts("log helper: production formatter masks full secrets crossing 192 bytes before clipping; context formatting covered, hardware log/screen path not executed");
}
static void testJournal(void) {
    reset();
    CHECK(raConfigStoreToken(path, "faketoken0123", &io));
    int steps = step;
    complete();
    for (int cut = 1; cut <= steps; ++cut) {
        reset(); crash = cut;
        if (!setjmp(powerCut)) raConfigStoreToken(path, "faketoken0123", &io);
        crash = 0;
        recoverCuts();
    }
    for (int partial = 1; partial <= 3; ++partial) {
        reset(); shortWrite = partial;
        CHECK(!raConfigStoreToken(path, "faketoken0123", &io));
        shortWrite = 0;
        recoverCuts();
    }
    reset(); readError = 1;
    CHECK(!raConfigStoreToken(path, "faketoken0123", &io));
    CHECK(!targetWrites);
    reset(); files[0].size = RA_CFG_FILE_MAX + 1;
    CHECK(!raConfigStoreToken(path, "faketoken0123", &io) && !targetWrites);
    reset();
    const char* noToken = "# same\r\nusername=testuser\r\npassword=fakepass\r\nunknown=leave";
    const char* appended = "# same\r\nusername=testuser\r\npassword=\r\nunknown=leave\ntoken=faketoken0123\n";
    files[0].size = strlen(noToken); memcpy(files[0].data, noToken, files[0].size);
    CHECK(raConfigStoreToken(path, "faketoken0123", &io));
    CHECK(files[0].size == strlen(appended) && !memcmp(files[0].data, appended, strlen(appended)));
    int growthSteps = step;
    for (int cut = 1; cut <= growthSteps; ++cut) {
        reset();
        wantedOld = noToken; wantedNext = appended;
        files[0].size = strlen(noToken); memcpy(files[0].data, noToken, files[0].size);
        crash = cut;
        if (!setjmp(powerCut)) raConfigStoreToken(path, "faketoken0123", &io);
        crash = 0;
        recoverCuts();
    }
    printf("config journal: crashes at all %d model steps and every recovery step leave complete old/new cfg; deleted journal bytes retained and zero; comments, unknown lines and mixed endings preserved, passwords removed\n", steps);
}
static void testBoot(void) {
    char directory[] = "/tmp/ra-config-boot.XXXXXX", cwd[1024];
    CHECK(getcwd(cwd, sizeof(cwd)) != 0 && mkdtemp(directory) != 0);
    CHECK(chdir(directory) == 0);
    FILE* f = fopen(path, "wb");
    CHECK(f != 0 && fputs(old, f) >= 0 && !fclose(f));
    struct stat before, after;
    CHECK(stat(path, &before) == 0);
    reset(); targetError = 1;
    CHECK(!raConfigStoreToken(path, "faketoken0123", &io));
    f = fopen("cfg.txn", "wb");
    CHECK(f != 0 && fwrite(files[2].data, 1, files[2].size, f) == files[2].size && !fclose(f));
    CHECK(raConfigRecover(path));
    CHECK(stat(path, &after) == 0 && after.st_ino == before.st_ino && after.st_size == sizeof(next) - 1);
    raConfig cfg;
    CHECK(raConfigRead(path, &cfg) && cfg.usable && cfg.token[0] && !cfg.password[0]);
    CHECK(access("cfg.txn", F_OK) != 0);
    const char tailPassword[] = "username=testuser\ntoken=\npassword=fakepass\npassword=fakepass\npassword=fakepass\npassword=fakepass\n";
    f = fopen(path, "wb");
    CHECK(f && fwrite(tailPassword, 1, sizeof(tailPassword) - 1, f) == sizeof(tailPassword) - 1 && !fclose(f));
    raQueueFileOps observed = raQueueStdFileOps;
    observed.remove = observedRemove;
    CHECK(raConfigStoreToken(path, "faketoken0123", &observed));
    CHECK(tailChecks >= 2 && journalChecks >= 1);
    CHECK(raConfigRead(path, &cfg) && !strcmp(cfg.token, "faketoken0123") && !cfg.password[0]);
    CHECK(!remove(path) && !chdir(cwd) && !rmdir(directory));
    puts("config stdio: redo precedes read, inode retained; shortening tails zero and synced before truncate; journals zero before remove; token-only config usable");
}
static void testHttp(void) {
    char reply[256];
    const char* body = "{\"Success\":true,\"Token\":\"faketoken0123\"}";
    int length = snprintf(reply, sizeof(reply), "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n%s",
                          (unsigned)strlen(body), body);
    CHECK(!raLoginReplyDecode(reply, length - 1, 1));
    CHECK(raLoginReplyDecode(reply, length, 0));
    CHECK(!strcmp(reply, body));
    strcpy(reply, "HTTP/1.1 503 Error\r\nContent-Length: 2\r\n\r\n{}");
    CHECK(!raLoginReplyDecode(reply, strlen(reply), 1));
    strcpy(reply, "HTTP/1.1 401 Unauthorized\r\nTransfer-Encoding: chunked\r\n\r\n2\r\n{}\r\n0\r\n\r\n");
    CHECK(raLoginReplyDecode(reply, strlen(reply), 0));
    CHECK(!strcmp(reply, "{}"));
    puts("login HTTP: complete 200/401 and chunked bodies decoded; truncated and 503 replies rejected");
}
int main(void) {
    testLogin(); testMalformed(); testLogBoundary(); testJournal(); testBoot(); testHttp();
    printf("token login: PASSED (%u checks)\n", checks);
    return 0;
}
