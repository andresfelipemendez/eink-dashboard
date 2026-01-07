#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <pthread.h>
#include <errno.h>
#include <libwebsockets.h>

#include "app.h"

#define APP_LIB "./app.so"
#define WATCH_DIR "./src"
#define PORT 8080
#define MAX_TIME_CLIENTS 64
#define MAX_LIVERELOAD_CLIENTS 64

static volatile int g_running = 1;
static volatile int g_reload_requested = 0;
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Hot-reloadable app */
static void *g_handle = NULL;
static AppAPI *g_api = NULL;

/* Server state - owned by loader, persists across reloads */
static struct lws_context *g_ctx = NULL;

/* Time WebSocket clients */
static struct lws *g_time_clients[MAX_TIME_CLIENTS];
static int g_time_client_count = 0;

/* Livereload WebSocket clients */
static struct lws *g_livereload_clients[MAX_LIVERELOAD_CLIENTS];
static int g_livereload_client_count = 0;

/* Timer for pushing time */
static struct lws_sorted_usec_list g_time_sul;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Push time to all connected clients */
static void push_time_to_clients(void) {
    if (!g_api || !g_api->get_time || g_time_client_count == 0) return;

    const char *time_str = g_api->get_time();
    if (!time_str) return;

    size_t len = strlen(time_str);
    unsigned char buf[LWS_PRE + 128];
    memcpy(&buf[LWS_PRE], time_str, len);

    for (int i = 0; i < g_time_client_count; i++) {
        if (g_time_clients[i]) {
            lws_write(g_time_clients[i], &buf[LWS_PRE], len, LWS_WRITE_TEXT);
        }
    }
}

/* Timer callback - runs every second */
static void time_timer_cb(struct lws_sorted_usec_list *sul) {
    push_time_to_clients();
    /* Reschedule for next second */
    lws_sul_schedule(g_ctx, 0, sul, time_timer_cb, LWS_US_PER_SEC);
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
        if (g_api && g_api->handle_http) {
            body = g_api->handle_http(&req);
        }
        if (!body) {
            body = "<html><body><h1>No handler</h1></body></html>";
        }

        size_t body_len = strlen(body);
        unsigned char buf[LWS_PRE + 4096];
        unsigned char *p = buf + LWS_PRE;
        unsigned char *end = buf + sizeof(buf);

        if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK, "text/html",
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

/* Close all livereload clients to trigger browser refresh */
static void close_livereload_clients(void) {
    for (int i = 0; i < g_livereload_client_count; i++) {
        if (g_livereload_clients[i]) {
            lws_close_reason(g_livereload_clients[i], LWS_CLOSE_STATUS_GOINGAWAY, NULL, 0);
            lws_set_timeout(g_livereload_clients[i], PENDING_TIMEOUT_CLOSE_SEND, 1);
        }
    }
    g_livereload_client_count = 0;
}

/* Livereload WebSocket */
static int callback_livereload(struct lws *wsi, enum lws_callback_reasons reason,
                               void *user, void *in, size_t len) {
    (void)user; (void)in; (void)len;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        if (g_livereload_client_count < MAX_LIVERELOAD_CLIENTS) {
            g_livereload_clients[g_livereload_client_count++] = wsi;
            printf("[livereload] client connected (%d total)\n", g_livereload_client_count);
            fflush(stdout);
        }
        break;

    case LWS_CALLBACK_CLOSED:
        for (int i = 0; i < g_livereload_client_count; i++) {
            if (g_livereload_clients[i] == wsi) {
                g_livereload_clients[i] = g_livereload_clients[--g_livereload_client_count];
                break;
            }
        }
        printf("[livereload] client disconnected (%d total)\n", g_livereload_client_count);
        fflush(stdout);
        break;

    default:
        break;
    }
    return 0;
}

/* Time WebSocket - pushes server time every second */
static int callback_time(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len) {
    (void)user; (void)in; (void)len;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        if (g_time_client_count < MAX_TIME_CLIENTS) {
            g_time_clients[g_time_client_count++] = wsi;
            printf("[time] client connected (%d total)\n", g_time_client_count);
            fflush(stdout);
        }
        break;

    case LWS_CALLBACK_CLOSED:
        /* Remove from list */
        for (int i = 0; i < g_time_client_count; i++) {
            if (g_time_clients[i] == wsi) {
                g_time_clients[i] = g_time_clients[--g_time_client_count];
                break;
            }
        }
        printf("[time] client disconnected (%d total)\n", g_time_client_count);
        fflush(stdout);
        break;

    default:
        break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    {"http-only", callback_http, 0, 0, 0, NULL, 0},
    {"livereload", callback_livereload, 0, 0, 0, NULL, 0},
    {"time", callback_time, 0, 128, 0, NULL, 0},
    {NULL, NULL, 0, 0, 0, NULL, 0}
};

static int rebuild_app(void) {
    printf("[loader] rebuilding app.so...\n");
    fflush(stdout);
    int ret = system("tcc -shared -fPIC -o app.so src/app.c");
    return ret == 0;
}

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

/* File watcher thread */
static void *watcher_thread(void *arg) {
    (void)arg;

    int inotify_fd = inotify_init1(0);
    if (inotify_fd < 0) {
        perror("inotify_init1");
        return NULL;
    }

    int wd = inotify_add_watch(inotify_fd, WATCH_DIR, IN_CLOSE_WRITE);
    if (wd < 0) {
        perror("inotify_add_watch");
        close(inotify_fd);
        return NULL;
    }

    printf("[watcher] monitoring %s\n", WATCH_DIR);
    fflush(stdout);

    while (g_running) {
        char buf[4096];
        ssize_t len = read(inotify_fd, buf, sizeof(buf));
        if (len > 0 && g_running) {
            printf("[watcher] file changed\n");
            fflush(stdout);
            pthread_mutex_lock(&g_mutex);
            g_reload_requested = 1;
            if (g_ctx) {
                lws_cancel_service(g_ctx);
            }
            pthread_mutex_unlock(&g_mutex);
        }
    }

    close(inotify_fd);
    return NULL;
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

    /* Start time push timer */
    lws_sul_schedule(g_ctx, 0, &g_time_sul, time_timer_cb, LWS_US_PER_SEC);

    printf("[loader] server listening on port %d\n", PORT);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Start file watcher */
    pthread_t watcher;
    if (pthread_create(&watcher, NULL, watcher_thread, NULL) != 0) {
        perror("pthread_create");
        return 1;
    }

    /* Initial build and load */
    if (!rebuild_app()) {
        fprintf(stderr, "[loader] initial build failed\n");
        return 1;
    }

    g_handle = load_library(APP_LIB);
    if (!g_handle) return 1;

    g_api = get_api(g_handle);
    if (!g_api) return 1;

    /* Create server - once, persists across reloads */
    if (server_init() != 0) {
        return 1;
    }

    printf("[loader] ready (ctrl-c to quit)\n");
    fflush(stdout);

    /* Main loop */
    while (g_running) {
        /* Check for reload */
        pthread_mutex_lock(&g_mutex);
        int do_reload = g_reload_requested;
        g_reload_requested = 0;
        pthread_mutex_unlock(&g_mutex);

        if (do_reload) {
            printf("[loader] reloading handlers...\n");
            fflush(stdout);

            if (rebuild_app()) {
                dlclose(g_handle);
                g_handle = load_library(APP_LIB);
                if (g_handle) {
                    g_api = get_api(g_handle);
                    if (g_api && g_api->on_reload) {
                        g_api->on_reload();
                    }
                    /* Close livereload clients to trigger browser refresh */
                    close_livereload_clients();
                    printf("[loader] reloaded\n");
                    fflush(stdout);
                }
            } else {
                fprintf(stderr, "[loader] rebuild failed\n");
            }
        }

        /* Service network */
        lws_service(g_ctx, 1);
    }

    /* Shutdown */
    g_running = 0;
    lws_sul_cancel(&g_time_sul);
    if (g_ctx) {
        lws_context_destroy(g_ctx);
    }
    if (g_handle) {
        dlclose(g_handle);
    }

    pthread_cancel(watcher);
    pthread_join(watcher, NULL);

    printf("[loader] shutdown\n");
    return 0;
}
