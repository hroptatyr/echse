/*** steve.h -- state events
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
#if !defined INCLUDED_steve_h_
#define INCLUDED_steve_h_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "echse.h"

/* define later */
typedef union stev_ti_u stev_ti_t;
typedef struct stev_ee_s stev_ee_t;
typedef struct stev_iv_s stev_iv_t;

/* instant in time object */
union stev_ti_u {
	struct {
		uint32_t y:16;
		uint32_t m:8;
		uint32_t d:8;
		uint32_t H:8;
		uint32_t M:8;
		uint32_t S:6;
		uint32_t ms:10;
	};
	struct {
		uint32_t dpart;
		uint32_t intra;
	};
	uint64_t u;
} __attribute__((transparent_union));

/* edge events */
struct stev_ee_s {
	stev_ti_t i;
};

/* intervals */
struct stev_iv_s {
	stev_ti_t b;
	stev_ti_t e;
};


static inline bool
stev_ti_all_day_p(stev_ti_t i)
{
#define STEV_ALL_DAY	(0xffffffffU)
	return i.intra == STEV_ALL_DAY;
}

static inline bool
stev_ti_all_sec_p(stev_ti_t i)
{
#define STEV_ALL_SEC	(0x3ffU)
	return i.ms == STEV_ALL_SEC;
}

static inline bool
stev_ti_abs_p(stev_ti_t i)
{
/* nothing needs to be stamped in the (non-existing) year 0, so
 * the remaining 48 bits of year 0 entries denote relative time specs. */
	return i.y;
}

#endif	/* INCLUDED_steve_h_ */
