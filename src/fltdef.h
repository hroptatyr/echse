/*** fltdef.h -- filter modules, files, sockets, etc.
 *
 * Copyright (C) 2013 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
 *
 * This file is part of echse.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/
#if !defined INCLUDED_fltdef_h_
#define INCLUDED_fltdef_h_

#include <stdint.h>
#include "echse.h"

typedef struct echs_fltdef_s echs_fltdef_t;
typedef void *echs_flthdl_t;

struct echs_fltdef_s {
	echs_filter_t f;
	echs_flthdl_t m;
};


/**
 * Open a filter according to FLTDEF and fast forward to instant I.
 * If FLTDEF points to a DSO, load and initialise it. */
extern echs_fltdef_t echs_open_filter(echs_instant_t i, const char *fltdef);

/**
 * Close a filter and free associated resources. */
extern void echs_close_filter(echs_fltdef_t);

/**
 * Pass property (k, v) to filter definition F. */
extern void
echs_pset_filter(echs_fltdef_t f, const char *k, struct echs_pset_s v);

/**
 * Return the psetter of filter definition F. */
extern void(*
	    echs_fltdef_psetter(echs_fltdef_t)
	)(echs_filter_t, const char*, struct echs_pset_s);

/**
 * Plug a stream S into a filter F and return the result stream. */
extern echs_stream_t
make_echs_filtstrm(echs_filter_t f, echs_stream_t s);

/**
 * Rid a filter-stream, as in separate its input stream from the filter.
 * This will not free the input stream nor the filter. */
extern void free_echs_filtstrm(echs_stream_t);

#endif	/* INCLUDED_fltdef_h_ */
