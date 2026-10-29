#include "internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void cr_cookie_list_init(cr_cookie_list *list)
{
    if (list != NULL) {
        list->items = NULL;
        list->len = 0;
        list->cap = 0;
    }
}

static void cookie_free(cr_cookie *cookie)
{
    if (cookie == NULL) {
        return;
    }
    free(cookie->name);
    free(cookie->value);
    free(cookie->domain);
    free(cookie->path);
    memset(cookie, 0, sizeof(*cookie));
}

void cr_cookie_list_clear(cr_cookie_list *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->len; i++) {
        cookie_free(&list->items[i]);
    }
    list->len = 0;
}

void cr_cookie_list_free(cr_cookie_list *list)
{
    if (list == NULL) {
        return;
    }
    cr_cookie_list_clear(list);
    free(list->items);
    list->items = NULL;
    list->cap = 0;
}

static void cookie_remove_at(cr_cookie_list *list, size_t index)
{
    if (index >= list->len) {
        return;
    }
    cookie_free(&list->items[index]);
    if (index + 1 < list->len) {
        memmove(&list->items[index], &list->items[index + 1],
                (list->len - index - 1) * sizeof(cr_cookie));
    }
    list->len--;
}

static int host_is_ip_literal(const char *host)
{
    const char *p;
    int dots = 0;
    int digits = 0;
    int only_ipv4 = 1;

    if (strchr(host, ':') != NULL) {
        return 1;
    }
    for (p = host; *p != '\0'; p++) {
        if (*p == '.') {
            dots++;
            digits = 0;
        } else if (*p >= '0' && *p <= '9') {
            digits++;
        } else {
            only_ipv4 = 0;
            break;
        }
    }
    return only_ipv4 && dots == 3 && digits > 0;
}

static int domain_match(const char *host, const char *domain)
{
    size_t host_len;
    size_t domain_len;

    if (host == NULL || domain == NULL) {
        return 0;
    }
    if (cr_ascii_casecmp(host, domain) == 0) {
        return 1;
    }
    if (host_is_ip_literal(host)) {
        return 0;
    }
    host_len = strlen(host);
    domain_len = strlen(domain);
    if (domain_len == 0 || host_len <= domain_len) {
        return 0;
    }
    return host[host_len - domain_len - 1] == '.' &&
           cr_ascii_casecmp(host + host_len - domain_len, domain) == 0;
}

static int path_match(const char *request_path, const char *cookie_path)
{
    size_t req_len;
    size_t cookie_len;

    if (request_path == NULL || cookie_path == NULL) {
        return 0;
    }
    req_len = strlen(request_path);
    cookie_len = strlen(cookie_path);
    if (cookie_len == 0) {
        return 1;
    }
    if (req_len < cookie_len || strncmp(request_path, cookie_path, cookie_len) != 0) {
        return 0;
    }
    if (cookie_path[cookie_len - 1] == '/') {
        return 1;
    }
    return req_len == cookie_len || request_path[cookie_len] == '/';
}

static char *default_cookie_path(const char *request_path)
{
    const char *slash;
    size_t len;

    if (request_path == NULL || request_path[0] != '/') {
        return cr_strdup("/");
    }
    slash = strrchr(request_path, '/');
    if (slash == NULL || slash == request_path) {
        return cr_strdup("/");
    }
    len = (size_t)(slash - request_path);
    if (len == 0) {
        return cr_strdup("/");
    }
    {
        char *out = (char *)malloc(len + 1);
        if (out == NULL) {
            return NULL;
        }
        memcpy(out, request_path, len);
        out[len] = '\0';
        return out;
    }
}

static int cookie_name_valid(const char *name)
{
    const unsigned char *p;

    if (name == NULL || *name == '\0') {
        return 0;
    }
    for (p = (const unsigned char *)name; *p != '\0'; p++) {
        if (*p <= 0x20 || *p >= 0x7f) {
            return 0;
        }
        switch (*p) {
        case ';': case '=': case ',': case '"': case '\\':
            return 0;
        default:
            break;
        }
    }
    return 1;
}

static int cookie_value_valid(const char *value)
{
    const unsigned char *p;

    for (p = (const unsigned char *)value; p != NULL && *p != '\0'; p++) {
        if (*p < 0x20 || *p == 0x7f || *p == ';') {
            return 0;
        }
    }
    return 1;
}

static char *trim_copy(const char *start, size_t len)
{
    const char *s = start;
    cr_trim(&s, &len);
    return cr_strndup(s, len);
}

static int cookie_find(const cr_cookie_list *list, const char *name,
                       const char *domain, const char *path, int host_only)
{
    size_t i;

    for (i = 0; i < list->len; i++) {
        const cr_cookie *c = &list->items[i];
        if (c->host_only == host_only &&
            strcmp(c->name, name) == 0 &&
            cr_ascii_casecmp(c->domain, domain) == 0 &&
            strcmp(c->path, path) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void cookie_remove_matching(cr_cookie_list *list, const char *name,
                                   const char *domain, const char *path,
                                   int host_only)
{
    int index = cookie_find(list, name, domain, path, host_only);
    if (index >= 0) {
        cookie_remove_at(list, (size_t)index);
    }
}

static int cookie_list_reserve(cr_cookie_list *list)
{
    cr_cookie *items;
    size_t cap;

    if (list->len < list->cap) {
        return 1;
    }
    cap = (list->cap == 0) ? 8 : list->cap * 2;
    if (cap > CR_MAX_COOKIES) {
        cap = CR_MAX_COOKIES;
    }
    if (cap <= list->len) {
        return 0;
    }
    items = (cr_cookie *)realloc(list->items, cap * sizeof(cr_cookie));
    if (items == NULL) {
        return 0;
    }
    list->items = items;
    list->cap = cap;
    return 1;
}

static void cookie_make_room(cr_cookie_list *list)
{
    size_t i;
    size_t oldest = 0;
    unsigned long oldest_seq;

    if (list->len < CR_MAX_COOKIES) {
        return;
    }
    oldest_seq = list->items[0].seq;
    for (i = 1; i < list->len; i++) {
        if (list->items[i].seq < oldest_seq) {
            oldest_seq = list->items[i].seq;
            oldest = i;
        }
    }
    cookie_remove_at(list, oldest);
}

static int parse_set_cookie(cr_session *session,
                            const cr_url *url,
                            const char *header,
                            time_t now)
{
    char *copy = NULL;
    char *semi;
    char *eq;
    char *name = NULL;
    char *value = NULL;
    char *domain = NULL;
    char *path = NULL;
    char *default_path = NULL;
    int host_only = 1;
    int secure = 0;
    int http_only = 0;
    time_t expires = 0;
    int have_max_age = 0;
    int expired = 0;
    int index;
    cr_cookie cookie;
    size_t header_len;

    if (header == NULL) {
        return 0;
    }
    header_len = strlen(header);
    if (header_len == 0 || header_len > CR_MAX_COOKIE_BYTES) {
        return 0;
    }
    copy = cr_strdup(header);
    if (copy == NULL) {
        return 0;
    }

    semi = strchr(copy, ';');
    if (semi != NULL) {
        *semi = '\0';
    }
    eq = strchr(copy, '=');
    if (eq == NULL) {
        free(copy);
        return 0;
    }
    name = trim_copy(copy, (size_t)(eq - copy));
    value = trim_copy(eq + 1, strlen(eq + 1));
    if (name == NULL || value == NULL || !cookie_name_valid(name) ||
        !cookie_value_valid(value)) {
        goto cleanup;
    }
    if (value[0] == '"') {
        size_t value_len = strlen(value);
        if (value_len >= 2 && value[value_len - 1] == '"') {
            memmove(value, value + 1, value_len - 2);
            value[value_len - 2] = '\0';
        }
    }

    while (semi != NULL) {
        char *attr;
        char *next;
        char *attr_eq;
        char *key;
        char *val = NULL;

        attr = semi + 1;
        next = strchr(attr, ';');
        if (next != NULL) {
            *next = '\0';
        }
        semi = next;

        while (*attr == ' ' || *attr == '\t') {
            attr++;
        }
        if (*attr == '\0') {
            continue;
        }
        attr_eq = strchr(attr, '=');
        if (attr_eq != NULL) {
            *attr_eq = '\0';
            val = attr_eq + 1;
            while (*val == ' ' || *val == '\t') {
                val++;
            }
        }
        key = attr;
        {
            size_t key_len = strlen(key);
            while (key_len > 0 && (key[key_len - 1] == ' ' || key[key_len - 1] == '\t')) {
                key[--key_len] = '\0';
            }
        }

        if (cr_ascii_casecmp(key, "Domain") == 0 && val != NULL) {
            size_t val_len;
            while (*val == '.') {
                val++;
            }
            val_len = strlen(val);
            while (val_len > 0 && (val[val_len - 1] == ' ' || val[val_len - 1] == '\t')) {
                val[--val_len] = '\0';
            }
            if (val_len > 0 && val[0] != '.' && strpbrk(val, " /:;,") == NULL) {
                free(domain);
                domain = cr_ascii_lower(cr_strdup(val));
                if (domain == NULL) {
                    goto cleanup;
                }
                host_only = 0;
            }
        } else if (cr_ascii_casecmp(key, "Path") == 0 && val != NULL) {
            size_t val_len;
            while (*val == ' ' || *val == '\t') {
                val++;
            }
            val_len = strlen(val);
            while (val_len > 0 && (val[val_len - 1] == ' ' || val[val_len - 1] == '\t')) {
                val[--val_len] = '\0';
            }
            if (val[0] == '/') {
                free(path);
                path = cr_strdup(val);
                if (path == NULL) {
                    goto cleanup;
                }
            }
        } else if (cr_ascii_casecmp(key, "Max-Age") == 0 && val != NULL) {
            long long max_age;
            if (cr_parse_ll(val, &max_age)) {
                have_max_age = 1;
                if (max_age <= 0) {
                    expired = 1;
                    expires = 1;
                } else {
                    long long when = (long long)now + max_age;
                    if (when < (long long)now) {
                        when = (long long)now;
                    }
                    expires = (time_t)when;
                }
            }
        } else if (cr_ascii_casecmp(key, "Expires") == 0 && val != NULL) {
            if (!have_max_age) {
                time_t when = cr_parse_http_date(val);
                expires = when;
                if (when <= 0) {
                    expired = 1;
                    expires = 1;
                }
            }
        } else if (cr_ascii_casecmp(key, "Secure") == 0) {
            secure = 1;
        } else if (cr_ascii_casecmp(key, "HttpOnly") == 0) {
            http_only = 1;
        }
    }

    if (domain != NULL) {
        if (!domain_match(url->host, domain)) {
            goto cleanup;
        }
    }
    if (path == NULL) {
        default_path = default_cookie_path(url->path);
        if (default_path == NULL) {
            goto cleanup;
        }
        path = default_path;
        default_path = NULL;
    }

    if (expired || (expires > 0 && expires <= now)) {
        cookie_remove_matching(&session->cookies, name, host_only ? url->host : domain,
                               path, host_only);
        goto cleanup;
    }

    index = cookie_find(&session->cookies, name, host_only ? url->host : domain,
                        path, host_only);
    if (index >= 0) {
        cookie_remove_at(&session->cookies, (size_t)index);
    }
    cookie_make_room(&session->cookies);
    if (!cookie_list_reserve(&session->cookies)) {
        goto cleanup;
    }

    memset(&cookie, 0, sizeof(cookie));
    cookie.name = cr_strdup(name);
    cookie.value = cr_strdup(value);
    cookie.domain = cr_strdup(host_only ? url->host : domain);
    cookie.path = cr_strdup(path);
    if (cookie.name == NULL || cookie.value == NULL ||
        cookie.domain == NULL || cookie.path == NULL) {
        cookie_free(&cookie);
        goto cleanup;
    }
    cookie.host_only = host_only;
    cookie.secure = secure;
    cookie.http_only = http_only;
    cookie.expires = expires;
    cookie.seq = ++session->cookie_seq;
    session->cookies.items[session->cookies.len++] = cookie;

cleanup:
    free(default_path);
    free(domain);
    free(path);
    free(name);
    free(value);
    free(copy);
    return 0;
}

cr_error cr_cookie_store_headers(cr_session *session,
                                 const cr_url *url,
                                 const cr_kv_list *headers)
{
    size_t i;
    time_t now;

    if (session == NULL || url == NULL || headers == NULL) {
        return CR_ERR_ARGUMENT;
    }
    now = time(NULL);
    for (i = 0; i < headers->len; i++) {
        if (cr_ascii_casecmp(headers->items[i].name, "Set-Cookie") == 0) {
            parse_set_cookie(session, url, headers->items[i].value, now);
        }
    }
    cr_cookie_purge_expired(session);
    return CR_OK;
}

void cr_cookie_purge_expired(cr_session *session)
{
    size_t i;
    time_t now;

    if (session == NULL) {
        return;
    }
    now = time(NULL);
    i = 0;
    while (i < session->cookies.len) {
        cr_cookie *c = &session->cookies.items[i];
        if (c->expires > 0 && c->expires <= now) {
            cookie_remove_at(&session->cookies, i);
        } else {
            i++;
        }
    }
}

static int cookie_ptr_cmp(const void *a, const void *b)
{
    const cr_cookie *ca = *(const cr_cookie *const *)a;
    const cr_cookie *cb = *(const cr_cookie *const *)b;
    size_t la = strlen(ca->path);
    size_t lb = strlen(cb->path);

    if (la != lb) {
        return (la > lb) ? -1 : 1;
    }
    if (ca->seq < cb->seq) {
        return -1;
    }
    if (ca->seq > cb->seq) {
        return 1;
    }
    return 0;
}

char *cr_cookie_header_for(cr_session *session, const cr_url *url)
{
    const cr_cookie **matched = NULL;
    size_t matched_count = 0;
    size_t i;
    time_t now;
    cr_buf out;

    if (session == NULL || url == NULL || session->cookies.len == 0) {
        return NULL;
    }
    cr_cookie_purge_expired(session);
    if (session->cookies.len == 0) {
        return NULL;
    }
    matched = (const cr_cookie **)calloc(session->cookies.len, sizeof(*matched));
    if (matched == NULL) {
        return NULL;
    }
    now = time(NULL);
    for (i = 0; i < session->cookies.len; i++) {
        const cr_cookie *c = &session->cookies.items[i];
        if (c->expires > 0 && c->expires <= now) {
            continue;
        }
        if (c->secure && !url->tls) {
            continue;
        }
        if (c->host_only) {
            if (cr_ascii_casecmp(c->domain, url->host) != 0) {
                continue;
            }
        } else if (!domain_match(url->host, c->domain)) {
            continue;
        }
        if (!path_match(url->path, c->path)) {
            continue;
        }
        matched[matched_count++] = c;
    }
    if (matched_count == 0) {
        free(matched);
        return NULL;
    }
    qsort(matched, matched_count, sizeof(*matched), cookie_ptr_cmp);

    cr_buf_init(&out);
    for (i = 0; i < matched_count; i++) {
        size_t needed = strlen(matched[i]->name) +
                        strlen(matched[i]->value) + 4;
        if (out.len + needed > CR_MAX_COOKIE_BYTES) {
            break;
        }
        if (out.len > 0 && !cr_buf_append_str(&out, "; ")) {
            cr_buf_free(&out);
            free(matched);
            return NULL;
        }
        if (!cr_buf_append_str(&out, matched[i]->name) ||
            !cr_buf_append_byte(&out, '=') ||
            !cr_buf_append_str(&out, matched[i]->value)) {
            cr_buf_free(&out);
            free(matched);
            return NULL;
        }
    }
    free(matched);
    if (!cr_buf_append_byte(&out, '\0')) {
        cr_buf_free(&out);
        return NULL;
    }
    return (char *)out.data;
}
