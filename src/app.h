#ifndef APP_H
#define APP_H

#include <libwebsockets.h>

/* HTTP request context passed to handlers */
typedef struct {
    struct lws *wsi;
    const char *path;
    size_t path_len;
} HttpRequest;

/* Hot-reloadable API - just handlers, no socket ownership */
typedef struct {
    /* Handle HTTP request, return response body */
    const char *(*handle_http)(HttpRequest *req);

    /* Get current time string to push to clients (static buffer ok) */
    const char *(*get_time)(void);

    /* Called after reload */
    void (*on_reload)(void);
} AppAPI;

/* Exported symbol from app.so */
AppAPI *app_api(void);

#endif
