/*
 * Copyright (c) 2014 Nicolas Martyanoff
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <unistd.h>

#include "http.h"
#include "internal.h"

static void http_connection_process_msg(struct http_connection *,
                                        struct http_msg *);

struct http_connection *
http_connection_setup(struct http_server *server, int sock) {
    struct http_connection *connection;

    connection = http_malloc(sizeof(struct http_connection));
    memset(connection, 0, sizeof(struct http_connection));

    connection->server = server;
    connection->sock = sock;

    connection->ev_read = event_new(server->ev_base, connection->sock,
                                    EV_READ | EV_PERSIST,
                                    http_connection_on_read_event,
                                    connection);
    if (!connection->ev_read) {
        http_set_error("cannot create read event handler: %s",
                       strerror(errno));
        goto error;
    }

    if (event_add(connection->ev_read, NULL) == -1) {
        http_set_error("cannot add read event handler: %s", strerror(errno));
        goto error;
    }

    connection->ev_write = event_new(server->ev_base, connection->sock,
                                     EV_WRITE | EV_PERSIST,
                                     http_connection_on_write_event,
                                     connection);
    if (!connection->ev_write) {
        http_set_error("cannot create write event handler: %s",
                       strerror(errno));
    }

    connection->rbuf = bf_buffer_new(0);
    if (!connection->rbuf) {
        http_set_error("%s", bf_get_error());
        goto error;
    }

    connection->wbuf = bf_buffer_new(0);
    if (!connection->wbuf) {
        http_set_error("%s", bf_get_error());
        goto error;
    }

    if (http_parser_init(&connection->parser, HTTP_MSG_REQUEST,
                         &server->cfg) == -1) {
        goto error;
    }

    connection->parser.msg.type = HTTP_MSG_REQUEST;
    connection->parser.cfg = &server->cfg;

    connection->http_version = HTTP_1_1;

    if (http_now_ms(&connection->last_activity) == -1)
        goto error;

    return connection;

error:
    http_connection_close(connection);
    return NULL;
}

void
http_connection_check_for_timeout(struct http_connection *connection,
                                  uint64_t now) {
    uint64_t diff;

    diff = now - connection->last_activity;
    if (diff > connection->server->cfg.connection_timeout) {
        http_connection_http_error(connection, HTTP_REQUEST_TIMEOUT);
        http_connection_shutdown(connection);
    }
}

int
http_connection_write(struct http_connection *connection,
                      const void *data, size_t sz) {
    size_t previous_length;

    previous_length = bf_buffer_length(connection->wbuf);

    if (bf_buffer_add(connection->wbuf, data, sz) == -1) {
        http_set_error("%s", bf_get_error());
        return -1;
    }

    if (previous_length == 0) {
        if (event_add(connection->ev_write, NULL) == -1) {
            http_set_error("cannot add write event handler: %s",
                           strerror(errno));
            bf_buffer_truncate(connection->wbuf, previous_length);
            return -1;
        }
    }

    return 0;
}

int
http_connection_printf(struct http_connection *connection,
                                   const char *fmt, ...) {
    size_t previous_length;
    va_list ap;

    previous_length = bf_buffer_length(connection->wbuf);

    va_start(ap, fmt);
    if (bf_buffer_add_vprintf(connection->wbuf, fmt, ap) == -1) {
        http_set_error("%s", bf_get_error());
        return -1;
    }
    va_end(ap);

    if (previous_length == 0) {
        if (event_add(connection->ev_write, NULL) == -1) {
            http_set_error("cannot add write event handler: %s",
                           strerror(errno));
            bf_buffer_truncate(connection->wbuf, previous_length);
            return -1;
        }
    }

    return 0;
}

int
http_connection_http_error(struct http_connection *connection,
                           enum http_status_code status_code) {
    const char *reason_phrase;
    char body[1024];
    char length_str[32];
    size_t body_length;

    reason_phrase = http_status_code_to_reason_phrase(status_code);
    snprintf(body, sizeof(body), "<h1>%d %s</h1>\n",
             status_code, reason_phrase);
    body_length = strlen(body);

    snprintf(length_str, sizeof(length_str), "%zu", body_length);

    if (http_connection_write_response(connection,
                                       status_code, reason_phrase) == -1) {
        http_connection_error(connection, "%s", strerror(errno));
        http_connection_close(connection);
        return -1;
    }

    if (http_connection_write_header(connection,
                                     "Content-Type", "text/html") == -1) {
        return -1;
    }

    if (http_connection_write_header(connection,
                                     "Content-Length", length_str) == -1) {
        return -1;
    }

    if (http_connection_write_body(connection, body, body_length) == -1)
        return -1;

    return 0;
}

void
http_connection_close(struct http_connection *connection) {
    if (!connection)
        return;

    if (connection->sock >= 0) {
        ht_table_remove(connection->server->connections,
                        HT_INT32_TO_POINTER(connection->sock));

        close(connection->sock);
        connection->sock = -1;
    }

    if (connection->ev_read)
        event_free(connection->ev_read);
    if (connection->ev_write)
        event_free(connection->ev_write);

    bf_buffer_delete(connection->rbuf);
    bf_buffer_delete(connection->wbuf);

    http_parser_free(&connection->parser);

    memset(connection, 0, sizeof(struct http_connection));
    http_free(connection);
}

int
http_connection_shutdown(struct http_connection *connection) {
    if (event_del(connection->ev_read) == -1) {
        http_set_error("cannot remove read event handler: %s",
                       strerror(errno));
        http_connection_close(connection);
        return -1;
    }

    if (shutdown(connection->sock, SHUT_RD) == -1) {
        http_set_error("cannot shutdown socket: %s", strerror(errno));
        http_connection_close(connection);
        return -1;
    }

    connection->shutting_down = true;
    return 0;
}

void
http_connection_on_read_event(evutil_socket_t sock, short events, void *arg) {
    struct http_connection *connection;
    const struct http_cfg *cfg;
    ssize_t ret;

    connection = arg;
    cfg = &connection->server->cfg;

    ret = bf_buffer_read(connection->rbuf, connection->sock, BUFSIZ);
    if (ret == -1) {
        http_connection_error(connection, "cannot read socket: %s",
                              strerror(errno));
        http_connection_close(connection);
        return;
    }

    if (ret == 0) {
        http_connection_close(connection);
        return;
    }

    for (;;) {
        struct http_parser *parser;
        struct http_msg *msg;
        int ret;

        parser = &connection->parser;
        msg = &parser->msg;

        ret = http_msg_parse(connection->rbuf, parser);
        if (ret == -1) {
            http_connection_error(connection, "cannot parse request: %s",
                                  http_get_error());
            http_connection_http_error(connection,
                                       HTTP_INTERNAL_SERVER_ERROR);
            http_connection_shutdown(connection);
            return;
        }

        if (ret == 0)
            break;

        if (parser->state == HTTP_PARSER_ERROR) {
            http_connection_error(connection, "cannot parse request: %s",
                                   parser->errmsg);
            http_connection_http_error(connection, parser->status_code);
            http_connection_shutdown(connection);
            return;
        } else if (parser->state == HTTP_PARSER_DONE) {
            if (cfg->request_hook)
                cfg->request_hook(connection, msg, cfg->hook_arg);

            http_connection_process_msg(connection, msg);

            if (http_parser_reset(parser, HTTP_MSG_REQUEST, cfg) == -1) {
                http_connection_error(connection, "cannot reset parser: %s",
                                      http_get_error());
                http_connection_close(connection);
                return;
            }
        }
    }

    if (http_now_ms(&connection->last_activity) == -1)
        http_connection_error(connection, "%s", http_get_error());
}

void
http_connection_on_write_event(evutil_socket_t sock, short events, void *arg) {
    struct http_connection *connection;
    ssize_t ret;

    connection = arg;

    ret = bf_buffer_write(connection->wbuf, connection->sock);
    if (ret == -1) {
        http_connection_error(connection, "cannot write to socket: %s",
                              strerror(errno));
        http_connection_close(connection);
        return;
    }

    bf_buffer_skip(connection->wbuf, (size_t)ret);
    if (bf_buffer_length(connection->wbuf) == 0) {
        event_del(connection->ev_write);

        if (connection->shutting_down) {
            http_connection_close(connection);
            return;
        }
    }
}

void
http_connection_error(struct http_connection *connection,
                      const char *fmt, ...) {
    char buf[HTTP_ERROR_BUFSZ];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, HTTP_ERROR_BUFSZ, fmt, ap);
    va_end(ap);

    http_server_error(connection->server, "%s:%s: %s",
                      connection->host, connection->port, buf);
}

void
http_connection_trace(struct http_connection *connection,
                      const char *fmt, ...) {
    char buf[HTTP_ERROR_BUFSZ];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, HTTP_ERROR_BUFSZ, fmt, ap);
    va_end(ap);

    http_server_trace(connection->server, "%s:%s: %s",
                      connection->host, connection->port, buf);
}

int
http_connection_write_response(struct http_connection *connection,
                               enum http_status_code status_code,
                               const char *reason_phrase) {
    const char *version_str;

    version_str = http_version_to_string(connection->http_version);
    if (!version_str) {
        http_set_error("unknown http version %d", connection->http_version);
        return -1;
    }

    if (!reason_phrase) {
        reason_phrase = http_status_code_to_reason_phrase(status_code);
        if (!reason_phrase) {
            http_set_error("unknown status code %d", status_code);
            return -1;
        }
    }

    if (http_connection_printf(connection, "%s %d %s\r\n",
                               version_str, status_code,
                               reason_phrase) == -1) {
        return -1;
    }

    return 0;
}

int
http_connection_write_header(struct http_connection *connection,
                             const char *name, const char *value) {
    if (http_connection_printf(connection, "%s: ", name) == -1)
        return -1;

    if (http_connection_write(connection, value, strlen(value)) == -1)
        return -1;

    if (http_connection_write(connection, "\r\n", 2) == -1)
        return -1;

    return 0;
}

int
http_connection_write_body(struct http_connection *connection,
                           const char *buf, size_t sz) {
    if (http_connection_write(connection, "\r\n", 2) == -1)
        return -1;

    if (http_connection_write(connection, buf, sz) == -1)
        return -1;

    return 0;
}

int
http_connection_write_empty_body(struct http_connection *connection) {
    if (http_connection_write(connection, "\r\n", 2) == -1)
        return -1;

    return 0;
}

static void
http_connection_process_msg(struct http_connection *connection,
                            struct http_msg *msg) {
    struct http_route_base *route_base;
    http_msg_handler handler;
    bool do_shutdown;
    enum http_route_match_result match_result;

    assert(msg->type == HTTP_MSG_REQUEST);

    route_base = connection->server->route_base;

    /* Version */
    connection->http_version = msg->version;

    /* URI */
    if (strcmp(msg->u.request.uri, "*") == 0) {
        /* '*' is used for requests that do not apply to a particular
         * resource such as OPTIONS, but we do not currently support any of
         * them. */
        http_connection_trace(connection, "invalid uri: '*'");
        http_connection_http_error(connection, HTTP_BAD_REQUEST);
        goto end;
    } else {
        /* Absolute URI or absolute path */
        msg->request_uri = http_uri_new(msg->u.request.uri);
        if (!msg->request_uri) {
            http_connection_trace(connection, "cannot parse uri: %s",
                                  http_get_error());
            http_connection_http_error(connection, HTTP_BAD_REQUEST);
            goto end;
        }

        /* TODO If the URI has a host make sure that it is our own. If it is
         * not, reject the request since we are not a proxy. */
    }

    /* Find a route matching the URI of the message and call its handler. */
    handler = http_route_base_find_msg_handler(route_base,
                                               msg->u.request.method,
                                               msg->request_uri->path,
                                               &match_result);
    if (!handler) {
        switch (match_result) {
        case HTTP_ROUTE_MATCH_WRONG_METHOD:
            http_connection_http_error(connection, HTTP_METHOD_NOT_ALLOWED);
            break;

        case HTTP_ROUTE_MATCH_WRONG_PATH:
        default:
            http_connection_http_error(connection, HTTP_NOT_FOUND);
            break;
        }

        goto end;
    }

    handler(connection, msg, route_base->msg_handler_arg);

end:
    if (msg->version == HTTP_1_0) {
        do_shutdown = !(msg->connection_options & HTTP_CONNECTION_KEEP_ALIVE);
    } else if (msg->version == HTTP_1_1) {
        do_shutdown = msg->connection_options & HTTP_CONNECTION_CLOSE;
    } else {
        do_shutdown = true;
    }

    if (do_shutdown) {
        if (http_connection_shutdown(connection) == -1) {
            http_connection_error(connection,
                                  "cannot shutdown connection: %s",
                                  http_get_error());
        }
    }
}
