/*
 * This file is part of quip.
 *
 * Copyright (c) 2012 by Daniel C. Jones <dcjones@cs.washington.edu>
 *
 */

#ifndef QUIP_QUIP
#define QUIP_QUIP

#include "parse.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef void   (*quip_writer_t) (void*, const uint8_t*, size_t);
typedef size_t (*quip_reader_t) (void*, uint8_t*, size_t);

typedef struct quip_compressor_t_ quip_compressor_t;
quip_compressor_t* quip_comp_alloc(quip_writer_t, void* writer_data, bool quick);
void               quip_comp_free(quip_compressor_t*);
void               quip_comp_addseq(quip_compressor_t*, seq_t*);
void               quip_comp_flush(quip_compressor_t*);

typedef struct quip_decompressor_t_ quip_decompressor_t;
quip_decompressor_t* quip_decomp_alloc(quip_reader_t, void* reader_data);
void                 quip_decomp_free(quip_decompressor_t*);
bool                 quip_decomp_read(quip_decompressor_t*, seq_t*);

extern bool quip_verbose;

#endif


