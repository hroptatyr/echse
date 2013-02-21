/*** gq.h -- generic queues, or pools of data elements
 *
 * Copyright (C) 2012-2013 Sebastian Freundt
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
#if !defined INCLUDED_gq_h_
#define INCLUDED_gq_h_

#include <stdint.h>
#include <stddef.h>

/* generic queues */
typedef struct gq_s *gq_t;
typedef struct gq_ll_s *gq_ll_t;
typedef struct gq_item_s *gq_item_t;

struct gq_item_s {
	gq_item_t next;

	char data[];
};

struct gq_ll_s {
	gq_item_t i1st;
	gq_item_t ilst;
};

struct gq_s {
	unsigned int nitems;
	unsigned int itemz;

	/* we keep all the ready-to-go elements here */
	struct gq_ll_s free[1];
	/* we chain up all book keeper elements here */
	struct gq_ll_s book[1];
};

#define GQ_NULL_ITEM	((gq_item_t)(NULL))

extern void init_gq(gq_t, size_t nnew_members, size_t mbsz);
extern void fini_gq(gq_t);

extern gq_item_t gq_pop_head(gq_ll_t);
extern void gq_push_tail(gq_ll_t, gq_item_t);
extern void gq_push_head(gq_ll_t, gq_item_t);

#endif	/* INCLUDED_gq_h_ */
