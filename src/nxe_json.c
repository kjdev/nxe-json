/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_json.c - JSON abstraction layer (jansson wrapper)
 *
 * Implementation notes:
 *   - Opaque nxe_json_t* is always a jansson json_t* underneath.
 *   - Logging is restricted to APIs that accept a pool / log argument
 *     so the module stays safe in contexts without a global cycle
 *     (e.g. standalone unit tests).
 *   - parse_untrusted() walks the tree after parsing to enforce
 *     DoS-limiting structural bounds; parse() does size + duplicate
 *     key checks only.
 *   - compare() is fail-closed: before the double fallback, any
 *     integer operand whose magnitude exceeds 2^53 is rejected with
 *     NGX_ERROR rather than silently collapsing onto a neighbouring
 *     double.
 */

#include "nxe_json.h"

#include <jansson.h>
#include <math.h>


#define NXE_JSON_CAST(json)  ((json_t *) (json))

/* Largest |int64_t| that round-trips through double without loss.
 * Integers above this collapse onto neighbouring doubles, so the
 * double-fallback path in nxe_json_compare() must refuse to convert
 * them. */
#define NXE_JSON_MAX_SAFE_INTEGER  9007199254740992LL


static ngx_log_t *
nxe_json_log(ngx_pool_t *pool)
{
    return pool != NULL ? pool->log : NULL;
}


static ngx_flag_t
nxe_json_int_double_safe(int64_t value)
{
    return value >= -NXE_JSON_MAX_SAFE_INTEGER
           && value <= NXE_JSON_MAX_SAFE_INTEGER;
}


static ngx_int_t
nxe_json_validate(json_t *json, ngx_uint_t depth, ngx_log_t *log)
{
    const char *key;
    json_t *value;
    size_t index;
    size_t size;

    if (json == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_json: validation failed, NULL value");
        return NGX_ERROR;
    }

    if (depth > NXE_JSON_MAX_DEPTH) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_json: validation failed, "
                      "depth %ui exceeds limit %ui",
                      depth, (ngx_uint_t) NXE_JSON_MAX_DEPTH);
        return NGX_ERROR;
    }

    switch (json_typeof(json)) {

    case JSON_STRING:
        size = json_string_length(json);
        if (size > NXE_JSON_MAX_STRING_LENGTH) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "nxe_json: validation failed, "
                          "string length %uz exceeds limit %uz",
                          size, (size_t) NXE_JSON_MAX_STRING_LENGTH);
            return NGX_ERROR;
        }
        break;

    case JSON_ARRAY:
        size = json_array_size(json);
        if (size > NXE_JSON_MAX_ARRAY_SIZE) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "nxe_json: validation failed, "
                          "array size %uz exceeds limit %uz",
                          size, (size_t) NXE_JSON_MAX_ARRAY_SIZE);
            return NGX_ERROR;
        }

        json_array_foreach(json, index, value) {
            if (nxe_json_validate(value, depth + 1, log) != NGX_OK) {
                return NGX_ERROR;
            }
        }
        break;

    case JSON_OBJECT:
        size = json_object_size(json);
        if (size > NXE_JSON_MAX_OBJECT_KEYS) {
            ngx_log_error(NGX_LOG_ERR, log, 0,
                          "nxe_json: validation failed, "
                          "object key count %uz exceeds limit %uz",
                          size, (size_t) NXE_JSON_MAX_OBJECT_KEYS);
            return NGX_ERROR;
        }

        /* jansson rejects NUL bytes in object keys at parse time
         * (JSON_ALLOW_NUL is never set), so ngx_strlen is safe here. */
        json_object_foreach(json, key, value) {
            if (ngx_strlen(key) > NXE_JSON_MAX_STRING_LENGTH) {
                ngx_log_error(NGX_LOG_ERR, log, 0,
                              "nxe_json: validation failed, "
                              "object key length exceeds limit %uz",
                              (size_t) NXE_JSON_MAX_STRING_LENGTH);
                return NGX_ERROR;
            }

            if (nxe_json_validate(value, depth + 1, log) != NGX_OK) {
                return NGX_ERROR;
            }
        }
        break;

    case JSON_NULL:
    case JSON_TRUE:
    case JSON_FALSE:
    case JSON_INTEGER:
    case JSON_REAL:
        break;

    default:
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_json: validation failed, unknown type");
        return NGX_ERROR;
    }

    return NGX_OK;
}


nxe_json_t *
nxe_json_parse(ngx_str_t *data, ngx_pool_t *pool)
{
    json_t *root;
    json_error_t error;
    ngx_log_t *log;

    log = nxe_json_log(pool);

    if (data == NULL || data->data == NULL || data->len == 0) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_json: parse failed, empty input");
        return NULL;
    }

    if (data->len > NXE_JSON_MAX_SIZE) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_json: parse failed, input too large: "
                      "%uz bytes (limit: %uz)",
                      data->len, (size_t) NXE_JSON_MAX_SIZE);
        return NULL;
    }

    root = json_loadb((const char *) data->data, data->len,
                      JSON_DECODE_ANY | JSON_REJECT_DUPLICATES, &error);
    if (root == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_json: parse failed at line %d: %s "
                      "(input %uz bytes)",
                      error.line, error.text, data->len);
        return NULL;
    }

    return (nxe_json_t *) root;
}


nxe_json_t *
nxe_json_parse_untrusted(ngx_str_t *data, ngx_pool_t *pool)
{
    nxe_json_t *root;
    ngx_log_t *log;

    root = nxe_json_parse(data, pool);
    if (root == NULL) {
        return NULL;
    }

    log = nxe_json_log(pool);

    if (nxe_json_validate(NXE_JSON_CAST(root), 0, log) != NGX_OK) {
        json_decref(NXE_JSON_CAST(root));
        return NULL;
    }

    return root;
}


void
nxe_json_free(nxe_json_t *json)
{
    if (json != NULL) {
        json_decref(NXE_JSON_CAST(json));
    }
}


nxe_json_type_t
nxe_json_type(nxe_json_t *json)
{
    json_t *j = NXE_JSON_CAST(json);

    if (j == NULL) {
        return NXE_JSON_INVALID;
    }

    switch (json_typeof(j)) {

    case JSON_NULL:
        return NXE_JSON_NULL;

    case JSON_TRUE:
    case JSON_FALSE:
        return NXE_JSON_BOOLEAN;

    case JSON_INTEGER:
        return NXE_JSON_INTEGER;

    case JSON_REAL:
        return NXE_JSON_REAL;

    case JSON_STRING:
        return NXE_JSON_STRING;

    case JSON_ARRAY:
        return NXE_JSON_ARRAY;

    case JSON_OBJECT:
        return NXE_JSON_OBJECT;

    default:
        return NXE_JSON_INVALID;
    }
}


nxe_json_t *
nxe_json_object_get(nxe_json_t *json, const char *key)
{
    json_t *obj = NXE_JSON_CAST(json);

    if (obj == NULL || !json_is_object(obj) || key == NULL) {
        return NULL;
    }

    return (nxe_json_t *) json_object_get(obj, key);
}


nxe_json_t *
nxe_json_object_get_ns(nxe_json_t *json, ngx_str_t *key)
{
    json_t *obj = NXE_JSON_CAST(json);

    if (obj == NULL || !json_is_object(obj)
        || key == NULL || key->data == NULL)
    {
        return NULL;
    }

    return (nxe_json_t *) json_object_getn(
        obj, (const char *) key->data, key->len);
}


ngx_int_t
nxe_json_object_get_string(nxe_json_t *json, const char *key,
    ngx_str_t *value, ngx_pool_t *pool)
{
    nxe_json_t *v;
    ngx_str_t tmp;
    u_char *buf;

    if (value == NULL) {
        return NGX_ERROR;
    }

    value->data = NULL;
    value->len = 0;

    if (pool == NULL) {
        return NGX_ERROR;
    }

    v = nxe_json_object_get(json, key);
    if (v == NULL) {
        return NGX_DECLINED;
    }

    if (nxe_json_string(v, &tmp) != NGX_OK) {
        return NGX_DECLINED;
    }

    if (tmp.len == 0) {
        return NGX_OK;
    }

    buf = ngx_pnalloc(pool, tmp.len);
    if (buf == NULL) {
        ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                      "nxe_json: object_get_string alloc failed");
        return NGX_ERROR;
    }

    ngx_memcpy(buf, tmp.data, tmp.len);
    value->data = buf;
    value->len = tmp.len;

    return NGX_OK;
}


ngx_int_t
nxe_json_object_get_integer(nxe_json_t *json, const char *key,
    int64_t *value)
{
    nxe_json_t *v;

    if (value == NULL) {
        return NGX_ERROR;
    }

    *value = 0;

    v = nxe_json_object_get(json, key);
    if (v == NULL) {
        return NGX_DECLINED;
    }

    if (nxe_json_integer(v, value) != NGX_OK) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}


ngx_int_t
nxe_json_object_get_boolean(nxe_json_t *json, const char *key,
    ngx_flag_t *value)
{
    nxe_json_t *v;

    if (value == NULL) {
        return NGX_ERROR;
    }

    *value = 0;

    v = nxe_json_object_get(json, key);
    if (v == NULL) {
        return NGX_DECLINED;
    }

    if (nxe_json_boolean(v, value) != NGX_OK) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}


size_t
nxe_json_array_size(nxe_json_t *json)
{
    json_t *arr = NXE_JSON_CAST(json);

    if (arr == NULL || !json_is_array(arr)) {
        return 0;
    }

    return json_array_size(arr);
}


nxe_json_t *
nxe_json_array_get(nxe_json_t *json, size_t index)
{
    json_t *arr = NXE_JSON_CAST(json);

    if (arr == NULL || !json_is_array(arr)) {
        return NULL;
    }

    return (nxe_json_t *) json_array_get(arr, index);
}


size_t
nxe_json_object_size(nxe_json_t *json)
{
    json_t *obj = NXE_JSON_CAST(json);

    if (obj == NULL || !json_is_object(obj)) {
        return 0;
    }

    return json_object_size(obj);
}


nxe_json_iter_t *
nxe_json_object_iter(nxe_json_t *json)
{
    json_t *obj = NXE_JSON_CAST(json);

    if (obj == NULL || !json_is_object(obj)) {
        return NULL;
    }

    return (nxe_json_iter_t *) json_object_iter(obj);
}


nxe_json_iter_t *
nxe_json_object_iter_next(nxe_json_t *json, nxe_json_iter_t *iter)
{
    json_t *obj = NXE_JSON_CAST(json);

    if (obj == NULL || !json_is_object(obj) || iter == NULL) {
        return NULL;
    }

    return (nxe_json_iter_t *) json_object_iter_next(obj, (void *) iter);
}


ngx_int_t
nxe_json_object_iter_key(nxe_json_iter_t *iter, ngx_str_t *key)
{
    const char *k;
    size_t klen;

    if (key == NULL) {
        return NGX_ERROR;
    }

    key->data = NULL;
    key->len = 0;

    if (iter == NULL) {
        return NGX_ERROR;
    }

    k = json_object_iter_key((void *) iter);
    if (k == NULL) {
        return NGX_ERROR;
    }
    klen = json_object_iter_key_len((void *) iter);

    key->data = (u_char *) k;
    key->len = klen;
    return NGX_OK;
}


nxe_json_t *
nxe_json_object_iter_value(nxe_json_iter_t *iter)
{
    if (iter == NULL) {
        return NULL;
    }

    return (nxe_json_t *) json_object_iter_value((void *) iter);
}


ngx_int_t
nxe_json_string(nxe_json_t *json, ngx_str_t *value)
{
    json_t *j = NXE_JSON_CAST(json);
    const char *s;

    if (value == NULL) {
        return NGX_ERROR;
    }

    value->data = NULL;
    value->len = 0;

    if (j == NULL || !json_is_string(j)) {
        return NGX_ERROR;
    }

    s = json_string_value(j);
    if (s == NULL) {
        return NGX_ERROR;
    }

    value->data = (u_char *) s;
    value->len = json_string_length(j);

    return NGX_OK;
}


ngx_int_t
nxe_json_integer(nxe_json_t *json, int64_t *value)
{
    json_t *j = NXE_JSON_CAST(json);

    if (value == NULL) {
        return NGX_ERROR;
    }

    *value = 0;

    if (j == NULL || !json_is_integer(j)) {
        return NGX_ERROR;
    }

    *value = (int64_t) json_integer_value(j);

    return NGX_OK;
}


ngx_int_t
nxe_json_real(nxe_json_t *json, double *value)
{
    json_t *j = NXE_JSON_CAST(json);

    if (value == NULL) {
        return NGX_ERROR;
    }

    *value = 0.0;

    if (j == NULL || !json_is_real(j)) {
        return NGX_ERROR;
    }

    *value = json_real_value(j);

    return NGX_OK;
}


ngx_int_t
nxe_json_boolean(nxe_json_t *json, ngx_flag_t *value)
{
    json_t *j = NXE_JSON_CAST(json);

    if (value == NULL) {
        return NGX_ERROR;
    }

    *value = 0;

    if (j == NULL || (!json_is_true(j) && !json_is_false(j))) {
        return NGX_ERROR;
    }

    *value = json_is_true(j) ? 1 : 0;

    return NGX_OK;
}


ngx_int_t
nxe_json_number(nxe_json_t *json, double *value)
{
    json_t *j = NXE_JSON_CAST(json);

    if (value == NULL) {
        return NGX_ERROR;
    }

    *value = 0.0;

    if (j == NULL) {
        return NGX_ERROR;
    }

    if (json_is_integer(j)) {
        *value = (double) json_integer_value(j);
        return NGX_OK;
    }

    if (json_is_real(j)) {
        *value = json_real_value(j);
        return NGX_OK;
    }

    return NGX_ERROR;
}


nxe_json_t *
nxe_json_from_string(ngx_str_t *str)
{
    json_t *j;

    if (str == NULL || str->data == NULL
        || str->len > NXE_JSON_MAX_SIZE)
    {
        return NULL;
    }

    j = json_stringn((const char *) str->data, str->len);

    return (nxe_json_t *) j;
}


ngx_flag_t
nxe_json_equal(nxe_json_t *a, nxe_json_t *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }

    return json_equal(NXE_JSON_CAST(a), NXE_JSON_CAST(b)) ? 1 : 0;
}


ngx_int_t
nxe_json_compare(nxe_json_t *a, nxe_json_t *b, double *diff,
    ngx_log_t *log)
{
    int64_t ia, ib;
    double da, db;

    if (a == NULL || b == NULL || diff == NULL) {
        return NGX_ERROR;
    }

    *diff = 0.0;

    if (nxe_json_integer(a, &ia) == NGX_OK
        && nxe_json_integer(b, &ib) == NGX_OK)
    {
        *diff = (ia > ib) ? 1.0 : (ia < ib) ? -1.0 : 0.0;
        return NGX_OK;
    }

    if (nxe_json_integer(a, &ia) == NGX_OK
        && nxe_json_real(b, &db) == NGX_OK)
    {
        if (db >= (double) INT64_MIN && db < (double) INT64_MAX
            && db == (double) (int64_t) db)
        {
            ib = (int64_t) db;
            *diff = (ia > ib) ? 1.0 : (ia < ib) ? -1.0 : 0.0;
            return NGX_OK;
        }
    }

    if (nxe_json_real(a, &da) == NGX_OK
        && nxe_json_integer(b, &ib) == NGX_OK)
    {
        if (da >= (double) INT64_MIN && da < (double) INT64_MAX
            && da == (double) (int64_t) da)
        {
            ia = (int64_t) da;
            *diff = (ia > ib) ? 1.0 : (ia < ib) ? -1.0 : 0.0;
            return NGX_OK;
        }
    }

    /* Fail-closed before the double fallback: if either operand is an
     * integer whose magnitude exceeds NXE_JSON_MAX_SAFE_INTEGER, the
     * double conversion below could collapse it onto a neighbouring
     * value and silently flip authorization decisions. */
    if ((nxe_json_integer(a, &ia) == NGX_OK
         && !nxe_json_int_double_safe(ia))
        || (nxe_json_integer(b, &ib) == NGX_OK
            && !nxe_json_int_double_safe(ib)))
    {
        return NGX_ERROR;
    }

    if (nxe_json_number(a, &da) != NGX_OK
        || nxe_json_number(b, &db) != NGX_OK)
    {
        return NGX_ERROR;
    }

    if (isnan(da) || isnan(db) || isinf(da) || isinf(db)) {
        return NGX_ERROR;
    }

    if (log != NULL) {
        ngx_log_error(NGX_LOG_DEBUG, log, 0,
                      "nxe_json: compare via double fallback: "
                      "%f vs %f", da, db);
    }

    *diff = (da > db) ? 1.0 : (da < db) ? -1.0 : 0.0;

    return NGX_OK;
}


static ngx_str_t *
nxe_json_stringify_flags(nxe_json_t *json, ngx_pool_t *pool,
    size_t jansson_flags, const char *tag)
{
    json_t *j = NXE_JSON_CAST(json);
    ngx_str_t *result;
    ngx_log_t *log;
    char *s;
    size_t len;

    log = nxe_json_log(pool);

    if (j == NULL || pool == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_json: %s, NULL input", tag);
        return NULL;
    }

    /* JSON_ENCODE_ANY so scalar roots (accepted by JSON_DECODE_ANY in
     * nxe_json_parse) also serialize; plain json_dumps rejects them. */
    s = json_dumps(j, jansson_flags | JSON_ENCODE_ANY);
    if (s == NULL) {
        ngx_log_error(NGX_LOG_ERR, log, 0,
                      "nxe_json: %s, json_dumps failed", tag);
        return NULL;
    }

    len = ngx_strlen(s);

    result = ngx_palloc(pool, sizeof(ngx_str_t));
    if (result == NULL) {
        free(s);
        return NULL;
    }

    result->data = ngx_pnalloc(pool, len);
    if (result->data == NULL) {
        free(s);
        return NULL;
    }

    result->len = len;
    ngx_memcpy(result->data, s, len);

    free(s);

    return result;
}


ngx_str_t *
nxe_json_stringify_compact(nxe_json_t *json, ngx_pool_t *pool)
{
    return nxe_json_stringify_flags(json, pool, JSON_COMPACT,
                                    "stringify_compact");
}


ngx_str_t *
nxe_json_stringify_pretty(nxe_json_t *json, ngx_pool_t *pool,
    ngx_uint_t indent)
{
    if (indent == 0) {
        indent = 1;
    }
    if (indent > 31) {
        indent = 31;
    }

    return nxe_json_stringify_flags(json, pool, JSON_INDENT(indent),
                                    "stringify_pretty");
}
