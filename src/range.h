/*** range.h -- ranges over instants or idiffs
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
#if !defined INCLUDED_range_h_
#define INCLUDED_range_h_
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "instant.h"

typedef struct {
	echs_instant_t beg;
	echs_instant_t end;
} echs_range_t;

typedef struct {
	echs_idiff_t lower;
	echs_idiff_t upper;
} echs_idrng_t;


static inline __attribute__((const, pure)) echs_idiff_t
echs_range_dur(echs_range_t r)
{
	return echs_instant_diff(r.end, r.beg);
}

static inline __attribute__((const, pure)) echs_range_t
echs_nul_range(void)
{
	return (echs_range_t){echs_nul_instant(), echs_nul_instant()};
}

static inline __attribute__((const, pure)) bool
echs_nul_range_p(echs_range_t r)
{
	return echs_nul_instant_p(r.end);
}

static inline __attribute__((const, pure)) echs_range_t
echs_max_range(void)
{
	return (echs_range_t){echs_min_instant(), echs_max_instant()};
}

static inline __attribute__((const, pure)) bool
echs_max_range_p(echs_range_t r)
{
	return echs_min_instant_p(r.beg) && echs_max_instant_p(r.end);
}

static inline __attribute__((const, pure)) echs_range_t
echs_min_range(void)
{
	return (echs_range_t){echs_max_instant(), echs_min_instant()};
}

static inline __attribute__((const, pure)) bool
echs_min_range_p(echs_range_t r)
{
	return echs_instant_le_p(r.end, r.beg);
}

/* 2-ary relations */
/**
 * Return true iff A precedes B, according to Allen. */
static inline __attribute__((const, pure)) bool
echs_range_precedes_p(echs_range_t a, echs_range_t b)
{
	return echs_instant_lt_p(a.end, b.beg);
}

/**
 * Return true iff A meets B, according to Allen. */
static inline __attribute__((const, pure)) bool
echs_range_meets_p(echs_range_t a, echs_range_t b)
{
	return echs_instant_eq_p(a.end, b.beg);
}

/**
 * Return true iff A overlaps B, according to Allen. */
static inline __attribute__((const, pure)) bool
echs_range_overlaps_p(echs_range_t a, echs_range_t b)
{
	return echs_instant_lt_p(a.beg, b.end) &&
		echs_instant_lt_p(b.beg, a.end);
}

/**
 * Return true iff A is finished by B, according to Allen. */
static inline __attribute__((const, pure)) bool
echs_range_finishes_p(echs_range_t a, echs_range_t b)
{
	return echs_instant_eq_p(a.end, b.end);
}

/**
 * Return true iff A contains B, according to Allen. */
static inline __attribute__((const, pure)) bool
echs_range_contains_p(echs_range_t a, echs_range_t b)
{
	return echs_instant_lt_p(a.beg, b.beg) &&
		echs_instant_lt_p(b.end, a.end);
}

/**
 * Return true iff A starts B, according to Allen. */
static inline __attribute__((const, pure)) bool
echs_range_starts_p(echs_range_t a, echs_range_t b)
{
	return echs_instant_eq_p(a.beg, b.beg);
}

/**
 * Return true iff A equals B, according to Allen. */
static inline __attribute__((const, pure)) bool
echs_range_equals_p(echs_range_t a, echs_range_t b)
{
	return echs_instant_eq_p(a.beg, b.beg) &&
		echs_instant_eq_p(a.end, b.end);
}

#endif	/* INCLUDED_range_h_ */
