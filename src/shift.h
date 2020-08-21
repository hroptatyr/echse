/*** shift.h -- stream shifts
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
#if !defined INCLUDED_shift_h_
#define INCLUDED_shift_h_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "instant.h"

/* SHIFTs should be SHIFT=x[,yB[+-]]
 * meaning shift first by x days (positive or negative),
 * then y business days
 * we'll use the high 16 bits for day movement,
 * the lowest 2 bits for the sign of business day shift and invertedness
 * the middle 14 bits for the value of business day shift, like so
 *
 * dddd dddd dddd dddd bbbb bbbb bbbb bbis
 *                                      |^
 *                                      |sign
 *                                      inverted
 */

typedef int echs_shift_t;

/**
 * Shift instant X by SH. */
extern echs_instant_t
echs_instant_shift(echs_instant_t x, echs_shift_t sh);


static inline bool
echs_shift_bday_p(echs_shift_t sh)
{
	return !!(sh & 0xffff);
}

static inline bool
echs_shift_neg_p(echs_shift_t sh)
{
/* LSB indicates sign */
	return !!(sh & 0b1U);
}

static inline bool
echs_shift_inv_p(echs_shift_t sh)
{
/* 3rd-LSB indicates shift semantics */
	return !!(sh & 0b10U);
}

static inline int
echs_shift_bvalue(echs_shift_t sh)
{
	int x = (sh & 0xffff) >> 2U;
	return !(sh & 0b1U) ? x : -x;
}

static inline __attribute__((pure)) int
echs_shift_dvalue(echs_shift_t sh)
{
	return sh >> 16U;
}

static inline unsigned int
echs_shift_absval(echs_shift_t sh)
{
	return (sh & 0xffff) >> 2U;
}

#endif	/* INCLUDED_shift_h_ */
