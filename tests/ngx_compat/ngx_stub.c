/*
 * Copyright (c) Tatsuya Kamijo
 * Copyright (c) Bengo4.com, Inc.
 *
 * ngx_stub.c - implementation of the minimal nginx stub used by
 * nxe-json unit tests.  All allocations are tracked so ngx_destroy_pool
 * can release them at teardown.
 */

#include "ngx_stub.h"

#include <stdlib.h>


ngx_pool_t *
ngx_create_pool(size_t size, ngx_log_t *log)
{
    ngx_pool_t *pool;

    (void) size;

    pool = malloc(sizeof(ngx_pool_t));
    if (pool == NULL) {
        return NULL;
    }

    pool->large = NULL;
    pool->log = log;

    return pool;
}


void
ngx_destroy_pool(ngx_pool_t *pool)
{
    ngx_pool_large_t *l, *next;

    if (pool == NULL) {
        return;
    }

    for (l = pool->large; l != NULL; l = next) {
        next = l->next;
        free(l->alloc);
        free(l);
    }

    free(pool);
}


void *
ngx_palloc(ngx_pool_t *pool, size_t size)
{
    void *p;
    ngx_pool_large_t *large;

    p = malloc(size);
    if (p == NULL) {
        return NULL;
    }

    large = malloc(sizeof(ngx_pool_large_t));
    if (large == NULL) {
        free(p);
        return NULL;
    }

    large->alloc = p;
    large->next = pool->large;
    pool->large = large;

    return p;
}


void *
ngx_pnalloc(ngx_pool_t *pool, size_t size)
{
    return ngx_palloc(pool, size);
}


void *
ngx_pcalloc(ngx_pool_t *pool, size_t size)
{
    void *p;

    p = ngx_palloc(pool, size);
    if (p != NULL) {
        ngx_memzero(p, size);
    }

    return p;
}


ngx_int_t
ngx_pfree(ngx_pool_t *pool, void *p)
{
    ngx_pool_large_t *l, **prev;

    if (pool == NULL) {
        return NGX_ERROR;
    }

    prev = &pool->large;
    for (l = pool->large; l != NULL; l = l->next) {
        if (l->alloc == p) {
            *prev = l->next;
            free(l->alloc);
            free(l);
            return NGX_OK;
        }
        prev = &l->next;
    }

    return NGX_ERROR;
}
