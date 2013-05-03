/*** echse.h -- testing echse concept
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
#if !defined INCLUDED_echse_h_
#define INCLUDED_echse_h_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define DEFSTATE(x)	static const char state__ ## x [] = "~" # x
#define DEFISTATE(x)	static const char istate__ ## x [] = "!" # x
#define ON(x)		(state__ ## x + 1U)
#define OFF(x)		(state__ ## x + 0U)
#define ITS(x)		(istate__ ## x + 0U)


typedef union echs_instant_u echs_instant_t;
typedef const char *echs_state_t;
typedef struct echs_event_s echs_event_t;
typedef struct echs_stream_s echs_stream_t;
typedef struct echs_filter_s echs_filter_t;
typedef const struct echs_prop_s *echs_prop_t;

union echs_instant_u {
	struct {
		uint32_t y:16;
		uint32_t m:8;
		uint32_t d:8;
		uint32_t H:8;
		uint32_t M:8;
		uint32_t S:6;
		uint32_t ms:10;
	};
	uint64_t u;
} __attribute__((transparent_union));

struct echs_event_s {
	echs_instant_t when;
	echs_state_t what;
};

typedef enum echs_strctl_e echs_strctl_t;

struct echs_stream_s {
	echs_event_t(*f)(void*);
	void *clo;
	/* for strctl */
	void *(*ctl)(echs_strctl_t, void*, ...);
};

struct echs_filter_s {
	echs_event_t(*f)(echs_event_t, void*);
	void *clo;
	/* for fltctl */
	void(*ctl)(echs_strctl_t, void*, ...);
};

struct echs_pset_s {
	enum {
		ECHS_PSET_UNK,
		ECHS_PSET_PTR,
		ECHS_PSET_STR,
		ECHS_PSET_MEM,
		ECHS_PSET_INT,
		ECHS_PSET_DBL,
	} typ;
	union {
		/* values with z == 0 */
		const void *ptr;
		intmax_t ival;
		double dval;

		/* values for which z should be >0 */
		const void *mem;
		const char *str;
	};
	size_t z;
};

struct echs_prop_s {
	const char *key;
	struct echs_pset_s pset;
};


/**
 * Stream ctor. */
extern echs_stream_t make_echs_stream(echs_instant_t, ...);

/**
 * Stream dtor. */
extern void free_echs_stream(echs_stream_t);

/**
 * Stream clone-tor. */
extern echs_stream_t clone_echs_stream(echs_stream_t);

/**
 * Stream clone-dtor. */
extern void unclone_echs_stream(echs_stream_t);

/**
 * Set property of S with key K to V. */
extern void
echs_stream_pset(echs_stream_t s, const char *k, struct echs_pset_s v);

/**
 * Filter ctor. */
extern echs_filter_t make_echs_filter(echs_instant_t, ...);

/**
 * Filter dtor. */
extern void free_echs_filter(echs_filter_t);

/**
 * Set property of F with key K to V. */
extern void
echs_filter_pset(echs_filter_t f, const char *k, struct echs_pset_s v);


static inline echs_event_t
echs_stream_next(echs_stream_t s)
{
	return s.f(s.clo);
}

static inline echs_event_t
echs_filter_next(echs_filter_t f, echs_event_t e)
{
	return f.f(e, f.clo);
}

static inline echs_event_t
echs_filter_drain(echs_filter_t f)
{
	return f.f((echs_event_t){0}, f.clo);
}


#define ECHS_ALL_DAY	(0xffU)
#define ECHS_ALL_SEC	(0x3ffU)

static inline bool
echs_instant_all_day_p(echs_instant_t i)
{
	return i.H == ECHS_ALL_DAY;
}

static inline bool
echs_instant_all_sec_p(echs_instant_t i)
{
	return i.ms == ECHS_ALL_SEC;
}

#endif	/* INCLUDED_echse_h_ */
