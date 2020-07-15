/*** evmrul.h -- mover rules
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
#if !defined INCLUDED_evmrul_h_
#define INCLUDED_evmrul_h_

#include <stddef.h>
#include <stdbool.h>
#include "state.h"
#include "evstrm.h"

typedef struct mrulsp_s mrulsp_t;

typedef enum {
	MDIR_NONE,
	MDIR_PAST,
	MDIR_PASTTHENFUTURE,
	MDIR_FUTURE,
	MDIR_FUTURETHENPAST,
} echs_mdir_t;

struct mrulsp_s {
	echs_mdir_t mdir;
	/* set of states we allow the event to be moved into */
	echs_stset_t into;
	/* set of states we need to move away from */
	echs_stset_t from;
};


/**
 * Turn a stream with movable events into a fixed date/time stream. */
extern echs_evstrm_t
make_evmrul(const mrulsp_t*, echs_evstrm_t mov, echs_evstrm_t aux);

/* serialiser */
extern void mrulsp_icalify(int whither, const mrulsp_t*);

#endif	/* INCLUDED_evmrul_h_ */
