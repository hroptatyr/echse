/*** instant.h -- some echs_instant_t functionality
 *
 * Copyright (C) 2013-2020 Sebastian Freundt
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
#if !defined INCLUDED_instant_h_
#define INCLUDED_instant_h_
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "boobs.h"

typedef struct echs_idiff_s echs_idiff_t;
typedef union echs_instant_u echs_instant_t;

union echs_instant_u {
	struct {
#if BYTE_ORDER == BIG_ENDIAN
		/* top 4 bits carry scale info */
		uint32_t y:16;
		/* top 4 bits carry tz info */
		uint32_t m:8;
		/* top 2 bits carry tz info */
		uint32_t d:8;
		uint32_t H:8;
		uint32_t M:8;
		uint32_t S:6;
		uint32_t ms:10;
#elif BYTE_ORDER == LITTLE_ENDIAN
		uint32_t ms:10;
		uint32_t S:6;
		uint32_t M:8;
		uint32_t H:8;
		uint32_t d:8;
		uint32_t m:8;
		uint32_t y:16;
#else
# warning endianness detection failed
#endif
	};
	struct {
#if BYTE_ORDER == BIG_ENDIAN
		uint32_t dpart;
		uint32_t intra;
#elif BYTE_ORDER == LITTLE_ENDIAN
		uint32_t intra;
		uint32_t dpart;
#endif
	};
	uint64_t u;
} __attribute__((transparent_union));

struct echs_idiff_s {
	int64_t d;
};


/**
 * Fix up instants like the 32 Dec to become 01 Jan of the following year. */
extern echs_instant_t echs_instant_fixup(echs_instant_t);

/**
 * Return the duration from BEG to END, i.e. compute END - BEG. */
extern echs_idiff_t echs_instant_diff(echs_instant_t end, echs_instant_t beg);

/**
 * Return the instant yielded by adding a duration ADD to the instant BAS. */
extern echs_instant_t echs_instant_add(echs_instant_t bas, echs_idiff_t add);

/**
 * Sort an array IN of NIN elements stable and in-place. */
extern void echs_instant_sort(echs_instant_t *restrict in, size_t nin);

/**
 * Convert echs_instant_t to epoch time. */
extern time_t echs_instant_to_epoch(echs_instant_t);

/**
 * Convert epoch time to echs_instant_t. */
extern echs_instant_t epoch_to_echs_instant(time_t);


#define ECHS_ALL_DAY	(0xffU)
#define ECHS_ALL_SEC	(0x3ffU)

static inline __attribute__((const, pure)) bool
echs_instant_all_day_p(echs_instant_t i)
{
	return i.H == ECHS_ALL_DAY;
}

static inline __attribute__((const, pure)) bool
echs_instant_all_sec_p(echs_instant_t i)
{
	return i.ms == ECHS_ALL_SEC;
}

static inline __attribute__((const, pure)) bool
echs_instant_0_p(echs_instant_t x)
{
	return x.u == 0U;
}

static inline __attribute__((const, pure)) bool
echs_instant_lt_p(echs_instant_t x, echs_instant_t y)
{
	x.H++, x.ms++;
	y.H++, y.ms++;
	return x.u < y.u;
}

static inline __attribute__((const, pure)) bool
echs_instant_le_p(echs_instant_t x, echs_instant_t y)
{
	x.H++, x.ms++;
	y.H++, y.ms++;
	return !(x.u > y.u);
}

static inline __attribute__((const, pure)) bool
echs_instant_eq_p(echs_instant_t x, echs_instant_t y)
{
	return x.u == y.u;
}

static inline __attribute__((const, pure)) echs_instant_t
echs_nul_instant(void)
{
	static const echs_instant_t nul = {.u = 0ULL};
	return nul;
}

static inline __attribute__((const, pure)) bool
echs_nul_instant_p(echs_instant_t x)
{
	return x.u == 0ULL;
}

static inline __attribute__((const, pure)) echs_instant_t
echs_min_instant(void)
{
	return echs_nul_instant();
}

static inline __attribute__((const, pure)) bool
echs_min_instant_p(echs_instant_t x)
{
	return echs_nul_instant_p(x);
}

static inline __attribute__((const, pure)) echs_instant_t
echs_max_instant(void)
{
	static const echs_instant_t i = {.u = -1ULL};
	return i;
}

static inline __attribute__((const, pure)) bool
echs_max_instant_p(echs_instant_t x)
{
	return x.u == -1ULL;
}


static inline __attribute__((const, pure)) echs_idiff_t
echs_nul_idiff(void)
{
	static const echs_idiff_t nul = {0U};
	return nul;
}

static inline __attribute__((const, pure)) bool
echs_nul_idiff_p(echs_idiff_t x)
{
	return x.d == 0;
}

static inline __attribute__((const, pure)) bool
echs_idiff_lt_p(echs_idiff_t i1, echs_idiff_t i2)
{
	return i1.d < i2.d;
}

static inline __attribute__((const, pure)) bool
echs_idiff_le_p(echs_idiff_t i1, echs_idiff_t i2)
{
	return i1.d <= i2.d;
}

static inline __attribute__((const, pure)) bool
echs_idiff_eq_p(echs_idiff_t i1, echs_idiff_t i2)
{
	return i1.d == i2.d;
}

static inline __attribute__((const, pure)) echs_idiff_t
echs_idiff_neg(echs_idiff_t i)
{
	return (echs_idiff_t){-i.d};
}

#endif	/* INCLUDED_instant_h_ */
