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

#ifndef HTTP_HTTP_H
#define HTTP_HTTP_H

#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <event.h>

#include <buffer.h>
#include <hashtable.h>

/* Error handling */
const char *http_get_error(void);
void http_set_error(const char *, ...)
    __attribute__((format(printf, 1, 2)));
void http_set_error_invalid_character(unsigned char, const char *, ...)
    __attribute__((format(printf, 2, 3)));

/* Memory */
struct http_memory_allocator {
   void *(*malloc)(size_t);
   void (*free)(void *);
   void *(*calloc)(size_t, size_t);
   void *(*realloc)(void *, size_t);
};

extern const struct http_memory_allocator *http_default_memory_allocator;

void http_set_memory_allocator(const struct http_memory_allocator *allocator);

void *http_malloc(size_t);
void *http_malloc0(size_t);
void *http_calloc(size_t, size_t);
void *http_realloc(void *, size_t);
void http_free(void *);

/* Time */
#define HTTP_RFC1123_DATE_BUFSZ 64

void http_format_date(char [static HTTP_RFC1123_DATE_BUFSZ], size_t,
                      const struct tm *);
int http_format_timestamp(char [static HTTP_RFC1123_DATE_BUFSZ], size_t,
                          time_t);

/* Protocol */
enum http_version {
    HTTP_1_0 = 0,
    HTTP_1_1,
};

const char *http_version_to_string(enum http_version);

enum http_method {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_HEAD,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_OPTIONS,

    HTTP_METHOD_MAX
};

const char *http_method_to_string(enum http_method);

enum http_status_code {
    HTTP_CONTINUE                        = 100,
    HTTP_SWITCHING_PROTOCOLS             = 101,

    HTTP_OK                              = 200,
    HTTP_CREATED                         = 201,
    HTTP_ACCEPTED                        = 202,
    HTTP_NON_AUTHORITATIVE_INFORMATION   = 203,
    HTTP_NO_CONTENT                      = 204,
    HTTP_RESET_CONTENT                   = 205,
    HTTP_PARTIAL_CONTENT                 = 206,
    HTTP_MULTI_STATUS                    = 207, /* RFC 4918 */

    HTTP_MULTIPLE_CHOICES                = 300,
    HTTP_MOVED_PERMANENTLY               = 301,
    HTTP_FOUND                           = 302,
    HTTP_SEE_OTHERS                      = 303,
    HTTP_NOT_MODIFIED                    = 304,
    HTTP_USE_PROXY                       = 305,
    HTTP_TEMPORARY_REDIRECT              = 307,

    HTTP_BAD_REQUEST                     = 400,
    HTTP_UNAUTHORIZED                    = 401,
    HTTP_PAYMENT_REQUIRED                = 402,
    HTTP_FORBIDDEN                       = 403,
    HTTP_NOT_FOUND                       = 404,
    HTTP_METHOD_NOT_ALLOWED              = 405,
    HTTP_NOT_ACCEPTABLE                  = 406,
    HTTP_PROXY_AUTHENTICATION_REQUIRED   = 407,
    HTTP_REQUEST_TIMEOUT                 = 408,
    HTTP_CONFLICT                        = 409,
    HTTP_GONE                            = 410,
    HTTP_LENGTH_REQUIRED                 = 411,
    HTTP_PRECONDITION_FAILED             = 412,
    HTTP_REQUEST_ENTITY_TOO_LARGE        = 413,
    HTTP_REQUEST_URI_TOO_LONG            = 414,
    HTTP_UNSUPPORTED_MEDIA_TYPE          = 415,
    HTTP_REQUEST_RANGE_NOT_SATISFIABLE   = 416,
    HTTP_EXPECTATION_FAILED              = 417,
    HTTP_UNPROCESSABLE_ENTITY            = 422, /* RFC 4918 */
    HTTP_LOCKED                          = 423, /* RFC 4918 */
    HTTP_FAILED_DEPENDENCY               = 424, /* RFC 4918 */
    HTTP_PRECONDITION_REQUIRED           = 428, /* RFC 6585 */
    HTTP_TOO_MANY_REQUESTS               = 429, /* RFC 6585 */
    HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE = 431, /* RFC 6585 */

    HTTP_INTERNAL_SERVER_ERROR           = 500,
    HTTP_NOT_IMPLEMENTED                 = 501,
    HTTP_BAD_GATEWAY                     = 502,
    HTTP_SERVICE_UNAVAILABLE             = 503,
    HTTP_GATEWAY_TIMEOUT                 = 504,
    HTTP_HTTP_VERSION_NOT_SUPPORTED      = 505,
    HTTP_INSUFFICIENT_STORAGE            = 507, /* RFC 4918 */
    HTTP_NETWORK_AUTHENTICATION_REQUIRED = 511, /* RFC 6585 */
};

const char *http_status_code_to_reason_phrase(enum http_status_code);

enum http_msg_type {
    HTTP_MSG_REQUEST,
    HTTP_MSG_RESPONSE,
};

enum http_connection_option {
    HTTP_CONNECTION_KEEP_ALIVE = 0x01, /* HTTP/1.0 only */
    HTTP_CONNECTION_CLOSE      = 0x02,
};

enum http_range_unit {
    HTTP_RANGE_UNIT_BYTES,
};

struct http_ranges;

bool http_ranges_is_satisfiable(const struct http_ranges *, size_t);
size_t http_ranges_length(const struct http_ranges *);

struct http_msg;
struct http_header;
struct http_connection;

enum http_version http_msg_version(const struct http_msg *);

size_t http_msg_nb_headers(const struct http_msg *);
const struct http_header *http_msg_header(const struct http_msg *, size_t);
const char *http_msg_get_header(const struct http_msg *, const char *);

bool http_msg_is_complete(const struct http_msg *);
bool http_msg_aborted(const struct http_msg *);

bool http_msg_has_content_length(const struct http_msg *);
size_t http_msg_content_length(const struct http_msg *);
const struct http_media_type *http_msg_content_type(const struct http_msg *);
bool http_msg_content_type_is(const struct http_msg *, const char *);

const char *http_msg_body(const struct http_msg *);
size_t http_msg_body_length(const struct http_msg *);

const void *http_msg_content(const struct http_msg *);
bool http_msg_has_form_data(const struct http_msg *);

int http_msg_content_disposition_filename(const struct http_msg *, char **);

enum http_method http_request_method(const struct http_msg *);
const char *http_request_uri(const struct http_msg *);

const char *http_request_named_parameter(const struct http_msg *,
                                         const char *);
bool http_request_has_query_parameter(const struct http_msg *, const char *);
const char *http_request_query_parameter(const struct http_msg *, const char *);

bool http_request_has_ranges(const struct http_msg *);
const struct http_ranges *http_request_ranges(const struct http_msg *);

enum http_status_code http_response_status_code(const struct http_msg *);
const char *http_response_reason_phrase(const struct http_msg *);

const char *http_header_name(const struct http_header *);
const char *http_header_value(const struct http_header *);

struct http_headers *http_headers_new(void);
void http_headers_delete(struct http_headers *);

const char *http_headers_get_header(struct http_headers *, const char *);
void http_headers_add_header(struct http_headers *, const char *, const char *);
void http_headers_add_headers(struct http_headers *,
                              const struct http_headers *);
void http_headers_set_header(struct http_headers *, const char *, const char *);
void http_headers_format_header(struct http_headers *, const char *,
                                const char *, ...);
void http_headers_remove_header(struct http_headers *, const char *);

char *http_format_content_disposition_attachment(const char *);

struct http_form_data;

bool http_form_data_has_parameter(const struct http_form_data *,
                                  const char *);
const char *http_form_data_get_parameter(const struct http_form_data *,
                                         const char *);

/* Request info */
struct http_request_info;

enum http_version http_request_info_version(const struct http_request_info *);
enum http_method http_request_info_method(const struct http_request_info *);
const char *http_request_info_uri_string(const struct http_request_info *);
time_t http_request_info_date(const struct http_request_info *);

enum http_status_code
http_request_info_status_code(const struct http_request_info *);

/* Configuration */
struct http_client;
struct http_cfg;

typedef void (*http_error_hook)(const char *, void *);
typedef void (*http_trace_hook)(const char *, void *);

typedef int (*http_error_sender)(struct http_connection *,
                                 enum http_status_code,
                                 struct http_headers *, const char *);

typedef void (*http_request_received_hook)(struct http_connection *,
                                           const struct http_msg *, void *);
typedef void (*http_request_hook)(struct http_connection *,
                                  const struct http_request_info *, void *);

typedef void (*http_response_handler)(struct http_client *,
                                      const struct http_msg *,
                                      void *);

typedef void *(*http_content_decode_func)(const struct http_msg *,
                                          const struct http_cfg *);
typedef void (*http_content_delete_func)(void *);

struct http_content_decoder {
    const char *content_type;

    http_content_decode_func decode;
    http_content_delete_func delete;
};

struct http_cfg {
    const char *host;
    const char *port;

    bool use_ssl;
    const char *ssl_ciphers;

    http_error_hook error_hook;
    http_trace_hook trace_hook;
    http_request_received_hook request_received_hook;
    http_request_hook request_hook;
    void *hook_arg;

    union {
        struct {
            int connection_backlog;

            size_t max_request_uri_length;

            http_error_sender error_sender;

            const char *ssl_certificate;
            const char *ssl_key;
        } server;

        struct {
            size_t max_reason_phrase_length;

            http_response_handler response_handler;
            void *response_handler_arg;
        } client;
    } u;

    size_t max_header_name_length;
    size_t max_header_value_length;

    size_t max_content_length;
    size_t max_chunk_length;

    bool bufferize_body;

    uint64_t connection_timeout; /* milliseconds */

    struct http_content_decoder *content_decoders;
    size_t nb_content_decoders;

    struct http_headers *default_headers;
};

void http_cfg_init_server(struct http_cfg *cfg);
void http_cfg_init_client(struct http_cfg *cfg);
void http_cfg_free(struct http_cfg *cfg);

void http_cfg_content_decoder_add(struct http_cfg *, const char *,
                                  http_content_decode_func,
                                  http_content_delete_func);
const struct http_content_decoder *
http_cfg_content_decoder_get(const struct http_cfg *, const char *);

void http_cfg_default_header_add(struct http_cfg *, const char *, const char *);
void http_cfg_default_header_set(struct http_cfg *, const char *, const char *);

/* URIs */
struct http_uri *http_uri_new(const char *);
void http_uri_delete(struct http_uri *);

const char *http_uri_host(const struct http_uri *);
const char *http_uri_port(const struct http_uri *);

bool http_uri_has_query_parameter(const struct http_uri *, const char *);
const char *http_uri_query_parameter(const struct http_uri *, const char *);

void http_uri_set_scheme(struct http_uri *, const char *);
void http_uri_set_user(struct http_uri *, const char *);
void http_uri_set_password(struct http_uri *, const char *);
void http_uri_set_host(struct http_uri *, const char *);
void http_uri_set_port(struct http_uri *, const char *);
void http_uri_set_path(struct http_uri *, const char *);
void http_uri_set_fragment(struct http_uri *, const char *);

void http_uri_add_query_parameter(struct http_uri *,
                                  const char *, const char *);

char *http_uri_encode(const struct http_uri *);
char *http_uri_encode_path_and_query(const struct http_uri *);

/* Server */
struct http_route_options {
    bool bufferize_body;

    size_t max_content_length;

    struct http_headers *default_headers;
};

void http_route_options_init(struct http_route_options *,
                             const struct http_cfg *);
void http_route_options_free(struct http_route_options *);
void http_route_options_copy(struct http_route_options *,
                             const struct http_route_options *);

void http_route_options_default_header_add(struct http_route_options *,
                                           const char *, const char *);
void http_route_options_default_header_set(struct http_route_options *,
                                           const char *, const char *);

typedef void (*http_msg_handler)(struct http_connection *,
                                 const struct http_msg *, void *);

struct http_server *http_server_new(struct http_cfg *, struct event_base *);
void http_server_delete(struct http_server *server);

void http_server_set_msg_handler_arg(struct http_server *, void *);
int http_server_add_route(struct http_server *,
                          enum http_method, const char *, http_msg_handler,
                          const struct http_route_options *);

int http_default_error_sender(struct http_connection *,
                              enum http_status_code,
                              struct http_headers *, const char *);

/* Client */
struct http_client *http_client_new(struct http_cfg *, struct event_base *);
void http_client_delete(struct http_client *client);

struct http_connection *http_client_connection(const struct http_client *);

int http_client_send_request(struct http_client *, enum http_method,
                             const struct http_uri *,
                             struct http_headers *);
int http_client_send_request_with_body(struct http_client *, enum http_method,
                                       const struct http_uri *,
                                       struct http_headers *,
                                       const char *, size_t);
int http_client_send_request_with_file(struct http_client *, enum http_method,
                                       const struct http_uri *,
                                       struct http_headers *,
                                       const char *, int, size_t);

/* Connections */
void http_connection_discard(struct http_connection *);
int http_connection_shutdown(struct http_connection *);

const char *http_connection_address(const struct http_connection *);

void http_connection_trace(struct http_connection *, const char *, ...)
    __attribute__((format(printf, 2, 3)));
void http_connection_error(struct http_connection *, const char *, ...)
    __attribute__((format(printf, 2, 3)));

int http_connection_send_response(struct http_connection *,
                                  enum http_status_code,
                                  struct http_headers *);
int http_connection_send_response_with_body(struct http_connection *,
                                            enum http_status_code,
                                            struct http_headers *,
                                            const char *, size_t);
int http_connection_send_response_with_file(struct http_connection *,
                                            enum http_status_code,
                                            struct http_headers *,
                                            const char *, int, size_t,
                                            const struct http_ranges *);
int http_connection_send_error(struct http_connection *, enum http_status_code,
                               const char *, ...)
    __attribute__((format(printf, 3, 4)));

/* MIME */
struct http_media_type *http_media_type_new(const char *);
void http_media_type_delete(struct http_media_type *);

const char *http_media_type_string(const struct http_media_type *);
const char *http_media_type_base_string(const struct http_media_type *);

const char *http_media_type_get_type(const struct http_media_type *);
const char *http_media_type_get_subtype(const struct http_media_type *);

bool http_media_type_has_parameter(const struct http_media_type *,
                                   const char *);
const char *http_media_type_get_parameter(const struct http_media_type *,
                                          const char *);

char *http_mime_q_encode(const char *);

#define HTTP_MIME_BOUNDARY_SZ (32 + 1)

void http_mime_generate_boundary(char [static HTTP_MIME_BOUNDARY_SZ], size_t);

/* SSL */
void http_ssl_initialize(void);
void http_ssl_shutdown(void);

#endif
