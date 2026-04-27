/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * test_runner.c - nxe-json standalone C unit tests.
 *
 * Runs against the real nxe_json.c (not a mock) using the jansson
 * library and the ngx_compat stubs.  Each TEST() function is a single
 * test case; RUN(name) executes it and records the result.
 *
 * Build: cd tests && make test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "nxe_json.h"


typedef struct {
    int  total;
    int  passed;
    int  failed;
    int  last_failed;
} test_stats_t;


#define TEST(name) \
        static void test_ ## name(test_stats_t * stats, ngx_pool_t * pool)

#define RUN(name) do {                                                      \
            ngx_pool_t *_pool = ngx_create_pool(0, &log);                       \
            stats.total++;                                                      \
            stats.last_failed = 0;                                              \
            printf("test_%s ... ", #name);                                      \
            fflush(stdout);                                                     \
            test_ ## name(&stats, _pool);                                         \
            if (stats.last_failed) {                                            \
                printf("FAILED\n");                                             \
                stats.failed++;                                                 \
            } else {                                                            \
                printf("ok\n");                                                 \
                stats.passed++;                                                 \
            }                                                                   \
            ngx_destroy_pool(_pool);                                            \
} while (0)

#define FAIL_HERE(fmt, ...) do {                                            \
            fprintf(stderr, "\n    %s:%d: " fmt,                                \
                    __FILE__, __LINE__, ## __VA_ARGS__);                         \
            stats->last_failed = 1;                                             \
} while (0)

#define ASSERT(expr) do {                                                   \
            if (!(expr)) {                                                      \
                FAIL_HERE("assertion failed: %s", #expr);                       \
                return;                                                         \
            }                                                                   \
} while (0)

#define ASSERT_EQ_INT(actual, expected) do {                                \
            long long _a = (long long) (actual);                                \
            long long _e = (long long) (expected);                              \
            if (_a != _e) {                                                     \
                FAIL_HERE("expected %lld, got %lld (%s)",                       \
                          _e, _a, #actual);                                     \
                return;                                                         \
            }                                                                   \
} while (0)

#define ASSERT_EQ_DOUBLE(actual, expected) do {                             \
            double _a = (double) (actual);                                      \
            double _e = (double) (expected);                                    \
            if (_a != _e) {                                                     \
                FAIL_HERE("expected %f, got %f (%s)", _e, _a, #actual);         \
                return;                                                         \
            }                                                                   \
} while (0)

#define ASSERT_STR_EQ(value, expected) do {                                 \
            const char *_e = (expected);                                        \
            size_t _elen = strlen(_e);                                          \
            if ((value).len != _elen                                            \
                || memcmp((value).data, _e, _elen) != 0)                        \
            {                                                                   \
                FAIL_HERE("expected \"%s\" (len=%zu), got len=%zu",             \
                          _e, _elen, (value).len);                              \
                return;                                                         \
            }                                                                   \
} while (0)


/* --- helper: build ngx_str_t from a C literal (non-binary-safe) --- */

static ngx_str_t
sz(const char *s)
{
    ngx_str_t str;

    str.data = (u_char *) s;
    str.len = (s != NULL) ? strlen(s) : 0;

    return str;
}


/* ============================================================ */
/* parse / parse_untrusted                                       */
/* ============================================================ */

TEST(parse_object){
    ngx_str_t input = sz("{\"a\":1,\"b\":\"x\"}");
    nxe_json_t *root;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT(nxe_json_is_object(root));
    ASSERT_EQ_INT(nxe_json_type(root), NXE_JSON_OBJECT);

    nxe_json_free(root);
}


TEST(parse_array){
    ngx_str_t input = sz("[1,2,3]");
    nxe_json_t *root;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT(nxe_json_is_array(root));
    ASSERT_EQ_INT(nxe_json_array_size(root), 3);

    nxe_json_free(root);
}


TEST(parse_scalar_accepted){
    ngx_str_t input = sz("42");
    nxe_json_t *root;
    int64_t iv = 0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(nxe_json_integer(root, &iv), NGX_OK);
    ASSERT_EQ_INT(iv, 42);

    nxe_json_free(root);
}


TEST(parse_null_input){
    ASSERT(nxe_json_parse(NULL, pool) == NULL);
}


TEST(parse_empty){
    ngx_str_t input = { 0, NULL };

    ASSERT(nxe_json_parse(&input, pool) == NULL);
}


TEST(parse_size_limit){
    size_t len = NXE_JSON_MAX_SIZE + 1;
    u_char *buf = malloc(len);
    ngx_str_t input;

    ASSERT(buf != NULL);
    memset(buf, ' ', len);
    buf[0] = '"';
    buf[len - 1] = '"';

    input.data = buf;
    input.len = len;

    ASSERT(nxe_json_parse(&input, pool) == NULL);

    free(buf);
}


TEST(parse_invalid_json){
    ngx_str_t input = sz("{not-json}");

    ASSERT(nxe_json_parse(&input, pool) == NULL);
}


TEST(parse_null_pool){
    /* pool == NULL is a documented suppress-logging contract */
    ngx_str_t good = sz("{\"a\":1}");
    ngx_str_t bad = sz("{not-json}");
    nxe_json_t *root;

    (void) pool;

    root = nxe_json_parse(&good, NULL);
    ASSERT(root != NULL);
    nxe_json_free(root);

    ASSERT(nxe_json_parse(&bad, NULL) == NULL);
}


TEST(parse_reject_duplicate_keys){
    ngx_str_t input = sz("{\"a\":1,\"a\":2}");

    ASSERT(nxe_json_parse(&input, pool) == NULL);
}


TEST(parse_untrusted_ok){
    ngx_str_t input = sz("{\"k\":[1,2]}");
    nxe_json_t *root;

    root = nxe_json_parse_untrusted(&input, pool);
    ASSERT(root != NULL);
    nxe_json_free(root);
}


TEST(parse_untrusted_depth_limit){
    /* build nested arrays deeper than NXE_JSON_MAX_DEPTH */
    size_t n = NXE_JSON_MAX_DEPTH + 5;
    size_t len = n * 2 + 1;
    char *buf = malloc(len + 1);
    ngx_str_t input;
    size_t i;

    ASSERT(buf != NULL);
    for (i = 0; i < n; i++) {
        buf[i] = '[';
    }
    buf[n] = '1';
    for (i = 0; i < n; i++) {
        buf[n + 1 + i] = ']';
    }
    buf[len] = '\0';

    input.data = (u_char *) buf;
    input.len = len;

    /* trusted parse accepts, untrusted rejects */
    nxe_json_t *ok = nxe_json_parse(&input, pool);
    ASSERT(ok != NULL);
    nxe_json_free(ok);

    ASSERT(nxe_json_parse_untrusted(&input, pool) == NULL);

    free(buf);
}


TEST(parse_untrusted_array_limit){
    size_t n = NXE_JSON_MAX_ARRAY_SIZE + 1;
    size_t cap = n * 4 + 8;
    char *buf = malloc(cap);
    size_t off = 0;
    size_t i;
    ngx_str_t input;

    ASSERT(buf != NULL);
    buf[off++] = '[';
    for (i = 0; i < n; i++) {
        if (i > 0) {
            buf[off++] = ',';
        }
        buf[off++] = '1';
    }
    buf[off++] = ']';

    input.data = (u_char *) buf;
    input.len = off;

    ASSERT(nxe_json_parse_untrusted(&input, pool) == NULL);

    free(buf);
}


TEST(parse_untrusted_string_limit){
    size_t payload = NXE_JSON_MAX_STRING_LENGTH + 1;
    size_t len = payload + 2;
    char *buf = malloc(len + 1);
    ngx_str_t input;

    ASSERT(buf != NULL);
    buf[0] = '"';
    memset(buf + 1, 'a', payload);
    buf[len - 1] = '"';
    buf[len] = '\0';

    input.data = (u_char *) buf;
    input.len = len;

    ASSERT(nxe_json_parse_untrusted(&input, pool) == NULL);

    free(buf);
}


TEST(parse_untrusted_object_key_length_limit){
    /* a single key whose length exceeds NXE_JSON_MAX_STRING_LENGTH
     * must be rejected by the per-key length check in validate. */
    size_t keylen = NXE_JSON_MAX_STRING_LENGTH + 1;
    size_t cap = keylen + 16;
    char *buf = malloc(cap);
    size_t off = 0;
    ngx_str_t input;

    ASSERT(buf != NULL);
    buf[off++] = '{';
    buf[off++] = '"';
    memset(buf + off, 'k', keylen);
    off += keylen;
    buf[off++] = '"';
    buf[off++] = ':';
    buf[off++] = '1';
    buf[off++] = '}';

    input.data = (u_char *) buf;
    input.len = off;

    ASSERT(nxe_json_parse_untrusted(&input, pool) == NULL);

    free(buf);
}


TEST(parse_untrusted_object_keys_limit){
    /* NXE_JSON_MAX_OBJECT_KEYS + 1 keys: "k0":0,"k1":1,... */
    size_t n = NXE_JSON_MAX_OBJECT_KEYS + 1;
    size_t cap = n * 20 + 16;
    char *buf = malloc(cap);
    size_t off = 0;
    size_t i;
    ngx_str_t input;

    ASSERT(buf != NULL);
    buf[off++] = '{';
    for (i = 0; i < n; i++) {
        int w;

        if (i > 0) {
            buf[off++] = ',';
        }
        w = snprintf(buf + off, cap - off, "\"k%zu\":%zu", i, i);
        ASSERT(w > 0 && (size_t) w < cap - off);
        off += (size_t) w;
    }
    ASSERT(off < cap);
    buf[off++] = '}';

    input.data = (u_char *) buf;
    input.len = off;

    ASSERT(nxe_json_parse_untrusted(&input, pool) == NULL);

    free(buf);
}


/* ============================================================ */
/* object_get / object_get_ns                                    */
/* ============================================================ */

TEST(object_get_basic){
    ngx_str_t input = sz("{\"hello\":\"world\"}");
    nxe_json_t *root;
    nxe_json_t *v;
    ngx_str_t s;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    v = nxe_json_object_get(root, "hello");
    ASSERT(v != NULL);
    ASSERT(nxe_json_is_string(v));
    ASSERT_EQ_INT(nxe_json_string(v, &s), NGX_OK);
    ASSERT_STR_EQ(s, "world");

    ASSERT(nxe_json_object_get(root, "nope") == NULL);
    ASSERT(nxe_json_object_get(root, NULL) == NULL);

    nxe_json_free(root);
}


TEST(object_get_on_non_object){
    ngx_str_t input = sz("[1,2]");
    nxe_json_t *root;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT(nxe_json_object_get(root, "x") == NULL);
    nxe_json_free(root);
}


TEST(object_get_ns_null_inputs){
    ngx_str_t input = sz("{\"a\":1}");
    nxe_json_t *root;
    ngx_str_t key = sz("a");
    ngx_str_t null_data = { 1, NULL };

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT(nxe_json_object_get_ns(NULL, &key) == NULL);
    ASSERT(nxe_json_object_get_ns(root, NULL) == NULL);
    ASSERT(nxe_json_object_get_ns(root, &null_data) == NULL);

    nxe_json_free(root);
}


TEST(object_get_ns_basic){
    /* object_get_ns accepts an ngx_str_t* key and performs the lookup
     * with an explicit length — no NUL terminator required on the
     * caller side.  (jansson rejects literal NUL bytes in keys at
     * parse time, so we exercise the length-based lookup with a key
     * that is a prefix of a longer string.) */
    ngx_str_t input = sz("{\"user\":1,\"username\":2}");
    nxe_json_t *root;
    nxe_json_t *v;
    u_char key_buf[] = "username-extra";
    ngx_str_t key = { 4, key_buf };     /* "user" slice */
    int64_t iv = 0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    v = nxe_json_object_get_ns(root, &key);
    ASSERT(v != NULL);
    ASSERT_EQ_INT(nxe_json_integer(v, &iv), NGX_OK);
    ASSERT_EQ_INT(iv, 1);

    key.len = 8;
    v = nxe_json_object_get_ns(root, &key);
    ASSERT(v != NULL);
    ASSERT_EQ_INT(nxe_json_integer(v, &iv), NGX_OK);
    ASSERT_EQ_INT(iv, 2);

    nxe_json_free(root);
}


TEST(object_get_string_convenience){
    ngx_str_t input = sz("{\"s\":\"hi\",\"n\":3,\"e\":\"\"}");
    nxe_json_t *root;
    ngx_str_t v;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_object_get_string(root, "s", &v, pool),
                  NGX_OK);
    ASSERT_STR_EQ(v, "hi");

    ASSERT_EQ_INT(nxe_json_object_get_string(root, "missing", &v, pool),
                  NGX_DECLINED);
    ASSERT_EQ_INT(nxe_json_object_get_string(root, "n", &v, pool),
                  NGX_DECLINED);

    ASSERT_EQ_INT(nxe_json_object_get_string(root, "e", &v, pool),
                  NGX_OK);
    ASSERT_EQ_INT(v.len, 0);

    nxe_json_free(root);
}


TEST(object_get_integer_convenience){
    ngx_str_t input =
        sz("{\"exp\":1700000000,\"s\":\"x\",\"r\":1.5,\"z\":null}");
    ngx_str_t arr_input = sz("[1,2,3]");
    nxe_json_t *root, *arr;
    int64_t n;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_object_get_integer(root, "exp", &n), NGX_OK);
    ASSERT_EQ_INT(n, 1700000000);

    /* missing key */
    ASSERT_EQ_INT(nxe_json_object_get_integer(root, "missing", &n),
                  NGX_DECLINED);

    /* wrong type: string */
    ASSERT_EQ_INT(nxe_json_object_get_integer(root, "s", &n),
                  NGX_DECLINED);

    /* wrong type: real (not integer) */
    ASSERT_EQ_INT(nxe_json_object_get_integer(root, "r", &n),
                  NGX_DECLINED);

    /* wrong type: null */
    ASSERT_EQ_INT(nxe_json_object_get_integer(root, "z", &n),
                  NGX_DECLINED);

    /* NULL key (delegated to object_get which returns NULL) */
    ASSERT_EQ_INT(nxe_json_object_get_integer(root, NULL, &n),
                  NGX_DECLINED);

    /* NULL out param */
    ASSERT_EQ_INT(nxe_json_object_get_integer(root, "exp", NULL),
                  NGX_ERROR);

    /* NULL root: object_get returns NULL -> DECLINED */
    ASSERT_EQ_INT(nxe_json_object_get_integer(NULL, "exp", &n),
                  NGX_DECLINED);

    /* non-object root */
    arr = nxe_json_parse(&arr_input, pool);
    ASSERT(arr != NULL);
    ASSERT_EQ_INT(nxe_json_object_get_integer(arr, "exp", &n),
                  NGX_DECLINED);
    nxe_json_free(arr);

    nxe_json_free(root);
}


TEST(object_get_boolean_convenience){
    ngx_str_t input =
        sz("{\"active\":true,\"inactive\":false,\"n\":1,"
           "\"r\":1.5,\"s\":\"x\",\"z\":null}");
    nxe_json_t *root;
    ngx_flag_t b;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "active", &b),
                  NGX_OK);
    ASSERT_EQ_INT(b, 1);

    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "inactive", &b),
                  NGX_OK);
    ASSERT_EQ_INT(b, 0);

    /* missing key */
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "missing", &b),
                  NGX_DECLINED);

    /* wrong type: integer */
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "n", &b),
                  NGX_DECLINED);

    /* wrong type: real */
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "r", &b),
                  NGX_DECLINED);

    /* wrong type: string */
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "s", &b),
                  NGX_DECLINED);

    /* wrong type: null */
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "z", &b),
                  NGX_DECLINED);

    /* NULL key */
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, NULL, &b),
                  NGX_DECLINED);

    /* NULL out param */
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "active", NULL),
                  NGX_ERROR);

    /* NULL root: object_get returns NULL -> DECLINED */
    ASSERT_EQ_INT(nxe_json_object_get_boolean(NULL, "active", &b),
                  NGX_DECLINED);

    nxe_json_free(root);
}


/* ============================================================ */
/* array operations                                              */
/* ============================================================ */

TEST(array_basic){
    ngx_str_t input = sz("[\"a\",\"b\",\"c\"]");
    nxe_json_t *root, *v;
    ngx_str_t s;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(nxe_json_array_size(root), 3);

    v = nxe_json_array_get(root, 1);
    ASSERT(v != NULL);
    ASSERT_EQ_INT(nxe_json_string(v, &s), NGX_OK);
    ASSERT_STR_EQ(s, "b");

    ASSERT(nxe_json_array_get(root, 99) == NULL);
    ASSERT_EQ_INT(nxe_json_array_size(NULL), 0);

    nxe_json_free(root);
}


TEST(array_on_non_array){
    ngx_str_t input = sz("{\"a\":1}");
    nxe_json_t *root;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(nxe_json_array_size(root), 0);
    ASSERT(nxe_json_array_get(root, 0) == NULL);
    nxe_json_free(root);
}


/* ============================================================ */
/* object iteration                                              */
/* ============================================================ */

TEST(object_size_basic){
    ngx_str_t input = sz("{\"a\":1,\"b\":2,\"c\":3}");
    nxe_json_t *root;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(nxe_json_object_size(root), 3);
    ASSERT_EQ_INT(nxe_json_object_size(NULL), 0);
    nxe_json_free(root);
}


TEST(object_size_on_non_object){
    ngx_str_t input = sz("[1,2,3]");
    nxe_json_t *root;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(nxe_json_object_size(root), 0);
    nxe_json_free(root);
}


TEST(object_iter_walks_all_keys){
    ngx_str_t input = sz("{\"a\":1,\"b\":2,\"c\":3}");
    nxe_json_t *root;
    nxe_json_iter_t *it;
    ngx_str_t key;
    int seen = 0;
    int64_t v;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    for (it = nxe_json_object_iter(root);
         it != NULL;
         it = nxe_json_object_iter_next(root, it))
    {
        ASSERT_EQ_INT(nxe_json_object_iter_key(it, &key), NGX_OK);
        ASSERT_EQ_INT(key.len, 1);
        ASSERT_EQ_INT(nxe_json_integer(nxe_json_object_iter_value(it), &v),
                      NGX_OK);
        if (key.data[0] == 'a') {
            ASSERT_EQ_INT(v, 1);
            seen |= 1;
        } else if (key.data[0] == 'b') {
            ASSERT_EQ_INT(v, 2);
            seen |= 2;
        } else if (key.data[0] == 'c') {
            ASSERT_EQ_INT(v, 3);
            seen |= 4;
        } else {
            /* unexpected key surfaced by the iterator */
            ASSERT(0);
        }
    }
    ASSERT_EQ_INT(seen, 7);

    nxe_json_free(root);
}


TEST(object_iter_preserves_insertion_order){
    /* jansson's documented guarantee, also asserted by our header
     * comment.  Lock it in so a future backend swap cannot regress
     * silently. */
    ngx_str_t input = sz("{\"c\":1,\"a\":2,\"b\":3}");
    nxe_json_t *root;
    nxe_json_iter_t *it;
    ngx_str_t key;
    const char expected[] = { 'c', 'a', 'b' };
    size_t i = 0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    for (it = nxe_json_object_iter(root);
         it != NULL;
         it = nxe_json_object_iter_next(root, it))
    {
        ASSERT(i < sizeof(expected));
        ASSERT_EQ_INT(nxe_json_object_iter_key(it, &key), NGX_OK);
        ASSERT_EQ_INT(key.len, 1);
        ASSERT_EQ_INT(key.data[0], expected[i]);
        i++;
    }
    ASSERT_EQ_INT(i, sizeof(expected));

    nxe_json_free(root);
}


TEST(object_iter_empty_object){
    ngx_str_t input = sz("{}");
    nxe_json_t *root;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT(nxe_json_object_iter(root) == NULL);
    nxe_json_free(root);
}


TEST(object_iter_on_non_object){
    ngx_str_t input = sz("[1,2,3]");
    nxe_json_t *root;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT(nxe_json_object_iter(root) == NULL);
    /* Lock in the non-object guard for iter_next as well: the iter
     * pointer is intentionally bogus because iter_next must reject
     * the call before dereferencing it. */
    ASSERT(nxe_json_object_iter_next(root,
                                     (nxe_json_iter_t *) 0xdead) == NULL);
    nxe_json_free(root);
}


TEST(object_iter_null_inputs){
    ngx_str_t key;
    ASSERT(nxe_json_object_iter(NULL) == NULL);
    ASSERT(nxe_json_object_iter_next(NULL, NULL) == NULL);
    ASSERT_EQ_INT(nxe_json_object_iter_key(NULL, &key), NGX_ERROR);
    ASSERT_EQ_INT(nxe_json_object_iter_key((nxe_json_iter_t *) 0xdead,
                                           NULL),
                  NGX_ERROR);
    ASSERT(nxe_json_object_iter_value(NULL) == NULL);
}


TEST(object_iter_key_returns_borrowed_view){
    /* Confirms that the returned ngx_str_t aliases jansson-owned
     * storage and is binary-safe (length-tracked, not NUL-terminated
     * via the C API).  We do not need an embedded NUL here because
     * the parser strips them; we only verify the length matches the
     * key bytes. */
    ngx_str_t input = sz("{\"long-kid-name\":\"v\"}");
    nxe_json_t *root;
    nxe_json_iter_t *it;
    ngx_str_t key;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    it = nxe_json_object_iter(root);
    ASSERT(it != NULL);
    ASSERT_EQ_INT(nxe_json_object_iter_key(it, &key), NGX_OK);
    ASSERT_EQ_INT(key.len, sizeof("long-kid-name") - 1);
    ASSERT(ngx_strncmp(key.data, "long-kid-name", key.len) == 0);
    nxe_json_free(root);
}


/* ============================================================ */
/* scalar extraction                                             */
/* ============================================================ */

TEST(string_extraction){
    /* Values parsed from JSON cannot contain embedded NUL bytes
     * (jansson rejects \u0000 by default).  Binary-safe handling on
     * the caller-construction side is covered by from_string_binary_safe;
     * here we verify that parsed strings return correct length + data. */
    ngx_str_t input = sz("{\"s\":\"hello\"}");
    nxe_json_t *root, *v;
    ngx_str_t s;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    v = nxe_json_object_get(root, "s");
    ASSERT(v != NULL);
    ASSERT_EQ_INT(nxe_json_string(v, &s), NGX_OK);
    ASSERT_STR_EQ(s, "hello");

    /* type mismatch: integer is not a string */
    ngx_str_t input2 = sz("{\"i\":1}");
    nxe_json_t *r2 = nxe_json_parse(&input2, pool);
    ASSERT(r2 != NULL);
    ASSERT_EQ_INT(nxe_json_string(nxe_json_object_get(r2, "i"), &s),
                  NGX_ERROR);
    nxe_json_free(r2);

    nxe_json_free(root);
}


TEST(integer_real_boolean){
    ngx_str_t input = sz(
        "{\"i\":-7,\"r\":3.25,\"t\":true,\"f\":false,\"n\":null}");
    nxe_json_t *root, *v;
    int64_t iv = 0;
    double dv = 0.0;
    ngx_flag_t bv = 0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    v = nxe_json_object_get(root, "i");
    ASSERT_EQ_INT(nxe_json_integer(v, &iv), NGX_OK);
    ASSERT_EQ_INT(iv, -7);

    v = nxe_json_object_get(root, "r");
    ASSERT_EQ_INT(nxe_json_real(v, &dv), NGX_OK);
    ASSERT_EQ_DOUBLE(dv, 3.25);

    v = nxe_json_object_get(root, "t");
    ASSERT_EQ_INT(nxe_json_boolean(v, &bv), NGX_OK);
    ASSERT_EQ_INT(bv, 1);

    v = nxe_json_object_get(root, "f");
    ASSERT_EQ_INT(nxe_json_boolean(v, &bv), NGX_OK);
    ASSERT_EQ_INT(bv, 0);

    v = nxe_json_object_get(root, "n");
    ASSERT(nxe_json_is_null(v));

    /* type mismatch → NGX_ERROR */
    v = nxe_json_object_get(root, "i");
    ASSERT_EQ_INT(nxe_json_real(v, &dv), NGX_ERROR);
    ASSERT_EQ_INT(nxe_json_boolean(v, &bv), NGX_ERROR);

    nxe_json_free(root);
}


TEST(real_rejects_integer){
    /* header: nxe_json_real returns NGX_ERROR for non-real values */
    ngx_str_t input = sz("[1]");
    nxe_json_t *root;
    double dv = 0.0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(nxe_json_real(nxe_json_array_get(root, 0), &dv),
                  NGX_ERROR);
    nxe_json_free(root);
}


TEST(number_rejects_boolean){
    /* nxe_json_number only accepts integer / real */
    ngx_str_t input = sz("[true,false,null]");
    nxe_json_t *root;
    double dv = 0.0;
    size_t i;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    for (i = 0; i < 3; i++) {
        ASSERT_EQ_INT(nxe_json_number(nxe_json_array_get(root, i), &dv),
                      NGX_ERROR);
    }

    nxe_json_free(root);
}


TEST(number_accepts_both){
    ngx_str_t input = sz("[7,2.5,\"x\"]");
    nxe_json_t *root;
    double dv = 0.0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_number(nxe_json_array_get(root, 0), &dv),
                  NGX_OK);
    ASSERT_EQ_DOUBLE(dv, 7.0);

    ASSERT_EQ_INT(nxe_json_number(nxe_json_array_get(root, 1), &dv),
                  NGX_OK);
    ASSERT_EQ_DOUBLE(dv, 2.5);

    ASSERT_EQ_INT(nxe_json_number(nxe_json_array_get(root, 2), &dv),
                  NGX_ERROR);

    nxe_json_free(root);
}


/* ============================================================ */
/* from_string                                                   */
/* ============================================================ */

TEST(from_string_and_free){
    ngx_str_t str = sz("hello");
    nxe_json_t *j = nxe_json_from_string(&str);
    ngx_str_t out;

    ASSERT(j != NULL);
    ASSERT(nxe_json_is_string(j));
    ASSERT_EQ_INT(nxe_json_string(j, &out), NGX_OK);
    ASSERT_STR_EQ(out, "hello");

    nxe_json_free(j);
}


TEST(from_string_binary_safe){
    u_char raw[] = { 'a', 0, 'b', 'c' };
    ngx_str_t str = { 4, raw };
    nxe_json_t *j = nxe_json_from_string(&str);
    ngx_str_t out;

    ASSERT(j != NULL);
    ASSERT_EQ_INT(nxe_json_string(j, &out), NGX_OK);
    ASSERT_EQ_INT(out.len, 4);
    ASSERT(memcmp(out.data, raw, 4) == 0);

    nxe_json_free(j);
}


TEST(from_string_rejects_oversized){
    ngx_str_t str = { NXE_JSON_MAX_SIZE + 1, (u_char *) "x" };

    ASSERT(nxe_json_from_string(&str) == NULL);
    ASSERT(nxe_json_from_string(NULL) == NULL);
}


/* ============================================================ */
/* equal                                                         */
/* ============================================================ */

TEST(equal_basic){
    ngx_str_t a_in = sz("[1,\"x\",{\"k\":null}]");
    ngx_str_t b_in = sz("[1,\"x\",{\"k\":null}]");
    ngx_str_t c_in = sz("[1,\"x\",{\"k\":1}]");
    nxe_json_t *a = nxe_json_parse(&a_in, pool);
    nxe_json_t *b = nxe_json_parse(&b_in, pool);
    nxe_json_t *c = nxe_json_parse(&c_in, pool);

    ASSERT(a && b && c);
    ASSERT_EQ_INT(nxe_json_equal(a, b), 1);
    ASSERT_EQ_INT(nxe_json_equal(a, c), 0);
    ASSERT_EQ_INT(nxe_json_equal(NULL, a), 0);
    ASSERT_EQ_INT(nxe_json_equal(a, NULL), 0);

    nxe_json_free(a);
    nxe_json_free(b);
    nxe_json_free(c);
}


/* ============================================================ */
/* compare                                                       */
/* ============================================================ */

TEST(compare_int_int){
    ngx_str_t input = sz("[3,5,5]");
    nxe_json_t *root;
    double diff = 0.0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root, 0),
                                   nxe_json_array_get(root, 1),
                                   &diff, NULL), NGX_OK);
    ASSERT_EQ_DOUBLE(diff, -1.0);

    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root, 1),
                                   nxe_json_array_get(root, 2),
                                   &diff, NULL), NGX_OK);
    ASSERT_EQ_DOUBLE(diff, 0.0);

    nxe_json_free(root);
}


TEST(compare_int_real_lossless){
    ngx_str_t input = sz("[10,10.0,11.0]");
    nxe_json_t *root;
    double diff = 0.0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root, 0),
                                   nxe_json_array_get(root, 1),
                                   &diff, NULL), NGX_OK);
    ASSERT_EQ_DOUBLE(diff, 0.0);

    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root, 0),
                                   nxe_json_array_get(root, 2),
                                   &diff, NULL), NGX_OK);
    ASSERT_EQ_DOUBLE(diff, -1.0);

    nxe_json_free(root);
}


TEST(compare_real_real){
    ngx_str_t input = sz("[1.5,2.5]");
    nxe_json_t *root;
    double diff = 0.0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root, 0),
                                   nxe_json_array_get(root, 1),
                                   &diff, NULL), NGX_OK);
    ASSERT_EQ_DOUBLE(diff, -1.0);

    nxe_json_free(root);
}


TEST(compare_large_int_exact){
    /* Two integers above 2^53 would collapse onto the same double if
     * compared via double.  The int64 fast path must preserve their
     * ordering; mixed integer/real cases where the fast path does not
     * apply are covered by compare_unsafe_int_vs_real_fails. */
    ngx_str_t input = sz("[9007199254740993,9007199254740992]");
    nxe_json_t *root;
    double diff = 0.0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root, 0),
                                   nxe_json_array_get(root, 1),
                                   &diff, NULL), NGX_OK);
    ASSERT_EQ_DOUBLE(diff, 1.0);

    nxe_json_free(root);
}


TEST(compare_rejects_non_number){
    ngx_str_t input = sz("[\"x\",1]");
    nxe_json_t *root;
    double diff = 0.0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root, 0),
                                   nxe_json_array_get(root, 1),
                                   &diff, NULL), NGX_ERROR);

    nxe_json_free(root);
}


TEST(compare_unsafe_int_vs_real_fails){
    /* An integer with magnitude > 2^53 cannot safely round-trip
     * through double.  When paired with a real that cannot be
     * promoted back to int64 losslessly, the double-fallback path
     * must refuse the comparison rather than collapse the integer
     * onto a neighbouring double. */
    ngx_str_t input = sz("[9007199254740993,1.5]");
    ngx_str_t input_rev = sz("[1.5,-9007199254740993]");
    nxe_json_t *root, *root_rev;
    double diff = 0.0;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root, 0),
                                   nxe_json_array_get(root, 1),
                                   &diff, NULL), NGX_ERROR);
    nxe_json_free(root);

    root_rev = nxe_json_parse(&input_rev, pool);
    ASSERT(root_rev != NULL);
    ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(root_rev, 0),
                                   nxe_json_array_get(root_rev, 1),
                                   &diff, NULL), NGX_ERROR);
    nxe_json_free(root_rev);
}


/* ============================================================ */
/* stringify_compact                                             */
/* ============================================================ */

TEST(stringify_compact_basic){
    ngx_str_t input = sz("{\n  \"a\": 1,\n  \"b\": [2,3]\n}");
    nxe_json_t *root;
    ngx_str_t *out;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    out = nxe_json_stringify_compact(root, pool);
    ASSERT(out != NULL);
    /* no whitespace in compact form; exact member order unspecified */
    {
        size_t i;
        for (i = 0; i < out->len; i++) {
            ASSERT(out->data[i] != ' ');
            ASSERT(out->data[i] != '\n');
            ASSERT(out->data[i] != '\t');
        }
    }

    nxe_json_free(root);
}


TEST(stringify_encodes_scalar_root){
    /* nxe_json_parse accepts scalar roots via JSON_DECODE_ANY;
     * stringify must round-trip them (requires JSON_ENCODE_ANY). */
    ngx_str_t input = sz("42");
    nxe_json_t *root;
    ngx_str_t *out;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    out = nxe_json_stringify_compact(root, pool);
    ASSERT(out != NULL);
    ASSERT_STR_EQ(*out, "42");

    nxe_json_free(root);
}


TEST(stringify_compact_roundtrip){
    ngx_str_t input = sz("[1,2,3]");
    nxe_json_t *root;
    ngx_str_t *out;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    out = nxe_json_stringify_compact(root, pool);
    ASSERT(out != NULL);
    ASSERT_STR_EQ(*out, "[1,2,3]");

    nxe_json_free(root);
}


TEST(stringify_pretty_has_newlines_and_indent){
    ngx_str_t input = sz("{\"a\":1,\"b\":[2,3]}");
    nxe_json_t *root;
    ngx_str_t *out;
    size_t newlines = 0;
    size_t i;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    out = nxe_json_stringify_pretty(root, pool, 2);
    ASSERT(out != NULL);

    for (i = 0; i < out->len; i++) {
        if (out->data[i] == '\n') {
            newlines++;
        }
    }
    ASSERT(newlines >= 2);

    {
        nxe_json_t *reparsed;
        nxe_json_t *b;

        reparsed = nxe_json_parse(out, pool);
        ASSERT(reparsed != NULL);
        ASSERT_EQ_INT(nxe_json_equal(root, reparsed), 1);

        b = nxe_json_object_get(reparsed, "b");
        ASSERT(b != NULL);
        ASSERT_EQ_INT(nxe_json_array_size(b), 2);

        nxe_json_free(reparsed);
    }

    nxe_json_free(root);
}


TEST(stringify_pretty_clamps_indent){
    ngx_str_t input = sz("[1]");
    nxe_json_t *root;
    ngx_str_t *out_zero, *out_huge;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    out_zero = nxe_json_stringify_pretty(root, pool, 0);
    ASSERT(out_zero != NULL);
    ASSERT(out_zero->len > 3);

    out_huge = nxe_json_stringify_pretty(root, pool, 100);
    ASSERT(out_huge != NULL);
    {
        nxe_json_t *reparsed = nxe_json_parse(out_huge, pool);
        ASSERT(reparsed != NULL);
        nxe_json_free(reparsed);
    }

    nxe_json_free(root);
}


TEST(stringify_pretty_null_inputs){
    ngx_str_t *out;
    ngx_str_t input = sz("[1]");
    nxe_json_t *root;

    out = nxe_json_stringify_pretty(NULL, pool, 2);
    ASSERT(out == NULL);

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);
    out = nxe_json_stringify_pretty(root, NULL, 2);
    ASSERT(out == NULL);
    nxe_json_free(root);
}


/* ============================================================ */
/* defensive zero-clear on failure                               */
/* ============================================================ */

/*
 * Verifies that every extractor clears its out-parameter before
 * returning a non-OK result.  Callers that forget to check the return
 * value then read a deterministic zero instead of stack garbage, which
 * closes the silent-downgrade window raised by the security audit for
 * nxe_json_object_get_integer / _boolean (tasks/issues/008.md).
 *
 * Each subcase seeds the out-param with a sentinel ("poison") first and
 * asserts the sentinel is gone after the failing call.
 */
TEST(extractor_zero_clears_on_failure){
    ngx_str_t input =
        sz("{\"s\":\"hi\",\"n\":1,\"r\":1.5,\"b\":true,\"z\":null}");
    nxe_json_t *root, *sv, *nv, *rv, *bv, *zv;
    ngx_str_t sout;
    int64_t iout;
    double dout;
    ngx_flag_t fout;

    root = nxe_json_parse(&input, pool);
    ASSERT(root != NULL);

    sv = nxe_json_object_get(root, "s");
    nv = nxe_json_object_get(root, "n");
    rv = nxe_json_object_get(root, "r");
    bv = nxe_json_object_get(root, "b");
    zv = nxe_json_object_get(root, "z");
    ASSERT(sv != NULL && nv != NULL && rv != NULL
           && bv != NULL && zv != NULL);

    /* nxe_json_string: type mismatch */
    sout.data = (u_char *) "poison";
    sout.len = 6;
    ASSERT_EQ_INT(nxe_json_string(nv, &sout), NGX_ERROR);
    ASSERT(sout.data == NULL);
    ASSERT_EQ_INT(sout.len, 0);

    /* nxe_json_string: NULL handle */
    sout.data = (u_char *) "poison";
    sout.len = 6;
    ASSERT_EQ_INT(nxe_json_string(NULL, &sout), NGX_ERROR);
    ASSERT(sout.data == NULL);
    ASSERT_EQ_INT(sout.len, 0);

    /* nxe_json_integer: type mismatch (string) */
    iout = 0x5a5a5a5a;
    ASSERT_EQ_INT(nxe_json_integer(sv, &iout), NGX_ERROR);
    ASSERT_EQ_INT(iout, 0);

    /* nxe_json_integer: type mismatch (real is not integer) */
    iout = 0x5a5a5a5a;
    ASSERT_EQ_INT(nxe_json_integer(rv, &iout), NGX_ERROR);
    ASSERT_EQ_INT(iout, 0);

    /* nxe_json_integer: NULL handle */
    iout = 0x5a5a5a5a;
    ASSERT_EQ_INT(nxe_json_integer(NULL, &iout), NGX_ERROR);
    ASSERT_EQ_INT(iout, 0);

    /* nxe_json_real: type mismatch (integer is not real) */
    dout = 12345.0;
    ASSERT_EQ_INT(nxe_json_real(nv, &dout), NGX_ERROR);
    ASSERT_EQ_DOUBLE(dout, 0.0);

    /* nxe_json_real: NULL handle */
    dout = 12345.0;
    ASSERT_EQ_INT(nxe_json_real(NULL, &dout), NGX_ERROR);
    ASSERT_EQ_DOUBLE(dout, 0.0);

    /* nxe_json_boolean: type mismatch */
    fout = 0x5a;
    ASSERT_EQ_INT(nxe_json_boolean(nv, &fout), NGX_ERROR);
    ASSERT_EQ_INT(fout, 0);

    /* nxe_json_boolean: NULL handle */
    fout = 0x5a;
    ASSERT_EQ_INT(nxe_json_boolean(NULL, &fout), NGX_ERROR);
    ASSERT_EQ_INT(fout, 0);

    /* nxe_json_number: type mismatch (boolean is not a number) */
    dout = 12345.0;
    ASSERT_EQ_INT(nxe_json_number(bv, &dout), NGX_ERROR);
    ASSERT_EQ_DOUBLE(dout, 0.0);

    /* nxe_json_number: null is not a number */
    dout = 12345.0;
    ASSERT_EQ_INT(nxe_json_number(zv, &dout), NGX_ERROR);
    ASSERT_EQ_DOUBLE(dout, 0.0);

    /* nxe_json_number: NULL handle */
    dout = 12345.0;
    ASSERT_EQ_INT(nxe_json_number(NULL, &dout), NGX_ERROR);
    ASSERT_EQ_DOUBLE(dout, 0.0);

    /* nxe_json_object_get_string: missing key */
    sout.data = (u_char *) "poison";
    sout.len = 6;
    ASSERT_EQ_INT(nxe_json_object_get_string(root, "missing", &sout,
                                             pool),
                  NGX_DECLINED);
    ASSERT(sout.data == NULL);
    ASSERT_EQ_INT(sout.len, 0);

    /* nxe_json_object_get_string: type mismatch */
    sout.data = (u_char *) "poison";
    sout.len = 6;
    ASSERT_EQ_INT(nxe_json_object_get_string(root, "n", &sout, pool),
                  NGX_DECLINED);
    ASSERT(sout.data == NULL);
    ASSERT_EQ_INT(sout.len, 0);

    /* nxe_json_object_get_string: NULL pool (defensive zero-clear
     * still applies once the value pointer itself is non-NULL, to
     * match the other extractors). */
    sout.data = (u_char *) "poison";
    sout.len = 6;
    ASSERT_EQ_INT(nxe_json_object_get_string(root, "s", &sout, NULL),
                  NGX_ERROR);
    ASSERT(sout.data == NULL);
    ASSERT_EQ_INT(sout.len, 0);

    /* nxe_json_object_get_integer: missing key */
    iout = 0x5a5a5a5a;
    ASSERT_EQ_INT(nxe_json_object_get_integer(root, "missing", &iout),
                  NGX_DECLINED);
    ASSERT_EQ_INT(iout, 0);

    /* nxe_json_object_get_integer: type mismatch */
    iout = 0x5a5a5a5a;
    ASSERT_EQ_INT(nxe_json_object_get_integer(root, "s", &iout),
                  NGX_DECLINED);
    ASSERT_EQ_INT(iout, 0);

    /* nxe_json_object_get_boolean: missing key */
    fout = 0x5a;
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "missing", &fout),
                  NGX_DECLINED);
    ASSERT_EQ_INT(fout, 0);

    /* nxe_json_object_get_boolean: type mismatch */
    fout = 0x5a;
    ASSERT_EQ_INT(nxe_json_object_get_boolean(root, "n", &fout),
                  NGX_DECLINED);
    ASSERT_EQ_INT(fout, 0);

    /* nxe_json_compare: non-numeric input clears *diff */
    dout = 12345.0;
    ASSERT_EQ_INT(nxe_json_compare(sv, nv, &dout, NULL), NGX_ERROR);
    ASSERT_EQ_DOUBLE(dout, 0.0);

    /* nxe_json_compare: fail-closed on |int| > 2^53 vs non-integer
     * real must also clear *diff so callers that skip the return
     * check cannot read a stale sentinel. */
    {
        ngx_str_t unsafe = sz("[9007199254740993,1.5]");
        nxe_json_t *uroot = nxe_json_parse(&unsafe, pool);

        ASSERT(uroot != NULL);
        dout = 12345.0;
        ASSERT_EQ_INT(nxe_json_compare(nxe_json_array_get(uroot, 0),
                                       nxe_json_array_get(uroot, 1),
                                       &dout, NULL), NGX_ERROR);
        ASSERT_EQ_DOUBLE(dout, 0.0);
        nxe_json_free(uroot);
    }

    nxe_json_free(root);
}


/* ============================================================ */
/* type dispatch & edge cases                                    */
/* ============================================================ */

TEST(type_on_null){
    ASSERT_EQ_INT(nxe_json_type(NULL), NXE_JSON_INVALID);
}


TEST(free_on_null){
    nxe_json_free(NULL);   /* must not crash */
}


/* ============================================================ */
/* main                                                          */
/* ============================================================ */

int
main(void)
{
    test_stats_t stats;
    ngx_log_t log;

    memset(&stats, 0, sizeof(stats));
    /* Suppress expected error/debug log noise; set to NGX_LOG_DEBUG
     * (or use NXE_JSON_TEST_VERBOSE=1) to see all messages. */
    log.log_level = getenv("NXE_JSON_TEST_VERBOSE") != NULL
                    ? NGX_LOG_DEBUG : NGX_LOG_STDERR;

    /* parse */
    RUN(parse_object);
    RUN(parse_array);
    RUN(parse_scalar_accepted);
    RUN(parse_null_input);
    RUN(parse_empty);
    RUN(parse_size_limit);
    RUN(parse_invalid_json);
    RUN(parse_null_pool);
    RUN(parse_reject_duplicate_keys);

    /* parse_untrusted */
    RUN(parse_untrusted_ok);
    RUN(parse_untrusted_depth_limit);
    RUN(parse_untrusted_array_limit);
    RUN(parse_untrusted_string_limit);
    RUN(parse_untrusted_object_key_length_limit);
    RUN(parse_untrusted_object_keys_limit);

    /* object_get */
    RUN(object_get_basic);
    RUN(object_get_on_non_object);
    RUN(object_get_ns_null_inputs);
    RUN(object_get_ns_basic);
    RUN(object_get_string_convenience);
    RUN(object_get_integer_convenience);
    RUN(object_get_boolean_convenience);

    /* array */
    RUN(array_basic);
    RUN(array_on_non_array);

    RUN(object_size_basic);
    RUN(object_size_on_non_object);
    RUN(object_iter_walks_all_keys);
    RUN(object_iter_preserves_insertion_order);
    RUN(object_iter_empty_object);
    RUN(object_iter_on_non_object);
    RUN(object_iter_null_inputs);
    RUN(object_iter_key_returns_borrowed_view);

    /* scalars */
    RUN(string_extraction);
    RUN(integer_real_boolean);
    RUN(real_rejects_integer);
    RUN(number_accepts_both);
    RUN(number_rejects_boolean);

    /* from_string */
    RUN(from_string_and_free);
    RUN(from_string_binary_safe);
    RUN(from_string_rejects_oversized);

    /* equal */
    RUN(equal_basic);

    /* compare */
    RUN(compare_int_int);
    RUN(compare_int_real_lossless);
    RUN(compare_real_real);
    RUN(compare_large_int_exact);
    RUN(compare_rejects_non_number);
    RUN(compare_unsafe_int_vs_real_fails);

    /* stringify_compact */
    RUN(stringify_compact_basic);
    RUN(stringify_compact_roundtrip);
    RUN(stringify_encodes_scalar_root);
    RUN(stringify_pretty_has_newlines_and_indent);
    RUN(stringify_pretty_clamps_indent);
    RUN(stringify_pretty_null_inputs);

    /* defensive zero-clear */
    RUN(extractor_zero_clears_on_failure);

    /* edge cases */
    RUN(type_on_null);
    RUN(free_on_null);

    printf("\n%d passed, %d failed (out of %d)\n",
           stats.passed, stats.failed, stats.total);

    return stats.failed > 0 ? 1 : 0;
}
