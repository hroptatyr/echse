/*** celest.h -- goodies for rise, transit, set computations
 *
 * Copyright (C) 2020 Sebastian Freundt
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
#if !defined INCLUDED_celest_h_
#define INCLUDED_celest_h_

#include <stdbool.h>
#include <math.h>
#include "echse.h"

#define pi		3.14159265358979323846
#define RAD(x)		((x) * pi / 180.0)
#define DEG(x)		((x) * 180.0 / pi)

#define NEVER_RISE	NAN
#define NEVER_SET	NAN

/* days since epoch (being 2000-Jan-00) */
typedef int cel_d_t;
/* hours (as fraction of day) since midnight */
typedef double cel_h_t;
/* hours (as fraction of 2pi) since midnight */
typedef double cel_a_t;

typedef struct cel_rts_s cel_rts_t;
typedef struct cel_pos_s cel_pos_t;
typedef const struct cel_obj_s *cel_obj_t;

struct cel_rts_s {
	cel_h_t rise;
	cel_h_t transit;
	cel_h_t set;
};

struct cel_pos_s {
	double lng;
	double lat;
	double alt;
};

struct cel_calcopt_s {
	/* maximum number of iterations, 4 if not set */
	size_t max_iter;
	/* precision, DBL_EPSILON if not set */
	double prec;
};


extern cel_obj_t sun;
extern cel_obj_t moon;

/**
 * Compute rise, transit and set of OBJ on day D as observed at position P. */
extern cel_rts_t
cel_rts(cel_obj_t obj, cel_d_t d, cel_pos_t p, struct cel_calcopt_s);

/**
 * Return the corresponding instant from a cel day D and an hour instant H. */
extern echs_instant_t dh_to_instant(cel_d_t d, cel_h_t h);

/**
 * Convert an instant I to a cel day. */
extern cel_d_t instant_to_d(echs_instant_t i);


static inline bool
cel_h_valid_p(cel_h_t x)
{
	return !isnan(x);
}

#endif	/* INCLUDED_celest_h_ */
