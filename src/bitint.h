/*** bitint.h -- integer degrading bitsets
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
#if !defined INCLUDED_bitint_h_
#define INCLUDED_bitint_h_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* the next types can take ordinary integers, or, if more than 1 is
 * tried to be assigned they degrade to bitsets */
typedef uint32_t bituint31_t;
typedef uint64_t bituint63_t;

typedef struct {
	uint32_t pos;
	int32_t neg;
} bitint31_t;

typedef struct {
	uint64_t pos;
	int64_t neg;
} bitint63_t;

typedef size_t bitint_iter_t;

/* a very specific one */
typedef struct {
	uint32_t pos[12U];
	int32_t neg[12U];
} bitint383_t;

typedef struct {
	uint32_t pos[14U];
	int32_t neg[14U];
} bitint447_t;

/**
 * Assign X to bitset/integer BI. */
extern void ass_bi383(bitint383_t *restrict bi, int x);

/**
 * Iterate over integers in BI. */
extern int bi383_next(bitint_iter_t *restrict iter, const bitint383_t *bi);

/**
 * Return max(BI, 0). */
extern int bi383_max0(const bitint383_t *bi);

/**
 * Assign X to bitset/integer BI. */
extern void ass_bi447(bitint447_t *restrict bi, int x);

/**
 * Iterate over integers in BI. */
extern int bi447_next(bitint_iter_t *restrict iter, const bitint447_t *bi);


/**
 * Assign X to bitset/integer BI. */
static inline bituint31_t
ass_bui31(bituint31_t bi, unsigned int x)
{
/* LSB set means we carry only an integer
 * bi == 0 means we carry nothing */
	if (bi == 0U) {
		return (x << 1U) | 1U;
	} else if (bi & 0b1U) {
		/* degrade */
		bi >>= 1U;
		bi++;
		bi = 1U << bi;
	}
	bi |= 1U << (x + 1U);
	return bi;
}

/**
 * Assign X to bitset/integer BI. */
static inline bitint31_t
ass_bi31(bitint31_t bi, int x)
{
/* LSB set in pos: one integer (in neg)
 * nothing set -> nothing set
 * otherwise bitset */
	if (bi.pos == 0U && bi.neg == 0) {
		bi.pos |= 0b1U;
		bi.neg = x;
		return bi;
	} else if (bi.pos & 0b1U) {
		/* degrade */
		if (bi.neg > 0) {
			bi.pos = 1U << (unsigned int)bi.neg;
			bi.neg = 0;
		} else {
			bi.pos = 0U;
			bi.neg = 1U << (unsigned int)(-bi.neg);
		}
	}
	if (x > 0) {
		bi.pos |= 1U << (unsigned int)x;
	} else {
		bi.neg |= 1U << (unsigned int)(-x);
	}
	return bi;
}

static inline bool
bui31_has_bits_p(bituint31_t bi)
{
	return bi != 0U;
}

static inline bool
bi31_has_bits_p(bitint31_t bi)
{
	return bi.pos != 0U || bi.neg != 0;
}

/**
 * Check that bit X is assignd in BI. */
static inline bool
bui31_has_bit_p(bituint31_t bi, unsigned int x)
{
/* LSB set means we carry only an integer
 * bi == 0 means we carry nothing */
	if (bi & 0b1U) {
		return (bi >> 1U) == x;
	}
	return (bi >> (x + 1U)) & 0b1U;
}

/**
 * Assign X to bitset/integer BI. */
static inline bituint63_t
ass_bui63(bituint63_t bi, unsigned int x)
{
/* LSB set means we carry only an integer
 * bi == 0 means we carry nothing */
	if (bi == 0U) {
		return (x << 1U) | 1U;
	} else if (bi & 0b1U) {
		/* degrade */
		bi >>= 1U;
		bi++;
		bi = 1ULL << bi;
	}
	bi |= 1ULL << (x + 1U);
	return bi;
}

/**
 * Check that bit X is assignd in BI. */
static inline bool
bi31_has_bit_p(bitint31_t bi, int x)
{
/* LSB set in pos: one integer (in neg)
 * nothing set -> nothing set
 * otherwise bitset */
	if (bi.pos & 0b1U) {
		return bi.neg == x;
	} else if (x > 0) {
		return (bi.pos >> (unsigned int)x) & 0b1U;
	}
	return (bi.neg >> (unsigned int)-x) & 0b1U;
}

/**
 * Assign X to bitset/integer BI. */
static inline bitint63_t
ass_bi63(bitint63_t bi, int x)
{
/* LSB set in pos: one integer (in neg)
 * nothing set -> nothing set
 * otherwise bitset */
	if (bi.pos == 0U && bi.neg == 0) {
		bi.pos |= 0b1U;
		bi.neg = x;
		return bi;
	} else if (bi.pos & 0b1U) {
		/* degrade */
		if (bi.neg > 0) {
			bi.pos = 1ULL << (unsigned int)bi.neg;
			bi.neg = 0LL;
		} else {
			bi.pos = 0ULL;
			bi.neg = 1ULL << (unsigned int)(-bi.neg);
		}
	}
	if (x > 0) {
		bi.pos |= 1ULL << (unsigned int)x;
	} else {
		bi.neg |= 1ULL << (unsigned int)(-x);
	}
	return bi;
}

static inline bool
bui63_has_bits_p(bituint63_t bi)
{
	return bi != 0ULL;
}

static inline bool
bi63_has_bits_p(bitint63_t bi)
{
	return bi.pos != 0ULL || bi.neg != 0LL;
}

static inline bool
bi383_has_bits_p(const bitint383_t bi[static 1U])
{
	return *bi->pos != 0U;
}

static inline bool
bi447_has_bits_p(const bitint447_t bi[static 1U])
{
	return *bi->pos != 0U;
}

static inline unsigned int
bui31_next(bitint_iter_t *restrict iter, bituint31_t bi)
{
	unsigned int res;

	if (bi & 0b1U) {
		if (*iter) {
			goto term;
		}
		res = bi >> 1U;
		*iter = res;
	} else if (bi >>= 1U, bi >>= *iter) {
		for (; !(bi & 0b1U); (*iter)++, bi >>= 1U);
		res = (*iter)++;
	} else {
	term:
		*iter = 0U;
		return 0;
	}
	return res;
}

static inline int
bi31_next(bitint_iter_t *restrict iter, bitint31_t bi)
{
	int res;

	if (bi.pos & 0b1U) {
		if (*iter) {
			goto term;
		}
		res = bi.neg;
		*iter = 1U;
	} else if (!*iter && bi.neg & 0b1U) {
		/* get the naught out first */
		res = 0U;
		*iter = 1U;
	} else if (*iter < 32U && (bi.pos >>= *iter)) {
		/* we're still doing positives */
		for (; !(bi.pos & 0b1U); (*iter)++, bi.pos >>= 1U);
		res = (*iter);
		if (bi.pos > 1U) {
			(*iter)++;
		} else {
			/* switch to negatives */
			*iter = 33U;
		}
	} else if (*iter > 32 && *iter < 64 && (bi.neg >>= (*iter - 32U))) {
		/* we're doing negatives alright */
		for (; !(bi.neg & 0b1U); (*iter)++, bi.neg >>= 1U);
		res = 32 - (*iter)++;
	} else {
	term:
		*iter = 0U;
		return 0;
	}
	return res;
}

static inline unsigned int
bui63_next(bitint_iter_t *restrict iter, bituint63_t bi)
{
	unsigned int res;

	if (bi & 0b1U) {
		if (*iter) {
			goto term;
		}
		res = bi >> 1U;
		*iter = res;
	} else if (bi >>= 1U, bi >>= *iter) {
		for (; !(bi & 0b1U); (*iter)++, bi >>= 1U);
		res = (*iter)++;
	} else {
	term:
		*iter = 0U;
		return 0;
	}
	return res;
}

static inline int
bi63_next(bitint_iter_t *restrict iter, bitint63_t bi)
{
	int res;

	if (bi.pos & 0b1U) {
		if (*iter) {
			goto term;
		}
		res = bi.neg;
		*iter = 1U;
	} else if (!*iter && bi.neg & 0b1U) {
		/* get the naught out first */
		res = 0U;
		*iter = 1U;
	} else if (*iter < 64U && (bi.pos >>= *iter)) {
		/* we're still doing positives */
		for (; !(bi.pos & 0b1U); (*iter)++, bi.pos >>= 1U);
		res = (*iter);
		if (bi.pos > 1U) {
			(*iter)++;
		} else {
			/* switch to negatives */
			*iter = 65U;
		}
	} else if (*iter > 64 && *iter < 128 && (bi.neg >>= (*iter - 64U))) {
		/* we're doing negatives alright */
		for (; !(bi.neg & 0b1U); (*iter)++, bi.neg >>= 1U);
		res = 64 - (*iter)++;
	} else {
	term:
		*iter = 0U;
		return 0;
	}
	return res;
}

#endif	/* INCLUDED_bitint_h_ */
