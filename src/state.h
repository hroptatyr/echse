/*** state.h -- state interning system
 *
 * Copyright (C) 2013-2015 Sebastian Freundt
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
#if !defined INCLUDED_state_h_
#define INCLUDED_state_h_

#include <stdint.h>
#include <stdbool.h>

/**
 * States are enumerated handles (from 1 to 63). */
typedef uint_fast8_t echs_state_t;

/**
 * Stsets are bitsets of states.  The lowest bit stands for the absent state,
 * For instance if an event does not define a state. */
typedef uint_fast64_t echs_stset_t;

/**
 * Return the state number of STR. */
extern echs_state_t add_state(const char *str, size_t len);

/**
 * Return the state number of STR. */
extern echs_state_t get_state(const char *str, size_t len);

/**
 * Unintern STATE. */
extern void rem_state(echs_state_t);

/**
 * Return the string representation of STATE. */
extern const char *state_name(echs_state_t);

/**
 * Clean up resources used by the state interning system. */
extern void clear_states(void);


static inline __attribute__((const, pure)) echs_stset_t
stset_add_state(echs_stset_t set, echs_state_t st)
{
	return set |= (uint_fast64_t)(1ULL << st);
}

static inline __attribute__((const, pure)) bool
stset_has_state_p(echs_stset_t set, echs_state_t st)
{
	return (set >> st) & 0b1U;
}

#endif	/* INCLUDED_state_h_ */
