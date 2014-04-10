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
static __attribute__((const, pure)) echs_wday_t
ymd_get_wday(unsigned int y, unsigned int m, unsigned int d)
{
/* sakamoto method, stolen from dateutils */
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	unsigned int res;

	y -= m < 3;
	res = y + y / 4U - y / 100U + y / 400U;
	res += t[m - 1U] + d;
	return (echs_wday_t)(((unsigned int)res % 7U) ?: SUN);
}

static int
__get_mcnt(unsigned int y, unsigned int m, echs_wday_t w)
{
/* get the number of weekdays W in Y-M, which is the max count
 * for a weekday W in ymcw dates in year Y and month M */
	echs_wday_t wd1 = ymd_get_wday(y, m, 1U);
	unsigned int md = mdays[m];
	/* the maximum number of WD01s in Y-M */
	unsigned int wd01cnt = (md - 1U) / 7U + 1;
	/* modulus */
	unsigned int wd01mod = (md - 1U) % 7U;

	/* we need sun on 0 */
	if (w == SUN) {
		w = MIR;
	}
	/* now the next WD01MOD days also have WD01CNT occurrences
	 * if wd01 + wd01mod exceeds the DAYS_PER_WEEK barrier wrap
	 * around by extending W to W + DAYS_PER_WEEK */
	if ((w >= wd1 && w <= wd1 + wd01mod) ||
	    (w + 7U) <= wd1 + wd01mod) {
		return wd01cnt;
	} else {
		return wd01cnt - 1;
	}
}

static unsigned int
ymcw_get_dom(unsigned int y, unsigned int m, int c, echs_wday_t w)
{
/* convert instant with CW coded D slot in Y to Gregorian insants. */
	/* get month's first's weekday */
	echs_wday_t wd1 = ymd_get_wday(y, m, 1U);
	unsigned int max = __get_mcnt(y, m, w);
	unsigned int add;
	unsigned int tgtd;

	/* quick sanity check */
	if (c > (int)max) {
		return 0U;
	} else if (c < 0 && (c += max + 1) <= 0) {
		return 0U;
	}

	/* now just like in the __wday_after() code, we want the number of days
	 * to add so that wd(res + X) == W above,
	 * this is a simple modulo subtraction */
	add = ((unsigned int)w + 7U - (unsigned int)wd1) % 7U;
	if ((tgtd = 1U + add + (c - 1) * 7U) > mdays[m]) {
		if (UNLIKELY(tgtd == 29U && !(y % 4U))) {
			/* ah, leap year innit
			 * no need to check for Feb because months are
			 * usually longer than 29 days and we wouldn't be
			 * here if it was a different month */
			;
		} else {
			tgtd -= 7U;
		}
	}
	return tgtd;
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
ywd_to_md(unsigned int y, int w, echs_wday_t d)
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

static unsigned int
ycw_get_yday(unsigned int y, int c, echs_wday_t w)
{
/* this differs from ywd in that we're actually looking for the C-th
 * occurrence of W in year Y
 * this is the inverse of dateutils' ymcw.c:__ymcw_get_yday()
 * look there for details */
	echs_wday_t j01w = ymd_get_wday(y, 1, 1);
	unsigned int diff = j01w <= w ? w - j01w : 7 + w - j01w;

	/* if W == j01w, the first W is the first yday
	 * the second W is the 8th yday, etc.
	 * so the first W is on 1 + diff */
	if (c > 0) {
		return 7 * (c - 1) + diff + 1;
	} else if (c < 0) {
		/* similarly for negative c,
		 * there's always the 53rd J01 in Y,
		 * mapping to the 365th day */
		unsigned int res = 7U * (53 + c) + diff + 1U;

		switch (diff) {
		default:
			break;
		case 0:
			return res;
		case 1:
			if (UNLIKELY(!(y % 4U)/*leap year*/)) {
				return res;
			}
			break;
		}
		return res - 7;
	}
	/* otherwise it's bullshit */
	return 0U;
}

static unsigned int
easter_get_yday(unsigned int y)
{
/* calculate gregorian easter date */
	unsigned int a = y % 19U;
	unsigned int b = y / 4U;
	unsigned int c = b / 25U + 1;
	unsigned int d = 3U * c / 4U;
	unsigned int e;

	e = 19U * a + -((8U * c + 5) / 25U) + d + 15U;
	e %= 30U;
	e += (29578U - a - 32U * e) / 1024U;
	e = e - ((y % 7U) + b - d + e + 2) % 7U;
	/* e is expressed in terms of Mar, so add the days till 01/Mar
	 * to obtain the doy */
	return e + 59 + !(y % 4U);
}


/* recurrence helpers */
static void
fill_yly_ywd(
	bitint383_t *restrict cand, unsigned int y,
	const bitint63_t woy, const bitint383_t *dow)
{
	int wk;

	for (bitint_iter_t wki = 0UL;
	     (wk = bi63_next(&wki, woy), wki);) {
		/* ywd */
		int dc;

		for (bitint_iter_t dowi = 0UL;
		     (dc = bi383_next(&dowi, dow), dowi);) {
			struct md_s md;
			echs_wday_t wd;

			if (dc <= MIR || (wd = (echs_wday_t)dc) > SUN) {
				continue;
			} else if (!(md = ywd_to_md(y, wk, wd)).m) {
				continue;
			}
			/* otherwise it's looking good */
			ass_bi383(cand, (md.m - 1U) * 32U + md.d);
		}
	}
	return;
}

static void
fill_yly_ymcw(
	bitint383_t *restrict cand, unsigned int y,
	const bitint383_t *dow, unsigned int m[static 12U], size_t nm)
{
	for (size_t i = 0UL; i < nm; i++) {
		int dc;

		for (bitint_iter_t dowi = 0UL;
		     (dc = bi383_next(&dowi, dow), dowi);) {
			int cnt;
			unsigned int dom;
			echs_wday_t wd;

			if (UNLIKELY((cnt = dc / 7) == 0 && dc > 0)) {
				continue;
			} else if (cnt == 0) {
				--cnt;
			}
			if ((wd = (echs_wday_t)(dc - cnt * 7)) == MIR) {
				wd = SUN;
				cnt--;
			}

			if (!(dom = ymcw_get_dom(y, m[i], cnt, wd))) {
				continue;
			}
			/* otherwise it's looking good */
			ass_bi383(cand, (m[i] - 1U) * 32U + dom);
		}
	}
	return;
}

static void
fill_yly_ycw(bitint383_t *restrict cand, unsigned int y, const bitint383_t *dow)
{
	int dc;

	for (bitint_iter_t dowi = 0UL;
	     (dc = bi383_next(&dowi, dow), dowi);) {
		struct md_s md;
		unsigned int yd;
		int cnt;
		echs_wday_t wd;

		if (UNLIKELY((cnt = dc / 7) == 0 && dc > 0)) {
			continue;
		} else if (cnt == 0) {
			--cnt;
		}
		if ((wd = (echs_wday_t)(dc - cnt * 7)) == MIR) {
			wd = SUN;
			cnt--;
		}

		if (!(yd = ycw_get_yday(y, cnt, wd))) {
			continue;
		} else if (!(md = yd_to_md(y, yd)).m) {
			continue;
		}
		/* otherwise it's looking good */
		ass_bi383(cand, (md.m - 1U) * 32U + md.d);
	}
	return;
}

static void
fill_yly_yd(bitint383_t *restrict cand, unsigned int y, const bitint383_t *doy)
{
	int yd;

	for (bitint_iter_t doyi = 0UL;
	     (yd = bi383_next(&doyi, doy), doyi);) {
		/* yd */
		struct md_s md;

		if (!(md = yd_to_md(y, yd)).m) {
			continue;
		}
		/* otherwise it's looking good */
		ass_bi383(cand, (md.m - 1U) * 32U + md.d);
	}
	return;
}

static void
fill_yly_eastr(bitint383_t *restrict cand, unsigned int y, const bitint383_t *s)
{
	int offs;

	for (bitint_iter_t easteri = 0UL;
	     (offs = bi383_next(&easteri, s), easteri);) {
		/* easter offset calendar */
		unsigned int yd;
		struct md_s md;

		if (!(yd = easter_get_yday(y))) {
			continue;
		} else if (!(yd += offs) || yd > 366) {
			/* huh? */
			continue;
		} else if (!(md = yd_to_md(y, yd)).m) {
			continue;
		}
		/* otherwise it's looking good */
		ass_bi383(cand, (md.m - 1U) * 32U + md.d);
	}
	return;
}

static void
fill_yly_ymd(
	bitint383_t *restrict cand, unsigned int y,
	unsigned int m[static 12U], size_t nm,
	int d[static 31U], size_t nd)
{
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
				continue;
			}

			/* it's a candidate */
			ass_bi383(cand, (m[i] - 1U) * 32U + dd);
		}
	}
	return;
}

size_t
rrul_fill_yly(echs_instant_t *restrict tgt, size_t nti, rrulsp_t rr)
{
	unsigned int y = tgt->y;
	unsigned int m[12U];
	size_t nm;
	int d[31U];
	size_t nd;
	size_t res = 0UL;
	size_t tries;
	bool ymdp;

	if (UNLIKELY(rr->count < nti)) {
		if (UNLIKELY((nti = rr->count) == 0UL)) {
			goto fin;
		}
	}

	/* check if we're ymd only */
	ymdp = !bi63_has_bits_p(rr->wk) &&
		!bi383_has_bits_p(&rr->dow) &&
		!bi383_has_bits_p(&rr->doy) &&
		!bi383_has_bits_p(&rr->easter);

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
	for (res = 0UL, tries = 64U; res < nti && --tries; y += rr->inter) {
		bitint383_t cand = {0U};
		int yd;

		if (bi63_has_bits_p(rr->wk) && bi383_has_bits_p(&rr->dow)) {
			/* ywd */
			fill_yly_ywd(&cand, y, rr->wk, &rr->dow);
		} else if (!bi63_has_bits_p(rr->wk) && nm) {
			/* ymcw */
			fill_yly_ymcw(&cand, y, &rr->dow, m, nm);
		} else if (!bi63_has_bits_p(rr->wk)) {
			/* ycw */
			fill_yly_ycw(&cand, y, &rr->dow);
		}
		/* extend by yd */
		fill_yly_yd(&cand, y, &rr->doy);

		/* extend by easter */
		fill_yly_eastr(&cand, y, &rr->easter);

		/* extend by ymd */
		fill_yly_ymd(&cand, y, m, nm, d, nd);

		/* now check the bitset */
		for (bitint_iter_t all = 0UL;
		     res < nti && (yd = bi383_next(&all, &cand), all); res++) {
			tgt[res].y = y;
			tgt[res].m = yd / 32U + 1U;
			tgt[res].d = yd % 32U;

			if (UNLIKELY(echs_instant_lt_p(rr->until, tgt[res]))) {
				goto fin;
			}
			tries = 64U;
		}
	}
fin:
	return res;
}

/* evrrul.c ends here */
