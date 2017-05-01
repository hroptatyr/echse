/*** scale.h -- calendar scales
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
#if !defined INCLUDED_scale_h_
#define INCLUDED_scale_h_
#include <stdint.h>
#include "instant.h"

/**
 * All the calendars we support. */
typedef enum {
	SCALE_GREGORIAN = 0,
	SCALE_HIJRI_UMMULQURA = 1,
} echs_scale_t;

/**
 * echs_instants are 64bit values with some bits unused to facilitate
 * aligned access, we now put the bits of echs_scale_t onto echs_instant
 * rendering it useless for intercalary comparisons */
#define ECHS_SMASK	0xf0000000U

static inline __attribute__((const, pure)) uint32_t
scale_mux(echs_scale_t s)
{
	return (s & 0xfU) << 28U;
}

static inline __attribute__((const, pure)) echs_scale_t
scale_demux(uint32_t x)
{
	return (echs_scale_t)((x >> 28U) & 0xfU);
}


/**
 * Convert instant I to calendar scale S. */
extern echs_instant_t echs_instant_rescale(echs_instant_t i, echs_scale_t s);

/**
 * Return the number of days in month M in year Y according to scale S. */
extern __attribute__((pure, const)) unsigned int
echs_scale_ndim(echs_scale_t s, unsigned int y, unsigned int m);

/**
 * Extract scale information from instant I. */
static inline __attribute__((const, pure)) echs_scale_t
echs_instant_scale(echs_instant_t i)
{
	return scale_demux(i.dpart & ECHS_SMASK);
}

/**
 * Detach SCALE information from instant I. */
static inline __attribute__((pure, const)) echs_instant_t
echs_instant_detach_scale(echs_instant_t i)
{
	i.dpart &= ~ECHS_SMASK;
	return i;
}

/**
 * Attach SCALE information to instant I. */
static inline __attribute__((pure, const)) echs_instant_t
echs_instant_attach_scale(echs_instant_t i, echs_scale_t s)
{
	/* rinse I first */
	i = echs_instant_detach_scale(i);
	i.dpart ^= scale_mux(s);
	return i;
}

/**
 * Return the number of days in month and year using the scale from I. */
static inline __attribute__((const, pure)) unsigned int
echs_instant_ndim(echs_instant_t i)
{
	const echs_scale_t sca = echs_instant_scale(i);
	const echs_instant_t str = echs_instant_detach_scale(i);
	return echs_scale_ndim(sca, str.y, str.m);
}

#endif	/* INCLUDED_scale_h_ */
