#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>
#include <unistd.h>
#include <libwebsockets.h>

#include "app.h"

#define APP_LIB "./app.so"
#define PORT 8080

static volatile int g_running = 1;

/* Hot-reloadable app */
static void *g_handle = NULL;
static AppAPI *g_api = NULL;

/* Server state */
static struct lws_context *g_ctx = NULL;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* HTTP callback - dispatches to app.so handler */
static int callback_http(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    (void)user;

    switch (reason) {
    case LWS_CALLBACK_HTTP: {
        HttpRequest req = {
            .wsi = wsi,
            .path = (const char *)in,
            .path_len = len
        };

        const char *body = NULL;
        const char *content_type = "text/html";

        /* Check if /time endpoint */
        if (len >= 5 && strncmp((const char *)in, "/time", 5) == 0) {
            if (g_api && g_api->get_time) {
                body = g_api->get_time();
            }
            content_type = "text/plain";
        } else {
            if (g_api && g_api->handle_http) {
                body = g_api->handle_http(&req);
            }
        }

        if (!body) {
            body = "error";
        }

        size_t body_len = strlen(body);
        unsigned char buf[LWS_PRE + 4096];
        unsigned char *p = buf + LWS_PRE;
        unsigned char *end = buf + sizeof(buf);

        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK, content_type,
                                        body_len, &p, end))
            return 1;
        if (lws_finalize_write_http_header(wsi, buf + LWS_PRE, &p, end))
            return 1;

        lws_write(wsi, (unsigned char *)body, body_len, LWS_WRITE_HTTP_FINAL);

        if (lws_http_transaction_completed(wsi))
            return -1;
        break;
    }
    default:
        break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    {"http-only", callback_http, 0, 0, 0, NULL, 0},
    {NULL, NULL, 0, 0, 0, NULL, 0}
};

static void *load_library(const char *path) {
    void *handle = dlopen(path, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
    }
    return handle;
}

static AppAPI *get_api(void *handle) {
    AppAPI *(*fn)(void) = dlsym(handle, "app_api");
    if (!fn) {
        fprintf(stderr, "dlsym: %s\n", dlerror());
        return NULL;
    }
    return fn();
}

static int server_init(void) {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = PORT;
    info.protocols = protocols;
    info.options = 0;

    g_ctx = lws_create_context(&info);
    if (!g_ctx) {
        fprintf(stderr, "[loader] failed to create server context\n");
        return -1;
    }

    printf("[loader] server listening on port %d\n", PORT);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_handle = load_library(APP_LIB);
    if (!g_handle) return 1;

    g_api = get_api(g_handle);
    if (!g_api) return 1;

    if (server_init() != 0) {
        return 1;
    }

    printf("[loader] ready\n");
    fflush(stdout);

    while (g_running) {
        lws_service(g_ctx, 1000);
    }

    if (g_ctx) {
        lws_context_destroy(g_ctx);
    }
    if (g_handle) {
        dlclose(g_handle);
    }

    printf("[loader] shutdown\n");
    return 0;
}
