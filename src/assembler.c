
#include "assembler.h"
#include "bloom.h"
#include "kmer.h"
#include "kmerhash.h"
#include "misc.h"
#include "seqset.h"
#include "sw.h"
#include "twobit.h"
#include <assert.h>
#include <math.h>

struct assembler_t_
{
    /* kmer bit-mask used in assembly */
    kmer_t assemble_kmer_mask;

    /* kmer bit-mask used in alignment */
    kmer_t align_kmer_mask;

    /* read set */
    seqset_t* S;

    /* current read */
    twobit_t* x;

    /* k-mer size used for assembly */
    size_t assemble_k;

    /* k-mer size used for seeds of alignment */
    size_t align_k;

    /* k-mer table used for assembly */
    bloom_t* B;

    /* k-mer table used for alignment */
    kmerhash_t* H;

    /* count required to be nominated as a seed candidate */
    unsigned int count_cutoff;
};


assembler_t* assembler_alloc(size_t assemble_k, size_t align_k)
{
    assembler_t* A = malloc_or_die(sizeof(assembler_t));
    assert(A != NULL);

    A->assemble_k = assemble_k;
    A->align_k    = align_k;

    A->assemble_kmer_mask = 0;
    size_t i;
    for (i = 0; i < A->assemble_k; ++i) {
#ifdef WORDS_BIGENDIAN
        A->assemble_kmer_mask = (A->assemble_kmer_mask >> 2) | 0x3;
#else
        A->assemble_kmer_mask = (A->assemble_kmer_mask << 2) | 0x3;
#endif
    }

    A->align_kmer_mask = 0;
    for (i = 0; i < A->align_k; ++i) {
#ifdef WORDS_BIGENDIAN
        A->align_kmer_mask = (A->align_kmer_mask >> 2) | 0x3;
#else
        A->align_kmer_mask = (A->align_kmer_mask << 2) | 0x3;
#endif
    }


    A->S = seqset_alloc();


    // TODO: set n and m in some principled way
    A->B = bloom_alloc(8388608, 8);

    A->x = twobit_alloc();


    A->H = kmerhash_alloc();

    A->count_cutoff = 2;

    return A;
}


void assembler_free(assembler_t* A)
{
    seqset_free(A->S);
    bloom_free(A->B);
    kmerhash_free(A->H);
    twobit_free(A->x);
}



void assembler_add_seq(assembler_t* A, const char* seq, size_t seqlen)
{
    /* does the read contain non-nucleotide characters ? */
    size_t i;
    for (i = 0; i < seqlen; ++i) {
        /* XXX: for now, we just throw out any reads with non-nucleotide characters
         * */
        if (chartokmer(seq[i]) > 3) return;
    }

    // TODO: handle the case in which seqlen < k

    twobit_copy_n(A->x, seq, seqlen);
    seqset_inc(A->S, A->x);
}


typedef struct seed_t_
{
    kmer_t x;
    unsigned int count;
} seed_t;



static void assembler_make_contig(assembler_t* A, twobit_t* seed, twobit_t* contig)
{
    twobit_clear(contig);


    /* delete all kmers in the seed */
    kmer_t x = twobit_get_kmer(seed, 0, A->assemble_k);
    size_t i;
    for (i = A->assemble_k; i < twobit_len(seed); ++i) {
        bloom_del(A->B, kmer_canonical((x << 2) | twobit_get(seed, i), A->assemble_k));
    }


    /* expand the contig as far left as possible */
    unsigned int cnt, cnt_best = 0;

    kmer_t nt, nt_best = 0, xc, y;


    x = twobit_get_kmer(seed, 0, A->assemble_k);
    while (true) {
        bloom_del(A->B, kmer_canonical(x, A->assemble_k));

#if WORDS_BIGENDIAN
        x = (x << 2) & A->assemble_kmer_mask;
#else 
        x = (x >> 2) & A->assemble_kmer_mask;
#endif
        cnt_best = 0;
        for (nt = 0; nt < 4; ++nt) {
#if WORDS_BIGENDIAN
            y = nt >> (2 * (A->assemble_k - 1));
#else
            y = nt << (2 * (A->assemble_k - 1));
#endif
            xc = kmer_canonical(x | y, A->assemble_k);
            cnt = bloom_get(A->B, xc);

            if (cnt > cnt_best) {
                cnt_best = cnt;
                nt_best  = nt;
            }
        }

        if (cnt_best > 0) {
#if WORDS_BIGENDIAN
            y = nt_best >> (2 * (A->assemble_k - 1));
#else
            y = nt_best << (2 * (A->assemble_k - 1));
#endif
            x = x | y;
            twobit_append_kmer(contig, nt_best, 1);
        }
        else break;
    }

    twobit_reverse(contig);
    twobit_append_twobit(contig, seed);

    x = twobit_get_kmer(seed, twobit_len(seed) - A->assemble_k, A->assemble_k);
    while (true) {
        bloom_del(A->B, kmer_canonical(x, A->assemble_k));

#if WORDS_BIGENDIAN
        x = (x >> 2) & A->assemble_kmer_mask;
#else 
        x = (x << 2) & A->assemble_kmer_mask;
#endif
        cnt_best = 0;
        for (nt = 0; nt < 4; ++nt) {
            xc = kmer_canonical(x | nt, A->assemble_k);
            cnt = bloom_get(A->B, xc);

            if (cnt > cnt_best) {
                cnt_best = cnt;
                nt_best  = nt;
            }
        }

        if (cnt_best > 0) {
            x = x | nt_best;
            twobit_append_kmer(contig, nt_best, 1);
        }
        else break;
    }
}


static int seqset_value_cmp(const void* a_, const void* b_)
{
    seqset_value_t* a = (seqset_value_t*) a_;
    seqset_value_t* b = (seqset_value_t*) b_;

    if      (a->cnt < b->cnt) return 1;
    else if (a->cnt > b->cnt) return -1;
    else                      return 0;
}


static void index_contigs(assembler_t* A, twobit_t** contigs, size_t n)
{
    fprintf(stderr, "indexing contigs ... ");
    size_t i;
    size_t len;
    size_t pos;
    kmer_t x, y;
    twobit_t* contig;
    for (i = 0; i < n; ++i) {
        contig = contigs[i];
        len = twobit_len(contig);
        x = 0;
        for (pos = 0; pos < len; ++pos) {

#ifdef WORDS_BIGENDIAN
            x = ((x >> 2) | twobit_get(contig, pos)) & A->align_kmer_mask;
#else
            x = ((x << 2) | twobit_get(contig, pos)) & A->align_kmer_mask;
#endif

            if (pos + 1 >= A->align_k) {
                y = kmer_canonical(x, A->align_k);
                if (x == y) kmerhash_put(A->H, y, i, pos + 1 - A->align_k);
                else        kmerhash_put(A->H, y, i, - (int32_t) (pos + 2 - A->align_k));
            }
        }
    }

    fprintf(stderr, "done.\n");
}



static void align_to_contigs(assembler_t* A,
                             twobit_t** contigs, size_t contigs_len,
                             seqset_value_t* xs, size_t xs_len)
{
    size_t i, j, seqlen;
    kmer_t x, y;

    kmer_pos_t* pos;
    size_t poslen;

    /* create an alignment structure for each contig */
    sw_t** sws = malloc_or_die(contigs_len * sizeof(sw_t*));
    for (i = 0; i < contigs_len; ++i) {
        sws[i] = sw_alloc(contigs[i]);
    }

    /* create an alignment structure for the reverse complement of each contig */
    sw_t** sws_rc = malloc_or_die(contigs_len * sizeof(sw_t*));
    for (i = 0; i < contigs_len; ++i) {

        /* TODO: reverse complement */

        sws_rc[i] = sw_alloc(contigs[i]);
    }



    /* align every read! */
    for (i = 0; i < xs_len; ++i) {
        seqlen = twobit_len(xs[i].seq);
        x = 0;
        for (j = 0; j < seqlen; ++j) {
#ifdef WORDS_BIGENDIAN
            x = ((x >> 2) | twobit_get(xs[i].seq, j)) & A->align_kmer_mask;
#else
            x = ((x << 2) | twobit_get(xs[i].seq, j)) & A->align_kmer_mask;
#endif

            if (j + 1 >= A->align_k) {
                y = kmer_canonical(x, A->align_k);

                poslen = kmerhash_get(A->H, y, &pos);
                while (poslen--) {

                    if (pos->contig_pos < 0 && x != y) {
                        sw_seeded_align(sws[pos->contig_idx],
                                        xs[i].seq,
                                        -pos->contig_pos - 1,
                                        j + 1 - A->align_k,
                                        A->align_k);
                    }
                    /*else if (pos->contig_pos >= 0 && x == y) {*/
                        /*sw_seeded_align(sws[pos->contig_idx],*/
                                        /*xs[i].seq,*/
                                        /*pos->contig_pos,*/
                                        /*j + 1 - A->align_k,*/
                                        /*A->align_k);*/
                    /*}*/
                    /* TODO: align to reverse complement */

                    pos++;
                }
            }
        }
    }


    for (i = 0; i < contigs_len; ++i) {
        sw_free(sws[i]);
        sw_free(sws_rc[i]);
    }

    free(sws);
}



static void assembler_assemble(assembler_t* A)
{
    /* dump reads and sort by abundance */
    seqset_value_t* xs = seqset_dump(A->S);
    qsort(xs, seqset_size(A->S), sizeof(seqset_value_t), seqset_value_cmp);

    /* count kmers */
    size_t i, j, n, seqlen;
    kmer_t x, y;

    n = seqset_size(A->S);
    for (i = 0; i < n; ++i) {
        seqlen = twobit_len(xs[i].seq);
        x = 0;
        for (j = 0; j < seqlen; ++j) {
#ifdef WORDS_BIGENDIAN
            x = ((x >> 2) | twobit_get(xs[i].seq, j)) & A->assemble_kmer_mask;
#else
            x = ((x << 2) | twobit_get(xs[i].seq, j)) & A->assemble_kmer_mask;
#endif

            if (j + 1 >= A->assemble_k) {
                y = kmer_canonical(x, A->assemble_k);
                bloom_add(A->B, y, xs[i].cnt);
            }
        }
    }


    fprintf(stdout, "%zu unique reads\n", seqset_size(A->S));

    size_t contigs_size = 512;
    size_t contigs_len  = 0;
    twobit_t** contigs = malloc_or_die(contigs_size * sizeof(twobit_t*));

    FILE* f = fopen("contig.fa", "w");
    twobit_t* contig = twobit_alloc();

    for (i = 0; i < n && xs[i].cnt >= A->count_cutoff; ++i) {
        assembler_make_contig(A, xs[i].seq, contig);
        if (twobit_len(contig) < 3 * A->assemble_k) continue;

        /* TODO: when we discard a contig, it would be nice if we could return
         * its k-mers to the bloom filter */

        if (contigs_len == contigs_size) {
            contigs_size *= 2;
            contigs = realloc_or_die(contigs, contigs_size * sizeof(twobit_t*));
        }

        contigs[contigs_len++] = twobit_dup(contig);

        fprintf(f, ">contig_%05zu\n", i);
        twobit_print(contig, f);
        fprintf(f, "\n\n");
    }


    twobit_free(contig);
    fclose(f);

    index_contigs(A, contigs, contigs_len);
    align_to_contigs(A, contigs, contigs_len, xs, n);

    /* TODO: free xs ? */


    // TODO: align and compress, etc

    // What's next??
    // 1. for every read in the sequence set, look for a seed and try to find an
    //    alignment. This means we need a fast sw implementation. I very much
    //    don't want to roll my own, but that might be the only viable option.
    //    Maybe I can expand on the version I wrote for fastq-tools.

    for (i = 0; i < contigs_len; ++i) twobit_free(contigs[i]);
    free(contigs);
}


void assembler_write(assembler_t* A, FILE* fout)
{
    assembler_assemble(A);

    fprintf(fout, "IOU: compressed data\n");
}



