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
#include "dat_diyanet.c"

typedef unsigned int mjd_t;

struct ymd_s {
	unsigned int y;
	unsigned int m;
	unsigned int d;
};

/* for table based hijri calendars */
/* monthly transitions for calendars with fixed number of months in year */
#define SM(x)	(x[0U])
/* number of months in a year */
#define EM(x)	(x[1U])
/* the actual month transitions data */
#define MT(x)	(x + 2U)
/* determine number of months in the MT array */
#define NM(x)	(sizeof(x) / sizeof(unsigned int) - 2U)


static inline __attribute__((const, pure)) mjd_t
ht2mjd(const unsigned int *cal, size_t nm, struct ymd_s h)
{
	const unsigned int i = (h.y - 1U) * 12U + (h.m - 1U) - SM(cal);

	if (UNLIKELY(i >= nm)) {
		return 0U;
	}
	return MT(cal)[i] + (h.d - 1U);
}

static inline __attribute__((const, pure)) mjd_t
g2mjd(struct ymd_s g)
{
	const unsigned int b = g.y + 100100U + ((int)g.m - 8) / 6;
	unsigned int d = (b * 1461U) / 4U;

	d += (153U * ((g.m + 9U) % 12U) + 2U) / 5U + g.d;
	return d - ((b / 100U) * 3U) / 4U + 752U - 34840408U - 2400000U;
}

static __attribute__((const, pure)) struct ymd_s
mjd2g(mjd_t d)
{
/* turn julian day number into gregorian date */
	unsigned int j;
	unsigned int i;
	unsigned int gd, gm, gy;

	/* this one uses julian day numbers */
	d += 2400000U;
	j = 4U * d + 139361631U;
	j += ((((4U * d + 183187720U) / 146097U) * 3U) / 4U) * 4U - 3908U;
	i = ((j % 1461U) / 4U) * 5U + 308U;
	gd = ((i % 153U) / 5U) + 1U;
	gm = ((i / 153U) % 12U) + 1U;
	gy = j / 1461U - 100100U + (int)(8 - gm) / 6;
	return (struct ymd_s){gy, gm, gd};
}

static __attribute__((const, pure)) struct ymd_s
mjd2ht(const unsigned int *cal, size_t nm, mjd_t d)
{
/* turn julian day number into hijri date,
 * this one does a scan over the month transitions array, so use seldom */
	size_t i;
	unsigned int m;

	for (i = 0U; i < nm && MT(cal)[i] <= d; i++);
	if (UNLIKELY(i >= nm)) {
		/* that's beyond our time */
		goto nil;
	}
	/* M is the month count */
	m = i + SM(cal);

	return (struct ymd_s){
		.y = (m - 1U) / 12U + 1U,
		.m = (m - 1U) % 12U + 1U,
		.d = d - MT(cal)[i - 1U] + 1U
	};
nil:
	return (struct ymd_s){};
}

static __attribute__((pure, const)) unsigned int
__ndim_hij(const unsigned int *cal, size_t nm, unsigned int y, unsigned int m)
{
/* return the number of days in (hijri) month M in (hijri) year Y. */
	const unsigned int i = (y - 1U) * 12U + (m - 1U) - SM(cal);

	if (UNLIKELY(i + 1U >= nm)) {
		return 0U;
	}
	return MT(cal)[i + 1U] - MT(cal)[i + 0U];
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

static __attribute__((const, pure)) echs_wday_t
__wday_greg(unsigned int y, unsigned int m, unsigned int d)
{
/* sakamoto method, stolen from dateutils */
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	unsigned int res;

	y -= m < 3;
	res = y + y / 4U - y / 100U + y / 400U;
	res += t[m - 1U] + d;
	return (echs_wday_t)(((unsigned int)res % 7U) ?: SUN);
}

static __attribute__((const, pure)) echs_wday_t
__wday_hij(
	const unsigned int *cal, size_t nm,
	unsigned int y, unsigned int m, unsigned int d)
{
	const mjd_t j = ht2mjd(cal, nm, (struct ymd_s){y, m, d});
	return (echs_wday_t)(((j + 1U) % 7U) + 1U);
}


unsigned int echs_scale_min[] = {
	[SCALE_GREGORIAN] = 1600U,
	[SCALE_HIJRI_UMMULQURA] = 1355U,
	[SCALE_HIJRI_DIYANET] = 1318U,
};

unsigned int echs_scale_max[] = {
	[SCALE_GREGORIAN] = 4095U,
	[SCALE_HIJRI_UMMULQURA] = 1500U,
	[SCALE_HIJRI_DIYANET] = 1444U,
};

__attribute__((pure, const)) unsigned int
echs_scale_ndim(echs_scale_t s, unsigned int y, unsigned int m)
{
/* return the number of days in month M in year Y using scale S. */
	switch (s) {
	case SCALE_GREGORIAN:
		return __ndim_greg(y, m);
	case SCALE_HIJRI_UMMULQURA:
		return __ndim_hij(dat_ummulqura, NM(dat_ummulqura), y, m);
	case SCALE_HIJRI_DIYANET:
		return __ndim_hij(dat_diyanet, NM(dat_diyanet), y, m);
	default:
		break;
	}
	return 0U;
}

__attribute__((pure, const)) echs_wday_t
echs_scale_wday(echs_scale_t s, unsigned int y, unsigned int m, unsigned int d)
{
	switch (s) {
	case SCALE_GREGORIAN:
		return __wday_greg(y, m, d);
	case SCALE_HIJRI_UMMULQURA:
		return __wday_hij(dat_ummulqura, NM(dat_ummulqura), y, m, d);
	case SCALE_HIJRI_DIYANET:
		return __wday_hij(dat_diyanet, NM(dat_diyanet), y, m, d);
	}
	return MIR;
}

echs_instant_t
echs_instant_rescale(echs_instant_t i, echs_scale_t tgt)
{
	const echs_scale_t src = echs_instant_scale(i);
	const echs_tzob_t z = echs_instant_tzob(i);
	echs_instant_t tmp =
		echs_instant_detach_scale(echs_instant_detach_tzob(i));

	if (src != tgt) {
		mjd_t d;
		struct ymd_s tgg;

		switch (src) {
		case SCALE_GREGORIAN:
			d = g2mjd((struct ymd_s){tmp.y, tmp.m, tmp.d});;
			break;
		case SCALE_HIJRI_UMMULQURA:
			d = ht2mjd(
				dat_ummulqura, NM(dat_ummulqura),
				(struct ymd_s){tmp.y, tmp.m, tmp.d});
			break;
		case SCALE_HIJRI_DIYANET:
			d = ht2mjd(
				dat_diyanet, NM(dat_diyanet),
				(struct ymd_s){tmp.y, tmp.m, tmp.d});
			break;
		default:
			goto nul;
		}
		/* now convert to TGT scale */
		switch (tgt) {
		case SCALE_GREGORIAN:
			tgg = mjd2g(d);
			if (UNLIKELY(!tgg.y)) {
				goto nul;
			}
			break;
		case SCALE_HIJRI_UMMULQURA:
			tgg = mjd2ht(dat_ummulqura, NM(dat_ummulqura), d);
			if (UNLIKELY(!tgg.y)) {
				goto nul;
			}
			break;
		case SCALE_HIJRI_DIYANET:
			tgg = mjd2ht(dat_diyanet, NM(dat_diyanet), d);
			if (UNLIKELY(!tgg.y)) {
				goto nul;
			}
			break;
		default:
			goto nul;
		}
		/* assign to tgt instant */
		tmp.y = tgg.y;
		tmp.m = tgg.m;
		tmp.d = tgg.d;
	}
	return echs_instant_attach_tzob(echs_instant_attach_scale(tmp, tgt), z);
nul:
	return echs_nul_instant();
}

/* scale.c ends here */
