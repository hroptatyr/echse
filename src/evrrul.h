/*** evrrul.h -- recurrence rules
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
#if !defined INCLUDED_evrrul_h_
#define INCLUDED_evrrul_h_

#include <stddef.h>
#include <stdbool.h>
#include "evstrm.h"
#include "instant.h"
#include "bitint.h"

typedef const struct rrulsp_s *rrulsp_t;

typedef enum {
	MIR = (0U),
	MON = (1U),
	TUE = (2U),
	WED = (3U),
	THU = (4U),
	FRI = (5U),
	SAT = (6U),
	SUN = (7U),
} echs_wday_t;

typedef enum {
	NIL = (0U),
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

typedef enum {
	FREQ_NONE,
	FREQ_YEARLY,
	FREQ_MONTHLY,
	FREQ_WEEKLY,
	FREQ_DAILY,
	FREQ_HOURLY,
	FREQ_MINUTELY,
	FREQ_SECONDLY,
} echs_freq_t;

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

struct rrulsp_s {
	echs_freq_t freq;
	unsigned int count;
	unsigned int inter;
	echs_instant_t until;

	bitint31_t dom;
	bitint383_t doy;

	/* we'll store mon->1, tue->2, ..., 1mon->8, 2mon->15, ...
	 * -1mon->-1, -1tue->-2, ..., -2mon->-8, -3mon->-15, ... */
	bitint447_t dow;

	bituint31_t mon;
	bitint63_t wk;

	bituint31_t H;
	bituint63_t M, S;

	bitint383_t pos;
	bitint383_t easter;
	bitint383_t add;
};

struct cd_s {
	int cnt;
	echs_wday_t dow;
};


extern size_t
rrul_fill_yly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr);

extern size_t
rrul_fill_mly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr);

extern size_t
rrul_fill_dly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr);

extern size_t
rrul_fill_wly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr);

extern bool echs_instant_matches_p(rrulsp_t f, echs_instant_t i);


#define CD(args...)	((struct cd_s){args})

static inline int
pack_cd(struct cd_s cd)
{
	return (cd.cnt << 3) | cd.dow;
}

static inline struct cd_s
unpack_cd(int cd)
{
	return (struct cd_s){cd >> 3, (echs_wday_t)(cd & 0b111U)};
}

#endif	/* INCLUDED_evrrul_h_ */
