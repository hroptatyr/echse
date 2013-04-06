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

#define MON_AFTER(x)	__wday_after(x, MON)
#define TUE_AFTER(x)	__wday_after(x, TUE)
#define WED_AFTER(x)	__wday_after(x, WED)
#define THU_AFTER(x)	__wday_after(x, THU)
#define FRI_AFTER(x)	__wday_after(x, FRI)
#define SAT_AFTER(x)	__wday_after(x, SAT)
#define SUN_AFTER(x)	__wday_after(x, SUN)

#define MON_BEFORE(x)	__wday_before(x, MON)
#define TUE_BEFORE(x)	__wday_before(x, TUE)
#define WED_BEFORE(x)	__wday_before(x, WED)
#define THU_BEFORE(x)	__wday_before(x, THU)
#define FRI_BEFORE(x)	__wday_before(x, FRI)
#define SAT_BEFORE(x)	__wday_before(x, SAT)
#define SUN_BEFORE(x)	__wday_before(x, SUN)


DECLF echs_stream_t echs_wday_after(echs_stream_t s, echs_wday_t wd);
DECLF echs_stream_t echs_wday_before(echs_stream_t s, echs_wday_t wd);

/* dtor for both echs_wday_after() and echs_wday_before() */
DECLF void echs_free_wday(echs_stream_t);

DECLF echs_stream_t
echs_every_year(echs_instant_t, echs_mon_t mon, unsigned int dom);

DECLF echs_stream_t
echs_every_month(echs_instant_t, unsigned int dom);

DECLF void echs_free_every(echs_stream_t);

#endif	/* INCLUDED_builders_h_ */
