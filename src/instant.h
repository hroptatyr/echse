/*** instant.h -- some echs_instant_t functionality
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
#if !defined INCLUDED_instant_h_
#define INCLUDED_instant_h_

#include <stdbool.h>
#include "echse.h"
#include "boobs.h"

static inline __attribute__((pure)) uint64_t
__inst_u64(echs_instant_t x)
{
	return be64toh(x.u);
}

static inline __attribute__((pure)) echs_instant_t
__u64_inst(uint64_t x)
{
	return (echs_instant_t){.u = htobe64(x)};
}

static inline __attribute__((pure)) bool
__inst_lt_p(echs_instant_t x, echs_instant_t y)
{
	uint64_t x64 = __inst_u64(x);
	uint64_t y64 = __inst_u64(y);
	return x64 < y64;
}

static inline __attribute__((pure)) bool
__inst_le_p(echs_instant_t x, echs_instant_t y)
{
	uint64_t x64 = __inst_u64(x);
	uint64_t y64 = __inst_u64(y);
	return x64 <= y64;
}

static inline __attribute__((pure)) bool
__inst_eq_p(echs_instant_t x, echs_instant_t y)
{
	uint64_t x64 = __inst_u64(x);
	uint64_t y64 = __inst_u64(y);
	return x64 == y64;
}

#endif	/* INCLUDED_instant_h_ */
