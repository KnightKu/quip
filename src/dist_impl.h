/*
 * This file is part of quip.
 *
 * Copyright (c) 2012 by Daniel C. Jones <dcjones@cs.washington.edu>
 *
 */

/* WARNING: DO NOT INCLUDE THIS FILE DIRECTLY. You should include "dist.h".
 */


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "ac.h"

typedef struct dfun(t_)
{
    struct {
        uint16_t count;
        uint16_t freq;
    } xs[DISTSIZE];

    /* number of new observations until the distribution is updated */
    uint32_t update_delay;

    /* total number of observations */
    uint32_t use_count;

    /* decoder table */
    uint16_t* dec;
} dist_t;


/* Allocate a distribution over the alphabet [0, n - 1].
 * The structure is either allocated with the specify intent of either decoding
 * or encoding to save memory.
 * */
void dfun(init) (dist_t*, bool decode);
void dfun(free) (dist_t*);

/* update distribution to reflect calls new observations */
void dfun(update)(dist_t* D);

/* encode a symbol given the distribution and arithmetic coder */
void   dfun(encode)(ac_t*, dist_t*, symb_t);
symb_t dfun(decode)(ac_t*, dist_t*);


/* cache-conscious conditional distributions:
 * Once conditional distributions get very large, cache-misses become a huge
 * bottleneck. This implementation periodically rearranges to array so that
 * frequently used portions of the probability space are grouped together.
 */

typedef struct cdfun(t_)
{
    /* an array of distributions */
    dist_t* xss;

    /* Use xss[index[i]] to access element i. */
    uint32_t* index;

    /* The reverse of index. An array such that xss[i] corresponds to item
     * ord[i] */
    uint32_t* ord;

    /* alphabet over which the distribution is conditioned */
    uint32_t n;
} cond_dist_t;


void cdfun(init) (cond_dist_t*, size_t n, bool decode);
void cdfun(free) (cond_dist_t*);
void cdfun(reorder) (cond_dist_t*);

/* encode a symbol given the distribution and arithmetic coder */
void   cdfun(encode)(ac_t*, cond_dist_t*, uint32_t y, symb_t x);
symb_t cdfun(decode)(ac_t*, cond_dist_t*, uint32_t y);

