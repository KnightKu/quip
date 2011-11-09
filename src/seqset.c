#include "seqset.h"
#include "misc.h"
#include <assert.h>
#include <string.h>



struct seqset_t_
{
    seqset_value_t* xs; /* table proper */
    size_t n;           /* table size */
    size_t pn;          /* n = primes[pn] */
    size_t m;           /* occupied positions (not including deleted) */
    size_t d;           /* number of deleted positions */
    size_t max_m;       /* max hashed items before size is doubled */
    size_t min_m;       /* min hashed items before size is halved */
};


/* Prime numbers that a near powers of two, suitable for hash table sizes, when
 * using quadratic probing. */
#define NUM_PRIMES 28
static const uint32_t primes[NUM_PRIMES] = {
           53U,         97U,        193U,        389U,    
          769U,       1543U,       3079U,       6151U,  
        12289U,      24593U,      49157U,      98317U,  
       196613U,     393241U,     786433U,    1572869U, 
      3145739U,    6291469U,   12582917U,   25165843U,
     50331653U,  100663319U,  201326611U,  402653189U,
    805306457U, 1610612741U, 3221225473U, 4294967291U };


static const double MAX_LOAD  = 0.5;
static const double MIN_LOAD  = 0.1;


/* simple quadratic probing */
static uint32_t probe(uint32_t h, uint32_t i)
{
    static const uint32_t c1 = 2;
    static const uint32_t c2 = 2;

    return h + i/c1 + (i*i)/c2;
}



seqset_t* seqset_alloc()
{
    seqset_t* S = malloc_or_die(sizeof(seqset_t));
    S->pn = 0;
    S->n  = primes[S->pn];
    S->xs = malloc_or_die(S->n * sizeof(seqset_value_t));
    memset(S->xs, 0, S->n * sizeof(seqset_value_t));
    S->m = 0;
    S->d = 0;
    S->max_m = (size_t) ((double) S->n * MAX_LOAD);
    S->min_m = (size_t) ((double) S->n * MIN_LOAD);

    return S;
}


void seqset_free(seqset_t* S)
{
    size_t i;
    for (i = 0; i < S->n; ++i) {
        twobit_free(S->xs[i].seq);
    }
    free(S->xs);
    free(S);
}


#if 0
static void set_nil_key(seqset_value_t* x)
{
    x->seq = NULL; x->cnt = 0;
}


static void set_del_key(seqset_value_t* x)
{
    x->seq = NULL; x->cnt = ~0U;
}
#endif


static bool is_nil_key(const seqset_value_t* x)
{
    return x->seq == NULL && x->cnt == 0;
}



static bool is_del_key(const seqset_value_t* x)
{
    return x->seq == NULL && x->cnt == ~0U;
}



static bool is_nil_or_del_key(const seqset_value_t* x)
{
    return x->seq == NULL;
}


static void seqset_rehash(seqset_t* dest, seqset_t* src)
{
    assert(dest->m == 0);

    uint32_t h, k, probe_num;

    size_t i;
    for (i = 0; i < src->n; ++i) {
        if (is_nil_or_del_key(src->xs + i)) continue;

        h = twobit_hash(src->xs[i].seq);
        k = h % dest->n;
        probe_num = 1;

        while (true) {
            if (is_nil_key(dest->xs + k)) {
                dest->xs[k].seq = src->xs[i].seq;
                dest->xs[k].cnt = src->xs[i].cnt;
                break;
            }

            k = probe(h, ++probe_num) % dest->n;
            assert(probe_num < dest->n);
        }
    }
}



static void seqset_resize(seqset_t* S, size_t new_pn)
{
    seqset_t T;
    T.pn = new_pn;
    T.n = primes[new_pn];
    T.m = 0;
    T.d = 0;
    T.xs = malloc_or_die(T.n * sizeof(seqset_value_t));
    memset(T.xs, 0, T.n * sizeof(seqset_value_t));

    /* not used: */
    T.max_m = 0;
    T.min_m = 0;

    seqset_rehash(&T, S);

    free(S->xs);
    S->xs  = T.xs;
    S->pn = T.pn;
    S->n  = T.n;
    S->d  = 0;
    S->min_m = (size_t) ((double) S->n * MIN_LOAD);
    S->max_m = (size_t) ((double) S->n * MAX_LOAD);
}


static void seqset_shrink_as_needed(seqset_t* S)
{
    size_t new_pn = S->pn;
    while (new_pn > 0 && S->m < (size_t) (MIN_LOAD * (double) primes[new_pn])) {
        --new_pn;
    }

    if (new_pn != S->pn) seqset_resize(S, new_pn);
}


static void seqset_resize_delta(seqset_t* T, size_t delta)
{
    seqset_shrink_as_needed(T);

    size_t new_pn = T->pn;
    while (T->m + delta > (size_t) (MAX_LOAD * (double) primes[new_pn])) ++new_pn;
    if (new_pn != T->pn) seqset_resize(T, new_pn);
}



uint32_t seqset_inc(seqset_t* S, const twobit_t* seq)
{
    seqset_resize_delta(S, 1);

    uint32_t h = twobit_hash(seq);
    uint32_t probe_num = 1;
    uint32_t k = h % S->n;

    int ins_pos = -1;

    while (true) {
        if (is_del_key(S->xs + k)) {
            if (ins_pos == -1) ins_pos = k;
        }
        else if (is_nil_key(S->xs + k)) {
            if (ins_pos == -1) ins_pos = k;
            break;
        }
        else if (twobit_cmp(S->xs[k].seq, seq) == 0) {
            if (S->xs[k].cnt < ~0U) ++S->xs[k].cnt;
            return S->xs[k].cnt;
        }

        k = probe(h, ++probe_num) % S->n;
        assert(probe_num < S->n);
    }

    if (is_del_key(S->xs + ins_pos)) --S->d;
    else                             ++S->m;

    S->xs[ins_pos].cnt = 1;
    S->xs[ins_pos].seq = twobit_dup(seq);

    return 1;
}

size_t seqset_size(const seqset_t* S)
{
    return S->m;
}


seqset_iter_t* seqset_iter_alloc()
{
    seqset_iter_t* i = malloc_or_die(sizeof(seqset_iter_t));
    memset(i, 0, sizeof(seqset_iter_t));
    return i;
}


void seqset_iter_free(seqset_iter_t* i)
{
    free(i);
}


void seqset_iter_start(seqset_iter_t* i, const seqset_t* S)
{
    i->val = NULL;
    i->S   = S;
    for (i->i = 0; i->i < S->n; ++i->i) {
        if (!is_nil_or_del_key(S->xs + i->i)) break;
    }

    if (i->i < S->n) i->val = S->xs + i->i;
}

void seqset_iter_next(seqset_iter_t* i)
{
    if (i->i < i->S->m) {
        ++i->i;
        for (; i->i < i->S->n; ++i->i) {
            if (!is_nil_or_del_key(i->S->xs + i->i)) break;
        }

        if (i->i < i->S->n) i->val = i->S->xs + i->i;
    }
}


bool seqset_iter_finished(const seqset_iter_t* i)
{
    return i->val == NULL || i->i >= i->S->m;
}



seqset_value_t* seqset_dump(const seqset_t* S)
{
    seqset_value_t* xs = malloc_or_die(S->m * sizeof(seqset_value_t));
    size_t i, j;
    for (i = 0, j = 0; i < S->n; ++i) {
        if (!is_nil_or_del_key(S->xs + i)) {
            xs[j].seq = S->xs[i].seq;
            xs[j].cnt = S->xs[i].cnt;
            ++j;
        }
    }

    return xs;
}


