#include "app.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *get_time(void);

/* HTML page - no WebSockets (e-ink browser compatibility) */
static const char *HTML_PAGE =
    "<!DOCTYPE html><html><head><title>eink-dashboard</title></head>"
    "<body>"
    "<h1>eink-dashboard</h1>"
    "<div id='time' style='font-size:2em;font-family:monospace;'></div>"
    "<script>"
    "function updateTime(){"
    "  fetch('/time').then(r=>r.text()).then(t=>{"
    "    document.getElementById('time').textContent=t;"
    "  });"
    "}"
    "updateTime();"
    "setInterval(updateTime,1000);"
    "</script>"
    "</body></html>";

/* HTTP request handler */
static const char *handle_http(HttpRequest *req) {
    if (req->path_len >= 5 && strncmp(req->path, "/time", 5) == 0) {
        return get_time();
    }
    return HTML_PAGE;
}

/* Get current time string */
static const char *get_time(void) {
    static char buf[32];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    return buf;
}

/* Called after reload */
static void on_reload(void) {
    printf("[app] handlers reloaded\n");
}

/* API export */
static AppAPI api = {
    .handle_http = handle_http,
    .get_time = get_time,
    .on_reload = on_reload
};

AppAPI *app_api(void) {
    return &api;
}
