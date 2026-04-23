/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * nxe_json.h - JSON abstraction layer (jansson wrapper)
 *
 * Thin wrapper around the Jansson JSON library.  Opaque handles hide
 * jansson from consumers so the underlying implementation can change
 * without affecting callers.
 *
 * Depends on: jansson (runtime), nginx core (types only).
 */

#ifndef _NXE_JSON_H_INCLUDED_
#define _NXE_JSON_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>

#include <stdint.h>


/*
 * Opaque JSON value handle.
 *
 * Underlying implementation is jansson's json_t, but callers must
 * treat this as an opaque pointer and use the nxe_json_* API.
 */
typedef void nxe_json_t;


/*
 * JSON value types.
 *
 * Maps to jansson's json_type but exposed as an abstraction.
 */
typedef enum {
    NXE_JSON_INVALID = -1,      /* NULL handle / unknown type */
    NXE_JSON_NULL,              /* JSON null */
    NXE_JSON_BOOLEAN,           /* JSON true or false */
    NXE_JSON_INTEGER,           /* JSON integer */
    NXE_JSON_REAL,              /* JSON real (floating point) */
    NXE_JSON_STRING,            /* JSON string */
    NXE_JSON_ARRAY,             /* JSON array */
    NXE_JSON_OBJECT             /* JSON object */
} nxe_json_type_t;


/*
 * Limits applied by nxe_json_parse() (size only) and
 * nxe_json_parse_untrusted() (size + structural limits).
 */
#define NXE_JSON_MAX_SIZE           (1 * 1024 * 1024)   /* 1 MiB */
#define NXE_JSON_MAX_DEPTH          10
#define NXE_JSON_MAX_ARRAY_SIZE     100
#define NXE_JSON_MAX_STRING_LENGTH  4096
#define NXE_JSON_MAX_OBJECT_KEYS    256


/*
 * Parse a JSON string (trusted input).
 *
 * Enforces NXE_JSON_MAX_SIZE on input length and rejects duplicate keys.
 * Accepts any JSON value (object / array / scalar) at the root.
 *
 * The caller owns the returned handle and must release it with
 * nxe_json_free().
 *
 * @param[in] data  input bytes (binary-safe)
 * @param[in] pool  nginx pool (used for logging only; may be NULL to
 *                  suppress logging)
 *
 * @return parsed handle, or NULL on error
 */
nxe_json_t *nxe_json_parse(ngx_str_t *data, ngx_pool_t *pool);


/*
 * Parse a JSON string from an untrusted source (with DoS protections).
 *
 * In addition to nxe_json_parse()'s size / duplicate-key checks,
 * validates the parsed structure against:
 *   - NXE_JSON_MAX_DEPTH         (nesting depth)
 *   - NXE_JSON_MAX_ARRAY_SIZE    (array element count)
 *   - NXE_JSON_MAX_STRING_LENGTH (string byte length, keys included)
 *   - NXE_JSON_MAX_OBJECT_KEYS   (object key count)
 *
 * Use this for JSON from the network (HTTP bodies, JWKS, metadata
 * responses, userinfo, signed tokens, etc.).  A valid signature proves
 * authenticity, not structural safety, so signed JWT payloads still
 * belong here.  Reserve nxe_json_parse() for JSON produced by
 * components under the same trust boundary, such as a trusted session
 * store.
 *
 * @param[in] data  input bytes (binary-safe)
 * @param[in] pool  nginx pool (used for logging only; may be NULL)
 *
 * @return parsed handle, or NULL on error
 */
nxe_json_t *nxe_json_parse_untrusted(ngx_str_t *data, ngx_pool_t *pool);


/*
 * Release a JSON handle previously returned by nxe_json_parse(),
 * nxe_json_parse_untrusted(), or nxe_json_from_string().
 *
 * Safe to call with NULL.  Borrowed references from nxe_json_object_get
 * or nxe_json_array_get must NOT be passed here.
 */
void nxe_json_free(nxe_json_t *json);


/*
 * Determine the type of a JSON value.  Returns NXE_JSON_INVALID for a
 * NULL handle.
 */
nxe_json_type_t nxe_json_type(nxe_json_t *json);


/*
 * Fetch a member of a JSON object by key.
 *
 * The returned pointer is a borrowed reference owned by the parent
 * object; it must not be passed to nxe_json_free() and becomes invalid
 * when the parent is freed.
 *
 * Returns NULL if the handle is not an object, the key is missing,
 * or key is NULL.
 *
 * @param[in] json  JSON object
 * @param[in] key   NUL-terminated key
 */
nxe_json_t *nxe_json_object_get(nxe_json_t *json, const char *key);


/*
 * Binary-safe variant of nxe_json_object_get().
 *
 * Looks up the key using exactly key->len bytes, so embedded NUL bytes
 * are handled correctly.
 */
nxe_json_t *nxe_json_object_get_ns(nxe_json_t *json, ngx_str_t *key);


/*
 * Convenience: fetch a string member and copy it into the pool.
 *
 * *value is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared (value->data = NULL, value->len = 0) as a defensive
 * safeguard against callers that forget to check the return value;
 * callers must still check the return value before reading *value.
 *
 * @return NGX_OK on success (value->data allocated on pool),
 *         NGX_DECLINED if the key is missing or not a string,
 *         NGX_ERROR if value or pool is NULL, or on allocation failure.
 */
ngx_int_t nxe_json_object_get_string(nxe_json_t *json, const char *key,
    ngx_str_t *value, ngx_pool_t *pool);


/*
 * Convenience: fetch an integer member by key.
 *
 * Does not allocate; *value is written directly from the underlying
 * jansson node.  Missing key and wrong value type collapse into the
 * same NGX_DECLINED result; callers that need to distinguish them
 * should still use nxe_json_object_get() + nxe_json_integer().
 *
 * *value is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared as a defensive safeguard; callers must still check the
 * return value before reading *value.
 *
 * @return NGX_OK on success,
 *         NGX_DECLINED if json is NULL / not an object, key is NULL,
 *         the key is missing, or the value is not an integer,
 *         NGX_ERROR if value is NULL.
 */
ngx_int_t nxe_json_object_get_integer(nxe_json_t *json, const char *key,
    int64_t *value);


/*
 * Convenience: fetch a boolean member by key.
 *
 * Does not allocate; *value is written directly (1 for true, 0 for
 * false).  Missing key and wrong value type collapse into the same
 * NGX_DECLINED result; callers that need to distinguish them should
 * still use nxe_json_object_get() + nxe_json_boolean().
 *
 * *value is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared as a defensive safeguard; callers must still check the
 * return value before reading *value.
 *
 * @return NGX_OK on success (*value set to 1 for true, 0 for false),
 *         NGX_DECLINED if json is NULL / not an object, key is NULL,
 *         the key is missing, or the value is not a boolean,
 *         NGX_ERROR if value is NULL.
 */
ngx_int_t nxe_json_object_get_boolean(nxe_json_t *json, const char *key,
    ngx_flag_t *value);


/*
 * Array length.  Returns 0 if the handle is not an array.
 */
size_t nxe_json_array_size(nxe_json_t *json);


/*
 * Array element lookup.  Returns a borrowed reference; see
 * nxe_json_object_get() for ownership rules.
 */
nxe_json_t *nxe_json_array_get(nxe_json_t *json, size_t index);


/*
 * Extract a string value.
 *
 * On success, value->data points into jansson-owned storage and remains
 * valid until the parent is freed.  The string is binary-safe.
 *
 * *value is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared (value->data = NULL, value->len = 0) as a defensive
 * safeguard; callers must still check the return value before reading
 * *value.
 *
 * @return NGX_OK on success, NGX_ERROR if the handle is not a string.
 */
ngx_int_t nxe_json_string(nxe_json_t *json, ngx_str_t *value);


/*
 * Extract an integer value.
 *
 * *value is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared as a defensive safeguard; callers must still check the
 * return value before reading *value.
 *
 * @return NGX_OK on success, NGX_ERROR if the handle is not an integer.
 */
ngx_int_t nxe_json_integer(nxe_json_t *json, int64_t *value);


/*
 * Extract a real (double) value.
 *
 * *value is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared as a defensive safeguard; callers must still check the
 * return value before reading *value.
 *
 * @return NGX_OK on success, NGX_ERROR if the handle is not a real.
 */
ngx_int_t nxe_json_real(nxe_json_t *json, double *value);


/*
 * Extract a boolean value (1 for true, 0 for false).
 *
 * *value is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared as a defensive safeguard; callers must still check the
 * return value before reading *value.
 *
 * @return NGX_OK on success, NGX_ERROR if the handle is not a boolean.
 */
ngx_int_t nxe_json_boolean(nxe_json_t *json, ngx_flag_t *value);


/*
 * Extract a numeric value as double (accepts integer or real).
 *
 * Precision note: integer values with magnitude above 2^53 lose
 * precision when converted to double.  For precision-preserving
 * integer comparison, use nxe_json_compare() instead.
 *
 * *value is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared as a defensive safeguard; callers must still check the
 * return value before reading *value.
 *
 * @return NGX_OK on success, NGX_ERROR if the handle is not a number.
 */
ngx_int_t nxe_json_number(nxe_json_t *json, double *value);


/*
 * Create a JSON string node from an nginx string (binary-safe).
 *
 * The returned handle is owned by the caller and must be released with
 * nxe_json_free().  Returns NULL if str is NULL or exceeds
 * NXE_JSON_MAX_SIZE.
 */
nxe_json_t *nxe_json_from_string(ngx_str_t *str);


/*
 * Deep equality comparison.  Returns 1 if a and b represent the same
 * JSON value, 0 otherwise (including when either operand is NULL).
 */
ngx_flag_t nxe_json_equal(nxe_json_t *a, nxe_json_t *b);


/*
 * Precision-aware numeric comparison.
 *
 * Fail-closed for lossy comparisons:
 *   - Both integers: compared with int64_t precision.
 *   - Mixed integer/real: real promoted to int64_t when lossless.
 *   - Otherwise: double comparison.  Before converting, any integer
 *     operand whose magnitude exceeds 2^53 is rejected with
 *     NGX_ERROR, because the double cast would collapse it onto a
 *     neighbouring value and could flip authorization results.
 *   - NaN / Inf operands return NGX_ERROR.
 *
 * On success *diff is -1.0, 0.0, or 1.0 (sign of a - b).
 *
 * *diff is only meaningful when NGX_OK is returned.  On failure it is
 * zero-cleared as a defensive safeguard; callers must still check the
 * return value before reading *diff.
 *
 * @return NGX_OK on success, NGX_ERROR on non-numeric input or a lossy
 *         comparison that must be rejected.
 */
ngx_int_t nxe_json_compare(nxe_json_t *a, nxe_json_t *b,
    double *diff, ngx_log_t *log);


/*
 * Serialize to a compact (no whitespace) JSON string, allocated on pool.
 *
 * @return allocated ngx_str_t, or NULL on error.
 */
ngx_str_t *nxe_json_stringify_compact(nxe_json_t *json, ngx_pool_t *pool);


/*
 * Serialize to a pretty-printed JSON string, allocated on pool.
 *
 * Wraps jansson's JSON_INDENT, so members are placed on separate lines
 * with `indent` spaces per nesting level.  `indent` is clamped to
 * [1, 31] (jansson's supported range); 0 is promoted to 1 so the
 * result stays distinct from stringify_compact (JSON_INDENT(0) would
 * still emit newlines with no indentation).
 *
 * @param[in] json    JSON value to serialize
 * @param[in] pool    nginx pool for the result
 * @param[in] indent  spaces per indent level (clamped to [1, 31];
 *                    0 is promoted to 1)
 *
 * @return allocated ngx_str_t, or NULL on error.
 */
ngx_str_t *nxe_json_stringify_pretty(nxe_json_t *json, ngx_pool_t *pool,
    ngx_uint_t indent);


/*
 * Type predicates.
 */

static ngx_inline ngx_int_t
nxe_json_is_null(nxe_json_t *json)
{
    return nxe_json_type(json) == NXE_JSON_NULL;
}


static ngx_inline ngx_int_t
nxe_json_is_boolean(nxe_json_t *json)
{
    return nxe_json_type(json) == NXE_JSON_BOOLEAN;
}


static ngx_inline ngx_int_t
nxe_json_is_integer(nxe_json_t *json)
{
    return nxe_json_type(json) == NXE_JSON_INTEGER;
}


static ngx_inline ngx_int_t
nxe_json_is_real(nxe_json_t *json)
{
    return nxe_json_type(json) == NXE_JSON_REAL;
}


static ngx_inline ngx_int_t
nxe_json_is_string(nxe_json_t *json)
{
    return nxe_json_type(json) == NXE_JSON_STRING;
}


static ngx_inline ngx_int_t
nxe_json_is_array(nxe_json_t *json)
{
    return nxe_json_type(json) == NXE_JSON_ARRAY;
}


static ngx_inline ngx_int_t
nxe_json_is_object(nxe_json_t *json)
{
    return nxe_json_type(json) == NXE_JSON_OBJECT;
}


#endif /* _NXE_JSON_H_INCLUDED_ */
