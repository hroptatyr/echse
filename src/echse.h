/*** echse.h -- testing echse concept
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
#if !defined INCLUDED_echse_h_
#define INCLUDED_echse_h_

#include <stdlib.h>
#include <stdint.h>

#define DEFSTATE(x)	static const char state__ ## x [] = "~" # x
#define ON(x)		(state__ ## x + 1U)
#define OFF(x)		(state__ ## x + 0U)


typedef union echs_instant_u echs_instant_t;
typedef const char *echs_state_t;
typedef struct echs_event_s echs_event_t;

union echs_instant_u {
	struct {
		uint32_t y:16;
		uint32_t m:8;
		uint32_t d:8;
		uint32_t H:8;
		uint32_t M:8;
		uint32_t S:6;
		uint32_t ms:10;
	};
	uint64_t u;
} __attribute__((transparent_union));

struct echs_event_s {
	echs_instant_t when;
	echs_state_t what;
};


/* type and prototype for streams */
typedef echs_event_t(*echs_stream_f)(echs_instant_t);
extern echs_event_t echs_stream(echs_instant_t);

#endif	/* INCLUDED_echse_h_ */
