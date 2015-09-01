/*** tzob.h -- timezone interning system
 *
 * Copyright (C) 2014-2015 Sebastian Freundt
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
#if !defined INCLUDED_tzob_h_
#define INCLUDED_tzob_h_
#include <stdint.h>
#include "instant.h"

typedef uint_fast32_t echs_tzob_t;

/**
 * echs_instants are 64bit values with some bits unused to facilitate
 * aligned access, we now put the bits of tzob_t onto echs_instant
 * rendering it useless for ordinary echs_instant operations */
#define ECHS_DMASK	0xf000f0e0U
#define ECHS_IMASK	0xe0c00000U

/**
 * Return interned representaton of zone ZN (of length NZN). */
extern echs_tzob_t echs_tzob(const char *zn, size_t nzn);

/**
 * Clear all interned tz objects and free associated resources. */
extern void clear_tzobs(void);


/**
 * Extract tz information from instant I. */
static inline __attribute__((pure, const)) echs_tzob_t
echs_instant_tzob(echs_instant_t i)
{
	echs_tzob_t r = 0U;

	r |= i.dpart & ECHS_DMASK;
	r |= i.intra & ECHS_IMASK;
	return r;
}

/**
 * Detach TZ information from instant I. */
static inline __attribute__((pure, const)) echs_instant_t
echs_instant_detach_tzob(echs_instant_t i)
{
	i.dpart &= ~ECHS_DMASK;
	i.intra &= ~ECHS_IMASK;
	return i;
}

/**
 * Attach TZ information to instant I. */
static inline __attribute__((pure, const)) echs_instant_t
echs_instant_attach_tzob(echs_instant_t i, echs_tzob_t tz)
{
	/* rinse I first */
	i = echs_instant_detach_tzob(i);
	i.dpart ^= (tz & ECHS_DMASK);
	i.intra ^= (tz & ECHS_IMASK);
	return i;
}

#endif	/* INCLUDED_tzob_h_ */
