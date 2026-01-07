#include "app.h"
#include <stdio.h>
#include <time.h>

/* HTML page with livereload and time display */
static const char *HTML_PAGE =
    "<!DOCTYPE html><html><head><title>eink-dashboard</title></head>"
    "<body><h1>eink-dashboard reload</h1>"
    "<div id='time' style='font-size:2em;font-family:monospace;'></div>"
    "<script>"
    "(function(){"
    "  var p = location.protocol === 'https:' ? 'wss://' : 'ws://';"
    "  var lr = new WebSocket(p + location.host, 'livereload');"
    "  lr.onclose = function() { setTimeout(function() { location.reload(); }, 500); };"
    "  var ts = new WebSocket(p + location.host, 'time');"
    "  ts.onmessage = function(e) { document.getElementById('time').textContent = e.data; };"
    "})();"
    "</script>"
    "</body></html>";

/* HTTP request handler - just returns the page */
static const char *handle_http(HttpRequest *req) {
    (void)req;
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
