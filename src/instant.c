/*** instant.c -- some echs_instant_t functionality
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
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */

#include "instant.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */

static const unsigned int doy[] = {
	0U, 0U, 31U, 59U, 90U, 120U, 151U, 181U, 212U, 243U, 273U, 304U, 334U,
	365U, 396U, 424U, 455U, 485U, 516U, 546U, 577U, 608U, 638U, 669U, 699U,
};


echs_instant_t
echs_instant_fixup(echs_instant_t e)
{
/* this is basically __ymd_fixup_d of dateutils
 * we only care about additive cockups though because instants are
 * chronologically ascending */
	static const unsigned int mdays[] = {
		0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
	};
	unsigned int md;

	if (UNLIKELY(echs_instant_all_day_p(e))) {
		/* just fix up the day, dom and year portion */
		goto fixup_d;
	} else if (UNLIKELY(echs_instant_all_day_p(e))) {
		/* just fix up the sec, min, ... portions */
		goto fixup_S;
	}

	if (UNLIKELY(e.ms >= 1000U)) {
		unsigned int dS = e.ms / 1000U;
		unsigned int ms = e.ms % 1000U;

		e.ms = ms;
		e.S += dS;
	}

fixup_S:
	if (UNLIKELY(e.S >= 60U)) {
		/* leap seconds? */
		unsigned int dM = e.S / 60U;
		unsigned int S = e.S % 60U;

		e.S = S;
		e.M += dM;
	}
	if (UNLIKELY(e.M >= 60U)) {
		unsigned int dH = e.M / 60U;
		unsigned int M = e.M % 60U;

		e.M = M;
		e.H += dH;
	}
	if (UNLIKELY(e.H >= 24U)) {
		unsigned int dd = e.H / 24U;
		unsigned int H = e.H % 24U;

		e.H = H;
		e.d += dd;
	}

fixup_d:
refix_ym:
	if (UNLIKELY(e.m > 12U)) {
		unsigned int dy = (e.m - 1) / 12U;
		unsigned int m = (e.m - 1) % 12U + 1U;

		e.m = m;
		e.y += dy;
	}

	if (UNLIKELY(e.d > (md = mdays[e.m]))) {
		/* leap year handling */
		if (UNLIKELY(e.m == 2U && (e.y % 4U) == 0U)) {
			md++;
		}
		if (LIKELY((e.d -= md) > 0U)) {
			e.m++;
			goto refix_ym;
		}
	}
	return e;
}

static inline unsigned int
__doy(echs_instant_t i)
{
	unsigned int res = doy[i.m] + i.d;

	if (UNLIKELY((i.y % 4U) == 0) && i.m >= 3) {
		res++;
	}
	return res;
}

echs_idiff_t
echs_instant_diff(echs_instant_t end, echs_instant_t beg)
{
	int extra_df;
	int intra_df;

	/* just see what the intraday part yields for the difference */
	intra_df = end.H - beg.H;
	intra_df *= 60;
	intra_df += end.M - beg.M;
	intra_df *= 60;
	intra_df += end.S - beg.S;
	intra_df *= 1000;
	intra_df += end.ms - beg.ms;

	if (intra_df < 0) {
		intra_df += 24 * 60 * 60 * 1000;
		extra_df = -1;
	} else if (intra_df < 24 * 60 * 60 * 1000) {
		extra_df = 0;
	} else {
		extra_df = 1;
	}

	{
		unsigned int dom_end = __doy(end);
		unsigned int dom_beg = __doy(beg);
		int df_y = end.y - beg.y;

		if ((extra_df += dom_end - dom_beg) < 0) {
			df_y--;
		}
		extra_df += df_y * 365 + (df_y - 1) / 4;
	}

	return (echs_idiff_t){extra_df, intra_df};
}

echs_instant_t
echs_instant_add(echs_instant_t bas, echs_idiff_t add)
{
	echs_instant_t res;
	int dd = add.dd;
	int msd = add.msd;
	int df_y;
	int df_m;

	res.y = bas.y + dd / 365U;
	if ((df_y = res.y - bas.y)) {
		dd -= df_y * 365 + (df_y - 1) / 4;
	}

	res.m = bas.m + dd / 31;
	if ((df_m = res.m - bas.m)) {
		dd -= doy[bas.m + df_m] - doy[bas.m + 1];
	}

	res.d = bas.d + dd;

	res.ms = bas.ms + msd % 1000;
	msd /= 1000;
	res.S = bas.S + msd % 60;
	msd /= 60;
	res.M = bas.M + msd % 60;
	msd /= 60;
	res.H = bas.H + msd % 24;
	msd /= 24;

	res.d += msd;
	return echs_instant_fixup(res);
}

/* instant.c ends here */
