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
#include "range.h"

typedef struct echs_evstrm_s *echs_evstrm_t;
typedef const struct echs_evstrm_s *echs_const_evstrm_t;
typedef const struct echs_evstrm_class_s *echs_evstrm_class_t;

struct echs_evstrm_class_s {
	/** next method */
	echs_event_t(*next)(echs_evstrm_t);
	/** clone method */
	echs_evstrm_t(*clone)(echs_const_evstrm_t);
	/** dtor method */
	void(*free)(echs_evstrm_t);
	/** serialiser method */
	void(*seria)(int whither, echs_const_evstrm_t);
	/** valid time accessor */
	echs_range_t(*set_valid)(echs_evstrm_t, echs_range_t);
	/** valid time accessor */
	echs_range_t(*valid)(echs_const_evstrm_t);
};

struct echs_evstrm_s {
	const echs_evstrm_class_t class;
	char data[];
};


/**
 * Stream ctor.  Probe file FN and return stream or NULL. */
extern echs_evstrm_t make_echs_evstrm_from_file(const char *fn);

/**
 * Muxer, produce an evstrm that iterates over all evstrms given.
 * The streams are repurposed in the mux and should not be used
 * at the same time as the mux stream.
 * For a cloning mux see `echs_evstrm_mux_clon()'. */
extern echs_evstrm_t echs_evstrm_mux(echs_evstrm_t s, ...);

/**
 * Muxer, produce an evstrm that iterates over all evstrms given.
 * Like `echs_evstrm_mux()' but clone the streams before using
 * them in the mux stream.  That way the input streams can be
 * used independently of the mux stream afterwards. */
extern echs_evstrm_t echs_evstrm_mux_clon(echs_evstrm_t s, ...);

/**
 * Muxer, same as `echs_evstrm_mux()' but for an array S of size N.
 * Again, for a cloning version see `echs_evstrm_vmux_clon()'. */
extern echs_evstrm_t echs_evstrm_vmux(const echs_evstrm_t s[], size_t n);

/**
 * Muxer, same as `echs_evstrm_mux_clon()' but for an array S of size N.
 * Streams in S are cloned before putting them into the mux stream.
 * That way the streams in S can be used independently of the resulting
 * mux stream afterwards. */
extern echs_evstrm_t echs_evstrm_vmux_clon(const echs_evstrm_t s[], size_t n);

/**
 * Demuxer, put at most NTGT streams of S into TGT and return
 * the number of streams actually put.
 * Use OFFSET to skip the first OFFSET streams.
 * This will not clone the streams in quesion. */
extern size_t
echs_evstrm_demux(echs_evstrm_t *restrict tgt, size_t tsz,
		  const struct echs_evstrm_s *s, size_t offset);


static inline echs_event_t
echs_evstrm_next(echs_evstrm_t s)
{
	return s->class->next(s);
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

static inline void
echs_evstrm_seria(int whither, echs_evstrm_t s)
{
	if (s->class->seria != NULL) {
		return s->class->seria(whither, s);
	}
	return;
}

static inline echs_range_t
echs_evstrm_set_valid(echs_evstrm_t s, echs_range_t v)
{
	if (s->class->set_valid != NULL) {
		return s->class->set_valid(s, v);
	}
	return echs_nul_range();
}

static inline echs_range_t
echs_evstrm_valid(echs_evstrm_t s)
{
	if (s->class->valid != NULL) {
		return s->class->valid(s);
	}
	return echs_nul_range();
}

#endif	/* INCLUDED_evstrm_h_ */
