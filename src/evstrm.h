/*** evstrm.h -- streams of events
 *
 * Copyright (C) 2013-2014 Sebastian Freundt
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
#if !defined INCLUDED_evstrm_h_
#define INCLUDED_evstrm_h_

#include <stddef.h>
#include "event.h"

typedef struct echs_evstrm_s *echs_evstrm_t;
typedef const struct echs_evstrm_class_s *echs_evstrm_class_t;

struct echs_evstrm_class_s {
	/** next method */
	echs_event_t(*next)(echs_evstrm_t);
	/** clone method */
	echs_evstrm_t(*clone)(echs_evstrm_t);
	/** dtor method */
	void(*free)(echs_evstrm_t);
	/** printer methods */
	void(*prnt1)(echs_evstrm_t);
	void(*prntm)(const echs_evstrm_t s[], size_t n);
};

struct echs_evstrm_s {
	const echs_evstrm_class_t class;
	char data[];
};


/**
 * Stream ctor.  Probe file FN and return stream or NULL. */
extern echs_evstrm_t make_echs_evstrm_from_file(const char *fn);

/**
 * Muxer, produce an evstrm that iterates over all evstrms given. */
extern echs_evstrm_t echs_evstrm_mux(echs_evstrm_t s, ...);

/**
 * Muxer, same as `echs_evstrm_mux()' but for an array S of size N. */
extern echs_evstrm_t echs_evstrm_vmux(const echs_evstrm_t s[], size_t n);

/**
 * Muxer, same as `echs_evstrm_vmux()' but don't clone the event streams. */
extern echs_evstrm_t make_echs_evmux(echs_evstrm_t s[], size_t n);


static inline echs_event_t
echs_evstrm_next(echs_evstrm_t s)
{
	return s->class->next(s);
}

static inline void
echs_evstrm_prnt(echs_evstrm_t s)
{
	if (s->class->prnt1 != NULL) {
		s->class->prnt1(s);
	} else if (s->class->prntm != NULL) {
		s->class->prntm(&s, 1UL);
	}
	return;
}

static inline void
free_echs_evstrm(echs_evstrm_t s)
{
	s->class->free(s);
	return;
}

static inline echs_evstrm_t
clone_echs_evstrm(echs_evstrm_t s)
{
	return s->class->clone(s);
}

#endif	/* INCLUDED_evstrm_h_ */
