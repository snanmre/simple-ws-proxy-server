// clang-format off
/*
    Simple Websocket proxy server

                                                                                                               
  ┌─────────────────┐                           ┌────────────┐   Remote WS client     ┌──────────────────────┐ 
  │                 │                           │            ├────────────────────────►         Remote       │ 
  │     POSTMAN     │                           │  WS Server │                        │                      │ 
  │    WS client    │                           │            │                        │       WS Server      │ 
  │                 ◄───────────────────────────┤            │                        │                      │ 
  └─────────────────┘            Local WS client└────────────┘                        └──────────────────────┘ 
                                                                                                               
*/
// clang-format on

#include <iostream>
#include <string>

#include "mongoose.h"

#define WS_LOCAL_SERVER_URL "ws://localhost:20000"
#define REMOTE_SERVER_URL "wss://echo.websocket.org/web1"

struct context {
    struct mg_mgr *mgr = NULL;

    struct mg_connection *server = NULL;
    struct mg_connection *local_client = NULL;
    struct mg_connection *remote_client = NULL;
    struct mg_timer *remote_reconnect_timer = NULL;
};

namespace {

void mgr_event_handler_fn(struct mg_connection *c, int ev, void *ev_data);

const char *to_string(int ev) {
    switch (ev) {
        case MG_EV_ERROR:
            return "MG_EV_ERROR";
        case MG_EV_OPEN:
            return "MG_EV_OPEN";
        case MG_EV_POLL:
            return "MG_EV_POLL";
        case MG_EV_RESOLVE:
            return "MG_EV_RESOLVE";
        case MG_EV_CONNECT:
            return "MG_EV_CONNECT";
        case MG_EV_ACCEPT:
            return "MG_EV_ACCEPT";
        case MG_EV_TLS_HS:
            return "MG_EV_TLS_HS";
        case MG_EV_READ:
            return "MG_EV_READ";
        case MG_EV_WRITE:
            return "MG_EV_WRITE";
        case MG_EV_CLOSE:
            return "MG_EV_CLOSE";
        case MG_EV_HTTP_MSG:
            return "MG_EV_HTTP_MSG";
        case MG_EV_WS_OPEN:
            return "MG_EV_WS_OPEN";
        case MG_EV_WS_MSG:
            return "MG_EV_WS_MSG";
        case MG_EV_WS_CTL:
            return "MG_EV_WS_CTL";
        case MG_EV_MQTT_CMD:
            return "MG_EV_MQTT_CMD";
        case MG_EV_MQTT_MSG:
            return "MG_EV_MQTT_MSG";
        case MG_EV_MQTT_OPEN:
            return "MG_EV_MQTT_OPEN";
        case MG_EV_SNTP_TIME:
            return "MG_EV_SNTP_TIME";
        case MG_EV_WAKEUP:
            return "MG_EV_WAKEUP";
        case MG_EV_USER:
            return "MG_EV_USER";
    }

    return "<NUL>";
}

void remote_client_event_handler_fn(struct mg_connection *c, int ev,
                                    void *ev_data) {
    context *ctxp = (context *)c->fn_data;

    if (ev != MG_EV_POLL) {
        MG_INFO(("event = %s", to_string(ev)));
    }

    if (ev == MG_EV_CONNECT) {
        MG_INFO(("Remote client connected."));
        struct mg_tls_opts opts = {.name = mg_str("echo.websocket.org")};
        mg_tls_init(c, &opts);
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        MG_INFO(("Message from remote: %.*s", wm->data.len, wm->data.ptr));
        if (ctxp->local_client) {
            size_t rc = mg_ws_send(ctxp->local_client, wm->data.ptr,
                                   wm->data.len, WEBSOCKET_OP_TEXT);
            MG_INFO(("message redirected. rc = %u", rc));
        } else {
            MG_ERROR(("ctx->local_client is NULL!"));
        }
    } else if (ev == MG_EV_CLOSE) {
        MG_INFO(("Remote client disconnected."));
        ctxp->remote_client = NULL;
    }
}

void local_client_event_handler_fn(struct mg_connection *c, int ev,
                                   void *ev_data) {
    context *ctxp = (context *)c->fn_data;

    if (ev != MG_EV_POLL) {
        MG_INFO(("event = %s", to_string(ev)));
    }
    if (ev == MG_EV_OPEN) {
    } else if (ev == MG_EV_WS_OPEN) {
        ctxp->local_client = c;

        MG_INFO(("Local client connected."));

        // reconnection setup
        ctxp->remote_reconnect_timer = mg_timer_add(
            ctxp->mgr, 5000, MG_TIMER_REPEAT | MG_TIMER_RUN_NOW,
            [](void *arg) {
                context *ctxp = (context *)arg;

                // MG_INFO(("timer called! %p", ctxp->remote_client));

                if (!ctxp->remote_client) {
                    ctxp->remote_client =
                        mg_ws_connect(ctxp->mgr, REMOTE_SERVER_URL,
                                      mgr_event_handler_fn, ctxp, NULL);
                    if (!ctxp->remote_client) {
                        MG_ERROR(("mg_ws_connect() failed"));
                    }
                }
            },
            ctxp);

    } else if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        mg_ws_upgrade(c, hm, NULL);
    } else if (ev == MG_EV_WS_MSG) {
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        // mg_ws_send(c, wm->data.ptr, wm->data.len, WEBSOCKET_OP_TEXT);
        if (ctxp->remote_client) {
            mg_ws_send(ctxp->remote_client, wm->data.ptr, wm->data.len,
                       WEBSOCKET_OP_TEXT);
        }
    } else if (ev == MG_EV_CLOSE) {
        ctxp->local_client = NULL;
        MG_INFO(("Local client disconnected."));

        mg_timer_free(&ctxp->mgr->timers, ctxp->remote_reconnect_timer);
        free(ctxp->remote_reconnect_timer);
        ctxp->remote_reconnect_timer = NULL;

        // close remote connection
        ctxp->remote_client->is_draining = 1;
    }
}

void local_server_event_handler_fn(struct mg_connection *c, int ev,
                                   void *ev_data) {
    context *ctxp = (context *)c->fn_data;

    if (ev != MG_EV_POLL) {
        MG_INFO(("event = %s", to_string(ev)));
    }
}

///////////////////////////////////////////////////////////////////////////////

void mgr_event_handler_fn(struct mg_connection *c, int ev, void *ev_data) {

    if (c->is_listening) {
        local_server_event_handler_fn(c, ev, ev_data);
    } else if (c->is_accepted) {
        local_client_event_handler_fn(c, ev, ev_data);
    } else {
        remote_client_event_handler_fn(c, ev, ev_data);
    }
}
}  // namespace

int main(int, char **) {
    mg_log_set(MG_LL_INFO);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);

    context ctx;
    ctx.mgr = &mgr;

    printf("Starting WS listener on %s/websocket\n", WS_LOCAL_SERVER_URL);
    ctx.server = NULL;
    mg_http_listen(&mgr, WS_LOCAL_SERVER_URL, &mgr_event_handler_fn, &ctx);

    while (true) {
        mg_mgr_poll(&mgr, 200);
    }

    mg_mgr_free(&mgr);

    return EXIT_SUCCESS;
}
