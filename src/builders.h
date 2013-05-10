/*** builders.h -- stream building blocks
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
#if !defined INCLUDED_builders_h_
#define INCLUDED_builders_h_

#if !defined DECLF
# define DECLF		extern
# define DEFUN
#endif	/* !DECLF */

#if !defined countof
# define countof(x)		(sizeof(x) / sizeof(*x))
#endif	/* !countof */

typedef enum {
	MON = (1U),
	TUE = (2U),
	WED = (3U),
	THU = (4U),
	FRI = (5U),
	SAT = (6U),
	SUN = (7U),
} echs_wday_t;

typedef enum {
	JAN = (1U),
	FEB = (2U),
	MAR = (3U),
	APR = (4U),
	MAY = (5U),
	JUN = (6U),
	JUL = (7U),
	AUG = (8U),
	SEP = (9U),
	OCT = (10U),
	NOV = (11U),
	DEC = (12U),
} echs_mon_t;

#define MON_AFTER(x)		echs_wday_after(x, MON)
#define TUE_AFTER(x)		echs_wday_after(x, TUE)
#define WED_AFTER(x)		echs_wday_after(x, WED)
#define THU_AFTER(x)		echs_wday_after(x, THU)
#define FRI_AFTER(x)		echs_wday_after(x, FRI)
#define SAT_AFTER(x)		echs_wday_after(x, SAT)
#define SUN_AFTER(x)		echs_wday_after(x, SUN)

#define MON_AFTER_OR_ON(x)	echs_wday_after_or_on(x, MON)
#define TUE_AFTER_OR_ON(x)	echs_wday_after_or_on(x, TUE)
#define WED_AFTER_OR_ON(x)	echs_wday_after_or_on(x, WED)
#define THU_AFTER_OR_ON(x)	echs_wday_after_or_on(x, THU)
#define FRI_AFTER_OR_ON(x)	echs_wday_after_or_on(x, FRI)
#define SAT_AFTER_OR_ON(x)	echs_wday_after_or_on(x, SAT)
#define SUN_AFTER_OR_ON(x)	echs_wday_after_or_on(x, SUN)

#define MON_BEFORE(x)		echs_wday_before(x, MON)
#define TUE_BEFORE(x)		echs_wday_before(x, TUE)
#define WED_BEFORE(x)		echs_wday_before(x, WED)
#define THU_BEFORE(x)		echs_wday_before(x, THU)
#define FRI_BEFORE(x)		echs_wday_before(x, FRI)
#define SAT_BEFORE(x)		echs_wday_before(x, SAT)
#define SUN_BEFORE(x)		echs_wday_before(x, SUN)

#define MON_BEFORE_OR_ON(x)	echs_wday_before_or_on(x, MON)
#define TUE_BEFORE_OR_ON(x)	echs_wday_before_or_on(x, TUE)
#define WED_BEFORE_OR_ON(x)	echs_wday_before_or_on(x, WED)
#define THU_BEFORE_OR_ON(x)	echs_wday_before_or_on(x, THU)
#define FRI_BEFORE_OR_ON(x)	echs_wday_before_or_on(x, FRI)
#define SAT_BEFORE_OR_ON(x)	echs_wday_before_or_on(x, SAT)
#define SUN_BEFORE_OR_ON(x)	echs_wday_before_or_on(x, SUN)

/* ymcw opers */
#define NTH(c, w)	(((c) << 8U) | ((w) & 0xfU))
#define FIRST(x)	NTH(1U, x)
#define SECOND(x)	NTH(2U, x)
#define THIRD(x)	NTH(3U, x)
#define FOURTH(x)	NTH(4U, x)
#define FIFTH(x)	NTH(5U, x)
#define LAST(x)		NTH(5U, x)

#define GET_NTH(spec)	((spec) >> 8U)
#define GET_WDAY(spec)	((spec) & 0xfU)


DECLF echs_stream_t echs_wday_after(echs_stream_t s, echs_wday_t wd);
DECLF echs_stream_t echs_wday_after_or_on(echs_stream_t s, echs_wday_t wd);
DECLF echs_stream_t echs_wday_before(echs_stream_t s, echs_wday_t wd);
DECLF echs_stream_t echs_wday_before_or_on(echs_stream_t s, echs_wday_t wd);

DECLF echs_stream_t echs_bday_after(echs_stream_t s);
DECLF echs_stream_t echs_bday_after_or_on(echs_stream_t s);
DECLF echs_stream_t echs_bday_before(echs_stream_t s);
DECLF echs_stream_t echs_bday_before_or_on(echs_stream_t s);

/**
 * dtor for both echs_wday_after() and echs_wday_before()
 * Return the stream S passed to the respective ctor. */
DECLF echs_stream_t echs_free_wday(echs_stream_t);

/**
 * Set the state name for any builder stream S. */
DECLF void echs_wday_set_state(echs_stream_t s, const char *state);

DECLF echs_stream_t
echs_every_year(echs_instant_t, echs_mon_t mon, unsigned int);

DECLF echs_stream_t
echs_every_month(echs_instant_t, unsigned int dom);

/**
 * Generate a stream with events on WDAY. */
DECLF echs_stream_t
echs_every_week(echs_instant_t, echs_wday_t wday);

DECLF void echs_free_every(echs_stream_t);

/**
 * Set the state name for any builder stream S. */
DECLF void echs_every_set_state(echs_stream_t s, const char *state);


/* just testing */
DECLF echs_stream_t echs_mux(size_t nstrm, echs_stream_t strm[]);
DECLF void echs_free_mux(echs_stream_t mux_strm);
#define __MUX(what)						\
	echs_mux(countof(what), what)
#define ECHS_MUX(args...)					\
	__MUX(((echs_stream_t[]){args}))

DECLF echs_stream_t echs_select(echs_stream_t st, size_t ns, const char *s[]);
DECLF void echs_free_select(echs_stream_t sel_strm);
#define __SELECT(strm, what)			\
	echs_select(strm, countof(what), what)
#define ECHS_SELECT(strm, what...)		\
	__SELECT(strm, ((const char*[])what))

struct echs_rename_atom_s {
	const char *from;
	const char *to;
};

DECLF echs_stream_t
echs_rename(echs_stream_t strm, size_t nr, struct echs_rename_atom_s r[]);
DECLF void echs_free_rename(echs_stream_t ren_strm);
#define __RENAME(strm, what)			\
	echs_rename(strm, countof(what), what)
#define ECHS_RENAME(strm, what...)		\
	__RENAME(strm, ((struct echs_rename_atom_s[])what))

DECLF echs_stream_t
echs_move_after(echs_stream_t blocker, echs_stream_t movees);
DECLF echs_stream_t
echs_move_before(echs_stream_t blocker, echs_stream_t movees);
DECLF void echs_free_move(echs_stream_t move_strm);

#endif	/* INCLUDED_builders_h_ */
