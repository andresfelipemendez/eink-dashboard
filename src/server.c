#include "app.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *get_time(void);

/* HTML page with WebSocket clock */
static const char *HTML_PAGE =
    "<!DOCTYPE html><html><head><title>eink-dashboard</title></head>"
    "<body>"
    "<h1>eink-dashboard</h1>"
    "<p id=\"time\">Connecting...</p>"
    "<script>"
#ifdef PRODUCTION
    "var ws = new WebSocket('wss://' + location.host + '/ws', 'time-protocol');"
#else
    "var ws = new WebSocket('ws://' + location.host + '/ws', 'time-protocol');"
#endif
    "ws.onmessage = function(e) { document.getElementById('time').textContent = e.data; };"
    "ws.onclose = function() { document.getElementById('time').textContent = 'Disconnected'; };"
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
