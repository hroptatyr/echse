/*** scale.c -- calendar scales
 *
 * Copyright (C) 2016-2017 Sebastian Freundt
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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>
#include "scale.h"
#include "tzob.h"
#include "nifty.h"

/* hijri calendar */
#include "dat_ummulqura.c"

typedef unsigned int jd_t;

struct ymd_s {
	unsigned int y;
	unsigned int m;
	unsigned int d;
};


static inline __attribute__((const, pure)) jd_t
h2jd(struct ymd_s h)
{
	const unsigned int i = (h.y - 1U) * 12U + (h.m - 1U) - 16260U/*1355AH*/;

	if (UNLIKELY(i >= countof(dat_ummulqura))) {
		return 0U;
	}
	return dat_ummulqura[i] + (h.d - 1U) + 2400000U/*jdn*/;
}

static inline __attribute__((const, pure)) jd_t
g2jd(struct ymd_s g)
{
	const unsigned int b = g.y + 100100U + ((int)g.m - 8) / 6;
	unsigned int d = (b * 1461U) / 4U;

	d += (153U * ((g.m + 9U) % 12U) + 2U) / 5U + g.d;
	return d - ((b / 100U) * 3U) / 4U + 752U - 34840408U;
}

static __attribute__((const, pure)) struct ymd_s
jd2g(jd_t d)
{
/* turn julian day number into gregorian date */
	unsigned int j;
	unsigned int i;
	unsigned int gd, gm, gy;

	j = 4U * d + 139361631U;
	j += ((((4U * d + 183187720U) / 146097U) * 3U) / 4U) * 4U - 3908U;
	i = ((j % 1461U) / 4U) * 5U + 308U;
	gd = ((i % 153U) / 5U) + 1U;
	gm = ((i / 153U) % 12U) + 1U;
	gy = j / 1461U - 100100U + (int)(8 - gm) / 6;
	return (struct ymd_s){gy, gm, gd};
}

static __attribute__((const, pure)) struct ymd_s
jd2h(jd_t d)
{
/* turn julian day number into hijri date,
 * this one does a scan over the ummulqura array, so use seldom */
	size_t i;
	unsigned int m;

	/* use modified julian day number */
	if (UNLIKELY((d -= 2400000U) < *dat_ummulqura)) {
		/* that's before our time */
		goto nil;
	}
	for (i = 0U; i < countof(dat_ummulqura) && dat_ummulqura[i] <= d; i++);
	if (UNLIKELY(i >= countof(dat_ummulqura))) {
		/* that's beyond our time */
		goto nil;
	}
	/* M is the month count */
	m = i + 16260/*1355AH*/;

	return (struct ymd_s){
		(m - 1U) / 12U + 1U, (m - 1U) % 12U + 1U,
			d - dat_ummulqura[i - 1U] + 1U};
nil:
	return (struct ymd_s){};
}

static __attribute__((pure, const)) unsigned int
__ndim_hij(unsigned int y, unsigned int m)
{
/* return the number of days in (hijri) month M in (hijri) year Y. */
	const unsigned int i = (y - 1U) * 12U + (m - 1U) - 16260U/*1355AH*/;

	if (UNLIKELY(!i || i >= countof(dat_ummulqura))) {
		return 0U;
	}
	return dat_ummulqura[i - 0U] - dat_ummulqura[i - 1U];
}

static __attribute__((const, pure)) inline unsigned int
__ndim_greg(unsigned int y, unsigned int m)
{
/* return the number of days in month M in year Y. */
	static const unsigned int mdays[] = {
		0U, 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
	};
	unsigned int res = mdays[m];

	if (UNLIKELY(!(y % 4U) && m == 2U)) {
		res++;
	}
	return res;
}


__attribute__((pure, const)) unsigned int
echs_scale_ndim(echs_scale_t s, unsigned int y, unsigned int m)
{
/* return the number of days in month M in year Y using scale S. */
	switch (s) {
	case SCALE_GREGORIAN:
		return __ndim_greg(y, m);
	case SCALE_HIJRI_UMMULQURA:
		return __ndim_hij(y, m);
	default:
		break;
	}
	return 0U;
}

echs_instant_t
echs_instant_rescale(echs_instant_t i, echs_scale_t tgt)
{
	const echs_scale_t src = echs_instant_scale(i);
	const echs_tzob_t z = echs_instant_tzob(i);
	echs_instant_t tmp =
		echs_instant_detach_scale(echs_instant_detach_tzob(i));

	switch (src) {
	case SCALE_GREGORIAN:
		switch (tgt) {
		case SCALE_GREGORIAN:
			/* nothing to do */
			break;
		case SCALE_HIJRI_UMMULQURA: {
			/* oh no */
			jd_t d = g2jd((struct ymd_s){tmp.y, tmp.m, tmp.d});
			struct ymd_s h;

			if (UNLIKELY(!d)) {
				goto nul;
			}
			/* and back to hijri */
			h = jd2h(d);
			if (UNLIKELY(!h.y)) {
				goto nul;
			}
			/* assign to tgt instant */
			tmp.y = h.y;
			tmp.m = h.m;
			tmp.d = h.d;
			break;
		}
		default:
			goto nul;
		}
		break;
	case SCALE_HIJRI_UMMULQURA:
		switch (tgt) {
		case SCALE_GREGORIAN: {
			jd_t d = h2jd((struct ymd_s){tmp.y, tmp.m, tmp.d});
			struct ymd_s g;

			if (UNLIKELY(!d)) {
				goto nul;
			}
			/* and back to gregorian */
			g = jd2g(d);
			if (UNLIKELY(!g.y)) {
				goto nul;
			}
			/* assign to tgt instant */
			tmp.y = g.y;
			tmp.m = g.m;
			tmp.d = g.d;
			break;
		}
		case SCALE_HIJRI_UMMULQURA:
			/* nothing to do, is there */
			break;
		default:
			goto nul;
		}
		break;
	}
	return echs_instant_attach_tzob(echs_instant_attach_scale(tmp, tgt), z);
nul:
	return echs_nul_instant();
}

/* scale.c ends here */
