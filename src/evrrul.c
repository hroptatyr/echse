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

struct md_s {
	unsigned int m;
	unsigned int d;
};

static const unsigned int mdays[] = {
	0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
};

#define M	(MON)
#define T	(TUE)
#define W	(WED)
#define R	(THU)
#define F	(FRI)
#define A	(SAT)
#define S	(SUN)

static const echs_wday_t __jan01_28y_wday[] = {
	/* 1904 - 1910 */
	F, S, M, T, W, F, A,
	/* 1911 - 1917 */
	S, M, W, R, F, A, M,
	/* 1918 - 1924 */
	T, W, R, A, S, M, T,
	/* 1925 - 1931 */
	R, F, A, S, T, W, R,
};

#undef M
#undef T
#undef W
#undef R
#undef F
#undef A
#undef S


/* generic date converters */
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

static inline __attribute__((pure)) echs_wday_t
get_jan01_wday(unsigned int year)
{
/* get the weekday of jan01 in YEAR
 * using the 28y cycle thats valid till the year 2399
 * 1920 = 16 mod 28 */
	return __jan01_28y_wday[year % 28U];
}

static int
ywd_get_jan01_hang(echs_wday_t j01)
{
/* Mon means hang of 0, Tue -1, Wed -2, Thu -3, Fri 3, Sat 2, Sun 1 */
	int res;

	if (UNLIKELY((res = 1 - (int)j01) < -3)) {
		return (int)(7U + res);
	}
	return res;
}

static inline __attribute__((const, pure))  unsigned int
get_isowk(unsigned int y)
{
	switch (y % 28U) {
	default:
		break;
	case 16:
		/* 1920, 1948, ... */
	case 21:
		/* 1925, 1953, ... */
	case 27:
		/* 1931, 1959, ... */
	case 4:
		/* 1936, 1964, ... */
	case 10:
		/* 1942, 1970, ... */
		return 53;
	}
	return 52;
}

static unsigned int
ywd_get_yday(unsigned int y, int w, int d)
{
/* since everything is in ISO 8601 format, getting the doy is a matter of
 * counting how many days there are in a week. */
	/* this one's special as it needs the hang helper slot */
	echs_wday_t j01 = get_jan01_wday(y);
	int hang = ywd_get_jan01_hang(j01);

	if (UNLIKELY(w < 0)) {
		w += 1 + get_isowk(y);
	}
	return 7U * (w - 1) + d + hang;
}

static struct md_s
yd_to_md(unsigned int y, int doy)
{
/* Freundt algo, stolen from dateutils */
#define GET_REM(x)	(rem[x])
	static const uint8_t rem[] = {
		19, 19, 18, 14, 13, 11, 10, 8, 7, 6, 4, 3, 1, 0

	};
	unsigned int m;
	unsigned int d;
	unsigned int beef;
	unsigned int cake;

	/* get 32-adic doys */
	m = (doy + 19) / 32U;
	d = (doy + 19) % 32U;
	beef = GET_REM(m);
	cake = GET_REM(m + 1);

	/* put leap years into cake */
	if (UNLIKELY(!(y % 4U)/*leapp(y)*/ && cake < 16U)) {
		/* note how all leap-affected cakes are < 16 */
		beef += beef < 16U;
		cake++;
	}

	if (d <= cake) {
		d = doy - ((m - 1) * 32 - 19 + beef);
	} else {
		d = doy - (m++ * 32 - 19 + cake);
	}
	return (struct md_s){m, d};
#undef GET_REM
}

static struct md_s
ywd_to_md(unsigned int y, int w, int d)
{
	unsigned int yday = ywd_get_yday(y, w, d);
	struct md_s res = yd_to_md(y, yday);

	if (UNLIKELY(res.m == 0)) {
		res.m = 12;
		res.d--;
	} else if (UNLIKELY(res.m == 13)) {
		res.m = 1;
	}
	return res;
}


/* recurrence helpers */
size_t
rrul_fill_yly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	size_t tries = nti;
	unsigned int y = tgt->y;
	unsigned int m[12U];
	size_t nm;
	int d[12U];
	size_t nd;
	size_t res = 0UL;
	bool ymdp;

	if (UNLIKELY(rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	}

	/* check if we're ymd only */
	ymdp = !bi63_has_bits_p(rr->wk) &&
		!bi383_has_bits_p(&rr->dow) &&
		!bi383_has_bits_p(&rr->doy);

	with (unsigned int tmpm) {
		nm = 0UL;
		for (bitint_iter_t mi = 0U;
		     nm < countof(m) && (tmpm = bui31_next(&mi, rr->mon), mi);
		     m[nm++] = tmpm);

		/* fill up with a default */
		if (!nm && ymdp) {
			m[nm++] = tgt->m;
		}
	}
	with (int tmpd) {
		nd = 0UL;
		for (bitint_iter_t di = 0U;
		     nd < nti && (tmpd = bi31_next(&di, rr->dom), di);
		     d[nd++] = tmpd + 1);

		/* fill up with the default */
		if (!nd && ymdp) {
			d[nd++] = tgt->d;
		}
	}

	/* fill up the array the hard way */
	for (res = 0UL; res < nti && --tries > 0U; y += rr->inter) {
		bitint383_t cand = {0U};
		int yd;

		for (bitint_iter_t wki = 0UL;
		     (yd = bi63_next(&wki, rr->wk), wki);) {
			int dow;
			for (bitint_iter_t dowi = 0UL;
			     (dow = bi383_next(&dowi, &rr->dow), dowi);) {
				struct md_s md;

				if (dow <= MIR || dow > SUN) {
					continue;
				} else if (!(md = ywd_to_md(y, yd, dow)).m) {
					continue;
				}
				/* otherwise it's looking good */
				ass_bi383(&cand, (md.m - 1U) * 32U + md.d);
			}
		}

		for (bitint_iter_t doyi = 0UL;
		     (yd = bi383_next(&doyi, &rr->doy), doyi);) {
			struct md_s md;

			if (!(md = yd_to_md(y, yd)).m) {
				continue;
			}
			/* otherwise it's looking good */
			ass_bi383(&cand, (md.m - 1U) * 32U + md.d);
		}

		for (size_t i = 0UL; i < nm; i++) {
			for (size_t j = 0UL; j < nd; j++) {
				int dd = d[j];

				if (dd > 0 && (unsigned int)dd <= mdays[m[i]]) {
					;
				} else if (dd < 0 &&
					   mdays[m[i]] + 1U + dd > 0) {
					dd += mdays[m[i]] + 1U;
				} else if (UNLIKELY(m[i] == 2U && !(y % 4U))) {
					/* fix up leap years */
					switch (dd) {
					case -29:
						dd = 1;
					case 29:
						break;
					default:
						goto invalid;
					}
				} else {
				invalid:
					if (LIKELY(--tries > 0)) {
						continue;
					}
					goto fin;
				}

				/* it's a candidate */
				ass_bi383(&cand, (m[i] - 1U) * 32U + dd);
				tries = nti;
			}
		}

		/* now check the bitset */
		for (bitint_iter_t all = 0UL;
		     res < nti && (yd = bi383_next(&all, &cand), all); res++) {
			tgt[res].y = y;
			tgt[res].m = yd / 32U + 1U;
			tgt[res].d = yd % 32U;

			if (UNLIKELY(echs_instant_lt_p(rr->until, tgt[res]))) {
				goto fin;
			}
		}
	}
fin:
	return res;
}

/* evrrul.c ends here */
