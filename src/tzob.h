/*** tzob.h -- timezone interning system
 *
 * Copyright (C) 2014-2020 Sebastian Freundt
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
#define ECHS_DMASK	0x0000f0c0U

/**
 * Return interned representaton of zone ZN (of length NZN). */
extern echs_tzob_t echs_tzob(const char *zn, size_t nzn);

/**
 * Return (the interned) zone name from an tzob_t object Z. */
extern const char *echs_zone(echs_tzob_t z);

/**
 * Clear all interned tz objects and free associated resources. */
extern void clear_tzobs(void);

/**
 * Convert instant I to UTC time from zone Z. */
extern echs_instant_t echs_instant_utc(echs_instant_t i, echs_tzob_t z);

/**
 * Convert instant I (in UTC) to local time in zone Z. */
extern echs_instant_t echs_instant_loc(echs_instant_t i, echs_tzob_t z);

/**
 * Return the curent UTC-offset of Z (in seconds) at instant I (+X seconds). */
extern int echs_tzob_offs(echs_tzob_t z, echs_instant_t i, int x);


/**
 * Extract tz information from instant I. */
static inline __attribute__((pure, const)) echs_tzob_t
echs_instant_tzob(echs_instant_t i)
{
	echs_tzob_t r = i.dpart & ECHS_DMASK;
	return r;
}

/**
 * Detach TZ information from instant I. */
static inline __attribute__((pure, const)) echs_instant_t
echs_instant_detach_tzob(echs_instant_t i)
{
	i.dpart &= ~ECHS_DMASK;
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
	return i;
}

/**
 * Just convenience when UTC-offset of Z to UTC at instant I is enough. */
static inline int
echs_instant_tzof(echs_instant_t i, echs_tzob_t z)
{
	return echs_tzob_offs(z, i, 0);
}

#endif	/* INCLUDED_tzob_h_ */
