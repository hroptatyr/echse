/*** evrrul.c -- recurrence rules
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
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdlib.h>
#include <string.h>
#include "evrrul.h"
#include "nifty.h"


/* generic date converters */
static const unsigned int mdays[] = {
	0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
};

static echs_wday_t
__get_wday(echs_instant_t i)
{
/* sakamoto method, stolen from dateutils */
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	unsigned int year = i.y;
	unsigned int res;

	year -= i.m < 3;
	res = year + year / 4U - year / 100U + year / 400U;
	res += t[i.m - 1U] + i.d;
	return (echs_wday_t)(((unsigned int)res % 7U) ?: SUN);
}

static echs_instant_t
__ymcw_to_inst(echs_instant_t y)
{
/* convert instant with CW coded D slot in Y to Gregorian insants. */
	unsigned int c = y.d >> 4U;
	unsigned int w = y.d & 0x07U;
	echs_instant_t res = y;
	/* get month's first's weekday */
	echs_wday_t wd1 = __get_wday((res.d = 1U, res));
	unsigned int add;
	unsigned int tgtd;

	/* now just like in the __wday_after() code, we want the number of days
	 * to add so that wd(res + X) == W above,
	 * this is a simple modulo subtraction */
	add = ((unsigned int)w + 7U - (unsigned int)wd1) % 7;
	if ((tgtd = 1U + add + (c - 1) * 7U) > mdays[res.m]) {
		if (UNLIKELY(tgtd == 29U && !(res.y % 4U))) {
			/* ah, leap year innit
			 * no need to check for Feb because months are
			 * usually longer than 29 days and we wouldn't be
			 * here if it was a different month */
			;
		} else {
			tgtd -= 7U;
		}
	}
	res.d = tgtd;
	return res;
}


/* recurrence stream classes */
struct evrrul_s {
	echs_evstrm_class_t class;
	evrrul_param_t param;
};

static void free_evrrul(echs_evstrm_t);
static echs_evstrm_t clone_evrrul(echs_evstrm_t);

/* yearly */
static inline echs_event_t
next_evrrul_yly_event(const evrrul_param_t param)
{
/* we trust FROM to sit on the right occurrence */
	echs_event_t res;

	if (UNLIKELY(!param.count)) {
		return echs_nul_event();
	} else if (UNLIKELY(echs_instant_lt_p(param.till, param.proto.from))) {
		return echs_nul_event();
	}
	/* otherwise prep the result */
	res = param.proto;
	/* the dates need fixing up */
	res.from = echs_instant_fixup(res.from);
	res.till = echs_instant_add(res.from, param.dur);
	return res;
}

static echs_event_t
next_evrrul_yearly(echs_evstrm_t s)
{
	struct evrrul_s *this = (struct evrrul_s*)s;
	/* construct the result first */
	const echs_event_t res = next_evrrul_yly_event(this->param);

	if (UNLIKELY(echs_event_0_p(res))) {
		goto out;
	}

	/* now produce the next occurrence */
	this->param.count--;
	this->param.proto.from.y++;
	if (UNLIKELY(this->param.interval)) {
		this->param.proto.from.y += this->param.interval;
	}
out:
	return res;
}

static const struct echs_evstrm_class_s evrrul_yearly_cls = {
	.next = next_evrrul_yearly,
	.free = free_evrrul,
	.clone = clone_evrrul,
};


static inline echs_event_t
next_evrrul_ymcwly_event(const evrrul_param_t param)
{
/* we trust FROM to sit on the right occurrence */
	echs_instant_t next;
	echs_event_t res;

	if (UNLIKELY(!param.count)) {
		return echs_nul_event();
	}
	/* compute next instance */
	next = __ymcw_to_inst(param.proto.from);

	if (UNLIKELY(echs_instant_lt_p(param.till, next))) {
		return echs_nul_event();
	}
	/* otherwise prep the result */
	res = param.proto;
	/* the dates need fixing up */
	res.from = next;
	res.till = echs_instant_add(next, param.dur);
	return res;
}

static echs_event_t
next_evrrul_yearly_ymcw(echs_evstrm_t s)
{
	struct evrrul_s *this = (struct evrrul_s*)s;
	/* construct the result first */
	const echs_event_t res = next_evrrul_ymcwly_event(this->param);

	if (UNLIKELY(echs_event_0_p(res))) {
		goto out;
	}

	/* now produce the next occurrence */
	this->param.count--;
	this->param.proto.from.y++;
	if (UNLIKELY(this->param.interval)) {
		this->param.proto.from.y += this->param.interval;
	}
out:
	return res;
}

static const struct echs_evstrm_class_s evrrul_yearly_ymcw_cls = {
	.next = next_evrrul_yearly_ymcw,
	.free = free_evrrul,
	.clone = clone_evrrul,
};

static echs_evstrm_t
_yearly(evrrul_param_t param, echs_mon_t mon, unsigned int dom)
{
	struct evrrul_s *res;

	if (param.proto.from.m < mon ||
	    param.proto.from.m == mon && param.proto.from.d <= dom) {
		/* all's good */
		;
	} else {
		param.proto.from.y++;
	}
	param.proto.from.m = mon;
	param.proto.from.d = dom;

	/* now set up the result */
	res = malloc(sizeof(*res));
	res->class = &evrrul_yearly_cls;
	res->param = param;
	return (echs_evstrm_t)res;
}

static echs_evstrm_t
_ymcw_yearly(evrrul_param_t param, echs_mon_t mon, unsigned int cw)
{
/* our cw muxer (using the .d slot) */
#define YMCW_MUX(c, w)	(((c) << 4U) | ((w) & 0x0fU))
	unsigned int c = cw >> 8U;
	unsigned int w = cw & 0x0fU;
	echs_instant_t from = param.proto.from;
	struct evrrul_s *res;

	param.proto.from.m = (unsigned int)mon;
	param.proto.from.d = YMCW_MUX(c, w);
	if (!echs_instant_le_p(from, __ymcw_to_inst(param.proto.from))) {
		/* next year then */
		param.proto.from.y++;
	}

	/* now set up the result */
	res = malloc(sizeof(*res));
	res->class = &evrrul_yearly_ymcw_cls;
	res->param = param;
	return (echs_evstrm_t)res;
}

echs_evstrm_t
echs_yearly(evrrul_param_t param, echs_mon_t mon, unsigned int dom)
{
/* short for *-MON-DOM */
	/* special case where date is derived from INITIAL */
	if (mon == NIL && dom == MIR) {
		mon = (echs_mon_t)param.proto.from.m;
		dom = param.proto.from.d;
	}
	/* massage param then */
	if (echs_instant_0_p(param.till)) {
		param.till = echs_max_instant();
	}
	if (!param.count) {
		param.count--;
	}
	if (param.interval) {
		/* use param.interval - 1 really */
		param.interval--;
	}
	/* check the mode we're in */
	if (LIKELY(dom < 32U)) {
		return _yearly(param, mon, dom);
	} else if (dom > FIRST(0U) && dom <= FIFTH(SUN)) {
		/* aaah, ymcw */
		return _ymcw_yearly(param, mon, dom);
	}
	/* otherwise we deem it bollocks */
	return NULL;
}


/* stuff that goes for all the *ly ctors */
static void
free_evrrul(echs_evstrm_t s)
{
	struct evrrul_s *this = (struct evrrul_s*)s;

	free(this);
	return;
}

static echs_evstrm_t
clone_evrrul(echs_evstrm_t s)
{
	struct evrrul_s *new = malloc(sizeof(*new));
	memcpy(new, s, sizeof(*new));
	return (echs_evstrm_t)new;
}


size_t
rrul_fill_yly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	unsigned int y = tgt->y;
	unsigned int m[12U];
	size_t nm;
	int d[12U];
	size_t nd;
	size_t res = 0UL;

	with (unsigned int tmpm) {
		nm = 0UL;
		for (bitint_iter_t mi = 0U;
		     nm < countof(m) && (tmpm = bui31_next(&mi, rr->mon), mi);
		     m[nm++] = tmpm);

		/* fill up with a default */
		if (!nm) {
			m[nm++] = tgt->m;
		}
	}
	with (int tmpd) {
		nd = 0UL;
		for (bitint_iter_t di = 0U;
		     nd < nti && (tmpd = bi31_next(&di, rr->dom), di);
		     d[nd++] = tmpd);

		/* fill up with the default */
		if (!nd) {
			d[nd++] = tgt->d;
		}
	}

	/* now just fill up the array */
	for (;; y++) {
		for (size_t i = 0UL; i < nm; i++) {
			for (size_t j = 0UL; j < nd; j++) {
				tgt[res].y = y;
				tgt[res].m = m[i];
				if (d[j] > 0) {
					tgt[res].d = d[j];
				} else {
					tgt[res].d = mdays[m[i]] + 1U + d[j];
				}
				tgt[res] = echs_instant_fixup(tgt[res]);
				if (++res >= nti) {
					goto fin;
				}
			}
		}
	}
fin:
	return res;
}

/* evrrul.c ends here */
