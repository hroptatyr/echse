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


extern echs_fltdef_t echs_open_fltdef(echs_instant_t i, const char *fltdef);
extern void echs_close_fltdef(echs_fltdef_t);

/**
 * Plug a stream S into a filter F and return the result stream. */
extern echs_stream_t
make_echs_filtstrm(echs_filter_t f, echs_stream_t s);

/**
 * Rid a filter-stream, as in separate its input stream from the filter.
 * This will not free the input stream nor the filter. */
extern void free_echs_filtstrm(echs_stream_t);

struct fltdef_pset_s {
	enum {
		PSET_TYP_UNK,
		PSET_TYP_PTR,
		PSET_TYP_STR,
		PSET_TYP_INT,
		PSET_TYP_DBL,
	} typ;
	union {
		void *ptr;
		const void *cptr;
		char *str;
		const char *cstr;
		intmax_t ival;
		double dval;
	};
};

/**
 * Set property of F with key K to V. */
extern void
echs_fltdef_pset(echs_fltdef_t, const char *k, struct fltdef_pset_s v);

#endif	/* INCLUDED_fltdef_h_ */
