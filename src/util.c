#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void *cr_calloc(size_t count, size_t size)
{
    return calloc(count, size);
}

char *cr_strdup(const char *s)
{
    size_t len;
    char *copy;

    if (s == NULL) {
        return NULL;
    }
    len = strlen(s);
    copy = (char *)malloc(len + 1);
    if (copy != NULL) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

char *cr_strndup(const char *s, size_t n)
{
    size_t len;
    char *copy;

    if (s == NULL) {
        return NULL;
    }
    len = 0;
    while (len < n && s[len] != '\0') {
        len++;
    }
    copy = (char *)malloc(len + 1);
    if (copy != NULL) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    return copy;
}

int cr_ascii_casecmp(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    if (a == NULL || b == NULL) {
        return (a == b) ? 0 : (a == NULL ? -1 : 1);
    }
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)tolower((unsigned char)*a);
        cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int cr_ascii_ncasecmp(const char *a, const char *b, size_t n)
{
    size_t i;
    unsigned char ca;
    unsigned char cb;

    for (i = 0; i < n; i++) {
        ca = (unsigned char)tolower((unsigned char)a[i]);
        cb = (unsigned char)tolower((unsigned char)b[i]);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        if (a[i] == '\0') {
            return 0;
        }
    }
    return 0;
}

char *cr_ascii_lower(char *s)
{
    char *p;

    if (s == NULL) {
        return NULL;
    }
    for (p = s; *p != '\0'; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
    return s;
}

void cr_trim(const char **start, size_t *len)
{
    const char *s;
    size_t n;

    if (start == NULL || len == NULL || *start == NULL) {
        return;
    }
    s = *start;
    n = *len;
    while (n > 0 && (s[0] == ' ' || s[0] == '\t')) {
        s++;
        n--;
    }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        n--;
    }
    *start = s;
    *len = n;
}

long long cr_now_ms(void)
{
    return (long long)GetTickCount64();
}

int cr_parse_ll(const char *s, long long *out)
{
    char *end = NULL;
    long long value;

    if (s == NULL || out == NULL || *s == '\0') {
        return 0;
    }
    errno = 0;
    value = strtoll(s, &end, 10);
    if (errno != 0 || end == s) {
        return 0;
    }
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != '\0') {
        return 0;
    }
    *out = value;
    return 1;
}

int cr_is_ascii_token(const char *s)
{
    const unsigned char *p;

    if (s == NULL || *s == '\0') {
        return 0;
    }
    for (p = (const unsigned char *)s; *p != '\0'; p++) {
        if (isalnum(*p)) {
            continue;
        }
        switch (*p) {
        case '!': case '#': case '$': case '%': case '&': case '\'':
        case '*': case '+': case '-': case '.': case '^': case '_':
        case '`': case '|': case '~':
            break;
        default:
            return 0;
        }
    }
    return 1;
}

int cr_header_name_valid(const char *name)
{
    return cr_is_ascii_token(name);
}

int cr_header_value_valid(const char *value)
{
    const unsigned char *p;

    if (value == NULL) {
        return 1;
    }
    for (p = (const unsigned char *)value; *p != '\0'; p++) {
        if (*p == '\r' || *p == '\n' || *p == '\0') {
            return 0;
        }
    }
    return 1;
}

static wchar_t *utf8_to_wide(const char *s)
{
    int needed;
    wchar_t *out;

    needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (needed <= 0) {
        return NULL;
    }
    out = (wchar_t *)malloc((size_t)needed * sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, out, needed) <= 0) {
        free(out);
        return NULL;
    }
    return out;
}

char *cr_fopen_read_all_utf8(const char *path, size_t *len, cr_error *error)
{
    wchar_t *wide = NULL;
    FILE *file = NULL;
    long long size;
    char *data = NULL;

    if (len != NULL) {
        *len = 0;
    }
    if (error != NULL) {
        *error = CR_OK;
    }
    if (path == NULL) {
        if (error != NULL) {
            *error = CR_ERR_ARGUMENT;
        }
        return NULL;
    }

    wide = utf8_to_wide(path);
    if (wide == NULL) {
        if (error != NULL) {
            *error = CR_ERR_FILE;
        }
        return NULL;
    }
    file = _wfopen(wide, L"rb");
    free(wide);
    if (file == NULL) {
        if (error != NULL) {
            *error = CR_ERR_FILE;
        }
        return NULL;
    }

    if (_fseeki64(file, 0, SEEK_END) != 0) {
        fclose(file);
        if (error != NULL) {
            *error = CR_ERR_FILE;
        }
        return NULL;
    }
    size = _ftelli64(file);
    if (size < 0) {
        fclose(file);
        if (error != NULL) {
            *error = CR_ERR_FILE;
        }
        return NULL;
    }
    if (_fseeki64(file, 0, SEEK_SET) != 0) {
        fclose(file);
        if (error != NULL) {
            *error = CR_ERR_FILE;
        }
        return NULL;
    }

    data = (char *)malloc((size_t)size + 1);
    if (data == NULL) {
        fclose(file);
        if (error != NULL) {
            *error = CR_ERR_NOMEM;
        }
        return NULL;
    }
    if (size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        if (error != NULL) {
            *error = CR_ERR_FILE;
        }
        return NULL;
    }
    fclose(file);
    data[size] = '\0';
    if (len != NULL) {
        *len = (size_t)size;
    }
    return data;
}

char *cr_base64_encode(const unsigned char *data, size_t len)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len;
    size_t groups;
    char *out;
    size_t i;
    size_t j;

    if (len > SIZE_MAX - 2) {
        return NULL;
    }
    groups = (len + 2) / 3;
    if (groups > (SIZE_MAX - 1) / 4) {
        return NULL;
    }
    out_len = groups * 4;
    out = (char *)malloc(out_len + 1);
    if (out == NULL) {
        return NULL;
    }

    j = 0;
    for (i = 0; i + 2 < len; i += 3) {
        unsigned int v = ((unsigned int)data[i] << 16) |
                         ((unsigned int)data[i + 1] << 8) |
                         (unsigned int)data[i + 2];
        out[j++] = alphabet[(v >> 18) & 0x3f];
        out[j++] = alphabet[(v >> 12) & 0x3f];
        out[j++] = alphabet[(v >> 6) & 0x3f];
        out[j++] = alphabet[v & 0x3f];
    }
    if (i < len) {
        unsigned int v = (unsigned int)data[i] << 16;
        out[j++] = alphabet[(v >> 18) & 0x3f];
        if (i + 1 < len) {
            v |= (unsigned int)data[i + 1] << 8;
            out[j++] = alphabet[(v >> 12) & 0x3f];
            out[j++] = alphabet[(v >> 6) & 0x3f];
            out[j++] = '=';
        } else {
            out[j++] = alphabet[(v >> 12) & 0x3f];
            out[j++] = '=';
            out[j++] = '=';
        }
    }
    out[j] = '\0';
    return out;
}

static int month_index(const char *month)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    size_t i;

    for (i = 0; i < 12; i++) {
        if (cr_ascii_ncasecmp(month, months[i], 3) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2 && is_leap(year)) {
        return 29;
    }
    return days[month - 1];
}

static long long days_from_civil(int year, int month, int day)
{
    int y = year - (month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned int yoe = (unsigned int)(y - era * 400);
    int mp = month + (month > 2 ? -3 : 9);
    unsigned int doy = (unsigned int)((153 * mp + 2) / 5 + day - 1);
    unsigned int doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (long long)era * 146097LL + (long long)doe - 719468LL;
}

time_t cr_parse_http_date(const char *text)
{
    char wday[8] = {0};
    char month[8] = {0};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int mon;
    int matched;

    if (text == NULL) {
        return 0;
    }
    while (*text == ' ' || *text == '\t') {
        text++;
    }

    /* IMF-fixdate: Sun, 06 Nov 1994 08:49:37 GMT */
    matched = sscanf(text, "%3[^,], %d %3s %d %d:%d:%d",
                     wday, &day, month, &year, &hour, &minute, &second);
    if (matched != 7) {
        /* RFC 850: Sunday, 06-Nov-94 08:49:37 GMT */
        matched = sscanf(text, "%3[^,], %d-%3s-%d %d:%d:%d",
                         wday, &day, month, &year, &hour, &minute, &second);
        if (matched == 7 && year >= 0 && year < 100) {
            year += (year < 70) ? 2000 : 1900;
        } else if (matched != 7) {
            /* ANSI C asctime: Sun Nov  6 08:49:37 1994 */
            matched = sscanf(text, "%3s %3s %d %d:%d:%d %d",
                             wday, month, &day, &hour, &minute, &second, &year);
        }
    }
    if (matched != 7) {
        return 0;
    }

    mon = month_index(month);
    if (mon < 0 || day < 1 || day > days_in_month(year, mon + 1) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60) {
        return 0;
    }

    {
        long long days = days_from_civil(year, (unsigned int)(mon + 1), (unsigned int)day);
        long long seconds = days * 86400LL + hour * 3600LL + minute * 60LL + second;
        if (seconds < 0 || seconds > (long long)LLONG_MAX) {
            return 0;
        }
        return (time_t)seconds;
    }
}

/* ------------------------------------------------------------------------- */
/* Byte buffer                                                               */
/* ------------------------------------------------------------------------- */

void cr_buf_init(cr_buf *buf)
{
    if (buf != NULL) {
        buf->data = NULL;
        buf->len = 0;
        buf->cap = 0;
    }
}

void cr_buf_free(cr_buf *buf)
{
    if (buf != NULL) {
        free(buf->data);
        buf->data = NULL;
        buf->len = 0;
        buf->cap = 0;
    }
}

int cr_buf_reserve(cr_buf *buf, size_t extra)
{
    size_t needed;
    size_t cap;
    unsigned char *new_data;

    if (buf == NULL) {
        return 0;
    }
    if (extra > SIZE_MAX - buf->len) {
        return 0;
    }
    needed = buf->len + extra;
    if (needed <= buf->cap) {
        return 1;
    }
    cap = (buf->cap == 0) ? 256 : buf->cap;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2) {
            cap = needed;
            break;
        }
        cap *= 2;
    }
    new_data = (unsigned char *)realloc(buf->data, cap);
    if (new_data == NULL) {
        return 0;
    }
    buf->data = new_data;
    buf->cap = cap;
    return 1;
}

int cr_buf_append(cr_buf *buf, const void *data, size_t len)
{
    if (buf == NULL || (data == NULL && len != 0)) {
        return 0;
    }
    if (!cr_buf_reserve(buf, len)) {
        return 0;
    }
    if (len != 0) {
        memcpy(buf->data + buf->len, data, len);
        buf->len += len;
    }
    return 1;
}

int cr_buf_append_byte(cr_buf *buf, unsigned char byte)
{
    return cr_buf_append(buf, &byte, 1);
}

int cr_buf_append_str(cr_buf *buf, const char *s)
{
    if (s == NULL) {
        return 1;
    }
    return cr_buf_append(buf, s, strlen(s));
}

int cr_buf_appendf(cr_buf *buf, const char *fmt, ...)
{
    va_list ap;
    va_list copy;
    int needed;
    size_t old_len;

    if (buf == NULL || fmt == NULL) {
        return 0;
    }
    old_len = buf->len;
    va_start(ap, fmt);
    va_copy(copy, ap);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(ap);
        return 0;
    }
    if (!cr_buf_reserve(buf, (size_t)needed + 1)) {
        va_end(ap);
        return 0;
    }
    vsnprintf((char *)buf->data + old_len, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    buf->len = old_len + (size_t)needed;
    return 1;
}

int cr_buf_set(cr_buf *buf, const void *data, size_t len)
{
    if (buf == NULL) {
        return 0;
    }
    buf->len = 0;
    return cr_buf_append(buf, data, len);
}

/* ------------------------------------------------------------------------- */
/* Key/value list                                                            */
/* ------------------------------------------------------------------------- */

void cr_kv_list_init(cr_kv_list *list)
{
    if (list != NULL) {
        list->items = NULL;
        list->len = 0;
        list->cap = 0;
    }
}

void cr_kv_list_clear(cr_kv_list *list)
{
    size_t i;

    if (list == NULL) {
        return;
    }
    for (i = 0; i < list->len; i++) {
        free(list->items[i].name);
        free(list->items[i].value);
    }
    list->len = 0;
}

void cr_kv_list_free(cr_kv_list *list)
{
    if (list == NULL) {
        return;
    }
    cr_kv_list_clear(list);
    free(list->items);
    list->items = NULL;
    list->cap = 0;
}

int cr_kv_add(cr_kv_list *list, const char *name, const char *value)
{
    char *name_copy;
    char *value_copy = NULL;
    size_t cap;

    if (list == NULL || name == NULL) {
        return 0;
    }
    name_copy = cr_strdup(name);
    if (name_copy == NULL) {
        return 0;
    }
    if (value != NULL) {
        value_copy = cr_strdup(value);
        if (value_copy == NULL) {
            free(name_copy);
            return 0;
        }
    }
    if (list->len == list->cap) {
        cr_kv *items;
        cap = (list->cap == 0) ? 8 : list->cap * 2;
        items = (cr_kv *)realloc(list->items, cap * sizeof(cr_kv));
        if (items == NULL) {
            free(name_copy);
            free(value_copy);
            return 0;
        }
        list->items = items;
        list->cap = cap;
    }
    list->items[list->len].name = name_copy;
    list->items[list->len].value = value_copy;
    list->len++;
    return 1;
}

int cr_kv_remove(cr_kv_list *list, const char *name)
{
    size_t i;
    size_t out;

    if (list == NULL || name == NULL) {
        return 0;
    }
    out = 0;
    for (i = 0; i < list->len; i++) {
        if (cr_ascii_casecmp(list->items[i].name, name) == 0) {
            free(list->items[i].name);
            free(list->items[i].value);
        } else {
            if (out != i) {
                list->items[out] = list->items[i];
            }
            out++;
        }
    }
    list->len = out;
    return 1;
}

int cr_kv_set(cr_kv_list *list, const char *name, const char *value)
{
    if (list == NULL || name == NULL) {
        return 0;
    }
    cr_kv_remove(list, name);
    if (value == NULL) {
        return 1;
    }
    return cr_kv_add(list, name, value);
}

const char *cr_kv_get(const cr_kv_list *list, const char *name)
{
    size_t i;

    if (list == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < list->len; i++) {
        if (cr_ascii_casecmp(list->items[i].name, name) == 0) {
            return list->items[i].value;
        }
    }
    return NULL;
}

int cr_kv_has(const cr_kv_list *list, const char *name)
{
    return cr_kv_get(list, name) != NULL;
}

int cr_kv_copy(cr_kv_list *dst, const cr_kv_list *src)
{
    size_t i;

    if (dst == NULL || src == NULL) {
        return 0;
    }
    cr_kv_list_clear(dst);
    for (i = 0; i < src->len; i++) {
        if (!cr_kv_add(dst, src->items[i].name, src->items[i].value)) {
            return 0;
        }
    }
    return 1;
}
