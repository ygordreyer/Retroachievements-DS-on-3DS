#include "ra_login.h"
#if RA_LAUNCHER_WIFI
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void raSecretMask(char* text, const char* secret) {
    size_t size = secret ? strlen(secret) : 0;
    char* at;
    if (!size) return;
    while ((at = strstr(text, secret)) != 0) {
        memset(at, '*', size);
        text = at + size;
    }
}

char* raSecretFormat(const char* fmt, va_list args, const char* const* secrets, size_t count) {
    va_list copy;
    char* text;
    int length;
    va_copy(copy, args);
    length = vsnprintf(0, 0, fmt, copy);
    va_end(copy);
    if (length < 0) return 0;
    text = malloc((size_t)length + 1);
    if (!text) return 0;
    if (vsnprintf(text, (size_t)length + 1, fmt, args) != length) {
        raSecureClear(text, (size_t)length + 1);
        free(text);
        return 0;
    }
    for (size_t i = 0; i < count; ++i) raSecretMask(text, secrets[i]);
    return text;
}

typedef struct raLoginBody { char* text; size_t used; } raLoginBody;
static void loginBody(void* context, const char* text, int length) {
    raLoginBody* out = context;
    memmove(out->text + out->used, text, (size_t)length);
    out->used += length;
}

/* Authentication failures may use HTTP 401; incomplete bodies never authenticate. */
int raLoginReplyDecode(char* response, int length, int closedByPeer) {
    raNetStream stream;
    raLoginBody out = {response, 0};
    if (length <= 0) return 0;
    raNetStreamReset(&stream, 0, 0);
    raNetStreamFeed(&stream, response, length);
    if ((stream.status != 200 && stream.status != 401)
     || !(stream.chunked ? stream.done : stream.state == RA_NET_STREAM_BODY
          && (stream.contentLength ? stream.bodyBytes == stream.contentLength : closedByPeer))) {
        raSecureClear(&stream, sizeof(stream));
        return 0;
    }
    raNetStreamReset(&stream, loginBody, &out);
    raNetStreamFeed(&stream, response, length);
    response[out.used] = 0;
    raSecureClear(&stream, sizeof(stream));
    return 1;
}

static int loginRequest(const raConfig* cfg, int password, char* path, size_t size) {
    char user[3 * sizeof(cfg->username)], secret[3 * sizeof(cfg->password)];
    int ok = raNetUrlEncode(cfg->username, user, sizeof(user))
        && raNetUrlEncode(password ? cfg->password : cfg->token, secret, sizeof(secret));
    if (ok) {
        int count = sniprintf(path, size, "/dorequest.php?r=login2&u=%s&%c=%s",
                             user, password ? 'p' : 't', secret);
        ok = count >= 0 && (size_t)count < size;
    }
    raSecureClear(secret, sizeof(secret));
    raSecureClear(user, sizeof(user));
    return ok;
}

static int invalidToken(const char* response) {
    char code[64], error[128];
    int invalid = (raNetJsonString(response, "Code", code, sizeof(code))
                  && (!strcmp(code, "invalid_token") || !strcmp(code, "expired_token")
                      || !strcmp(code, "invalid_credentials")))
        || (raNetJsonString(response, "Error", error, sizeof(error))
            && (!strcasecmp(error, "Invalid token") || !strcasecmp(error, "Invalid token.")
                || !strcasecmp(error, "The access token has expired.")));
    raSecureClear(code, sizeof(code));
    raSecureClear(error, sizeof(error));
    return invalid;
}

int raLogin(raConfig* cfg, const char* path, char user[33], char token[64], const raLoginOps* ops) {
    char request[384], response[2048];
    int password = !cfg->token[0], result = RA_LOGIN_FAILED;
    raSecureClear(user, 33);
    raSecureClear(token, 64);
    if (!cfg->username[0] || (!cfg->token[0] && !cfg->password[0])) {
        ops->log(ops->context, "ra.cfg needs username and token or password; login skipped");
        goto done;
    }
    for (;;) {
        int got;
        if (!loginRequest(cfg, password, request, sizeof(request))) {
            ops->log(ops->context, "login credentials too long to encode");
            goto done;
        }
        ops->log(ops->context, password ? "signing in with password once" : "signing in with token");
        memset(response, 0, sizeof(response));
        got = ops->request(ops->context, request, response, sizeof(response));
        raSecureClear(request, sizeof(request));
        if (got < 0) {
            ops->log(ops->context, "login HTTP failed");
            goto done;
        }
        if (raNetJsonTrue(response, "Success")) {
            if (!raNetJsonString(response, "Token", token, 64)
             || strspn(token, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789") != strlen(token)) {
                raSecureClear(token, 64);
                ops->log(ops->context, "login reply malformed; password kept");
                goto done;
            }
            if (!raNetJsonString(response, "User", user, 33) || !user[0])
                memcpy(user, cfg->username, sizeof(cfg->username));
            if (password) {
                if (raConfigStoreToken(path, token, ops->files))
                    ops->log(ops->context, "token saved; password removed from ra.cfg");
                else
                    ops->log(ops->context, "token save failed; password is still on the card; session continues");
            }
            memcpy(cfg->token, token, sizeof(cfg->token));
            ops->log(ops->context, "logged in; token set");
            result = RA_LOGIN_OK;
            goto done;
        }
        raSecureClear(token, 64);
        if (!password && invalidToken(response)) {
            if (cfg->password[0]) {
                ops->log(ops->context, "token rejected; using the saved password once");
                password = 1;
                raSecureClear(response, sizeof(response));
                continue;
            }
            ops->log(ops->context, "token rejected; put password= back in ra.cfg for one boot");
            result = RA_LOGIN_NEEDS_PASSWORD;
        } else {
            ops->log(ops->context, "login refused; check the account credentials");
        }
        break;
    }
done:
    raSecureClear(cfg->password, sizeof(cfg->password));
    raSecureClear(request, sizeof(request));
    raSecureClear(response, sizeof(response));
    return result;
}
#endif
