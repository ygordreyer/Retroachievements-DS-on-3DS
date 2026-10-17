#ifndef RA_LOGIN_H
#define RA_LOGIN_H
#include "ra_wifi.h"
#include <stdarg.h>
#include "ra_queue_io.h"

typedef struct raLoginOps {
    void* context;
    int (*request)(void*, const char*, char*, int);
    void (*log)(void*, const char*);
    const raQueueFileOps* files;
} raLoginOps;

enum { RA_LOGIN_FAILED = 0, RA_LOGIN_OK = 1, RA_LOGIN_NEEDS_PASSWORD = -1 };
/* Always clears cfg->password and transient credentials, including on failure. */
int raLogin(raConfig* cfg, const char* path, char user[33], char token[64], const raLoginOps* ops);
int raConfigStoreToken(const char* path, const char* token, const raQueueFileOps* ops);
int raLoginReplyDecode(char* response, int length, int closedByPeer);
/* Caller securely clears and frees the complete masked result. */
char* raSecretFormat(const char* fmt, va_list args, const char* const* secrets, size_t count);
void raSecretMask(char* text, const char* secret);
#endif
