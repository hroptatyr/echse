/*** nummapstr.h -- numerically mapped strings
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
#if !defined INCLUDED_nummapstr_h_
#define INCLUDED_nummapstr_h_
#include <stdint.h>
#include <stdbool.h>

typedef uintptr_t nummapstr_t;
#define NUMMAPSTR_NAN	((nummapstr_t)-1)
#define NUMMAPSTR_WID	(sizeof(nummapstr_t) * 8U)
#define NUMMAPSTR_MSK	((nummapstr_t)1 << (NUMMAPSTR_WID - 1U))
#define NUMMAPSTR_MIN	((nummapstr_t)0)
#define NUMMAPSTR_MAX	((nummapstr_t)INTPTRMAX)


static inline char*
nummapstr_str(nummapstr_t x)
{
/* Return the string in X as char*, or NULL if X is a number. */
	return x & NUMMAPSTR_MSK ? NULL : (void*)x;
}

static inline uintptr_t
nummapstr_num(nummapstr_t x)
{
/* Return the number in X as uintptr_t, or NUMMAPSTR_NAN if X is a string. */
	return x & NUMMAPSTR_MSK ? x ^ NUMMAPSTR_MSK : NUMMAPSTR_NAN;
}

static inline nummapstr_t
nummapstr_bang_num(uintptr_t n)
{
/* Return N as nummapstr_t */
	return n | NUMMAPSTR_MSK;
}

static inline nummapstr_t
nummapstr_bang_str(const char *s)
{
/* Return S as nummapstr_t */
	return (uintptr_t)s;
}

#endif	/* INCLUDED_nummapstr_h_ */
