/*** solar.c -- sun rise, sun set stream
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
/* These computations follow: http://www.stjarnhimlen.se/comp/riset.html */
#include <math.h>
#include "echse.h"
#include "instant.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */
#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */

#if defined __INTEL_COMPILER
# pragma warning (disable:981)
#endif	/* __INTEL_COMPILER */

struct clo_s {
	/* latitude */
	double lat;
	/* longitude */
	double lng;

	/* our metronome, in days */
	int now;

	enum {
		ST_UNK,
		ST_RISE,
		ST_NOON,
		ST_SET,
	} state;

	double noon;
	double lha;
};

struct orb_s {
	double N;
	double i;
	double w;
	double a;
	double e;
	double M;
};

struct rasc_decl_s {
	double rasc;
	double decl;

	double gmst0;
};

struct decomp_s {
	int ip;
	double r;
};


#define pi	3.14159265358979323846
#define RAD(x)	((x) * pi / 180.0)

static inline __attribute__((pure, const)) double
fmod_2pi(double x)
{
	if (x > 0.0) {
		return fmod(x, 2 * pi);
	} else {
		return 2 * pi + fmod(x, 2 * pi);
	}
}

static inline __attribute__((pure)) struct decomp_s
decomp(double x)
{
	double ip;
	double r = modf(x, &ip);
	return (struct decomp_s){.ip = (int)ip, .r = r};
}


static struct orb_s
orb_sun(double d)
{
	struct orb_s res = {
		.N = 0.0,
		.i = 0.0,
		.w = RAD(282.9404) + RAD(4.70935e-5) * d,
		.a = 1.0,
		.e = 0.016709 - 1.151e-9 * d,
		.M = RAD(356.0470) + RAD(0.9856002585) * d,
	};
	return res;
}

static int
instant_to_days(echs_instant_t i)
{
	unsigned int d = 367U * i.y -
		7U * (i.y + (i.m + 9U) / 12U) / 4U + 275U * i.m / 9U + i.d;
	//unsigned int s = ((i.H * 60U + i.M) * 60U + i.S) * 1000U + i.ms;
	return d - 730530;
}

static echs_instant_t
dh_to_instant(int d, double h)
{
/* stolen from dateutils' daisy.c */
	echs_instant_t i;
	int y;
	int j00;
	unsigned int doy;

	/* get year first (estimate) */
	y = --d / 365U;
	/* get jan-00 of (est.) Y */
	j00 = y * 365U + y / 4U;
	/* y correct? */
	if (UNLIKELY(j00 > d)) {
		/* correct y */
		y--;
		/* and also recompute the j00 of y */
		j00 = y * 365U + y / 4U;
	}
	/* ass */
	i.y = y + 2000U;
	/* this one must be positive now */
	doy = d - j00;

	/* get month and day from doy */
	{
#define GET_REM(x)	(rem[x])
		static const uint8_t rem[] = {
			19, 19, 18, 14, 13, 11, 10, 8, 7, 6, 4, 3, 1, 0

		};
		unsigned int mdm;
		unsigned int mdd;
		unsigned int beef;
		unsigned int cake;

		/* get 32-adic doys */
		mdm = (doy + 19) / 32U;
		mdd = (doy + 19) % 32U;
		beef = GET_REM(mdm);
		cake = GET_REM(mdm + 1);

		/* put leap years into cake */
		if (UNLIKELY((y % 4) == 0U && cake < 16U)) {
			/* note how all leap-affected cakes are < 16 */
			beef += beef < 16U;
			cake++;
		}

		if (mdd <= cake) {
			mdd = doy - ((mdm - 1) * 32 - 19 + beef);
		} else {
			mdd = doy - (mdm++ * 32 - 19 + cake);
		}
		i.m = mdm;
		i.d = mdd;
#undef GET_REM
	}

	/* account for H M S now */
	{
		struct decomp_s ipr;

		i.H = (ipr = decomp(h * 24.0 / (2 * pi))).ip;
		i.M = (ipr = decomp(ipr.r * 60.0)).ip;
		i.S = (ipr = decomp(ipr.r * 60.0)).ip;
		i.ms = (unsigned int)(ipr.r * 1000.0);
	}
	return i;
}

static double
ecl_earth(double d)
{
	return RAD(23.4393) - RAD(3.563E-7) * d;
}

static struct rasc_decl_s
rasc_decl_sun(double d)
{
	struct orb_s o = orb_sun(d);
	double ecl = ecl_earth(d);

	double E = o.M + o.e * sin(o.M) * (1.0 + o.e * cos(o.M));
	double xv = cos(E) - o.e;
	double yv = sin(E) * sqrt(1.0 - o.e * o.e);

	double v = atan2(yv, xv);
	double r = sqrt(xv * xv + yv * yv);
	double lng_sun = v + o.w;

	double xs = r * cos(lng_sun);
	double ys = r * sin(lng_sun);
	double UNUSED(zs) = 0.0;

	double xe = xs;
	double ye = ys * cos(ecl);
	double ze = ys * sin(ecl);

	/* final result */
	struct rasc_decl_s res = {
		.rasc = atan2(ye, xe),
		.decl = atan2(ze, sqrt(xe * xe + ye * ye)),
		.gmst0 = o.M + o.w + pi,
	};

	return res;
}

static double
noon_sun(const struct clo_s *clo, struct rasc_decl_s rd)
{
	return fmod_2pi(rd.rasc - rd.gmst0 - clo->lng);
}

static double
cos_lha(const struct clo_s *clo, struct rasc_decl_s rd)
{
/* cosine of sun's local hour angle */
	const double h = RAD(-0.833);

	return (sin(h) - sin(clo->lat) * sin(rd.decl)) /
		(cos(clo->lat) * cos(rd.decl));
}

static echs_event_t
__sun(void *vclo)
{
#define YIELD(x)	return x
	DEFISTATE(SUNRISE);
	DEFISTATE(SUNSET);
	DEFISTATE(NOON);
	struct clo_s *clo = vclo;
	echs_event_t e;

	while (1) {
		switch (clo->state) {
		default:
		case ST_UNK: {
			struct rasc_decl_s rd = rasc_decl_sun(++clo->now);
			double tmp;

			clo->noon = noon_sun(clo, rd);
			tmp = cos_lha(clo, rd);
			clo->lha = acos(tmp);

			/* yield rise value next */
			clo->state = ST_RISE;
		}

		case ST_RISE:
			if (LIKELY(!isnan(clo->lha))) {
				double h = clo->noon - clo->lha;

				e.when = dh_to_instant(clo->now, h);
				e.what = ITS(SUNRISE);
				/* after rise comes noon */
				clo->state = ST_NOON;
				YIELD(e);
			}

		case ST_NOON:
			e.when = dh_to_instant(clo->now, clo->noon);
			e.what = ITS(NOON);
			/* after rise comes set */
			clo->state = ST_SET;
			YIELD(e);

		case ST_SET:
			if (LIKELY(!isnan(clo->lha))) {
				double h = clo->noon + clo->lha;

				e.when = dh_to_instant(clo->now, h);
				e.what = ITS(SUNSET);
				/* after rise comes recalc */
				clo->state = ST_UNK;
				YIELD(e);
			}
		}
	}
	/* cannot be reached */
}

echs_stream_t
make_echs_stream(echs_instant_t i, ...)
{
	static struct clo_s clo = {
		.lng = RAD(8.7667),
		.lat = RAD(50.8167),
	};
	clo.now = instant_to_days(i) - 1;
	return (echs_stream_t){__sun, &clo};
}

/* solar.c ends here */
