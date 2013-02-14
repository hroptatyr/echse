/*** echse.c -- testing echse concept
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
#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "echse.h"


/* christmas stream */
static echs_event_t
xmas_next(echs_instant_t i)
{
	static const struct echs_state_s on = {1, "XMAS"};
	static const struct echs_state_s off = {0, "XMAS"};
	static struct {
		struct echs_event_s e;
		echs_state_t st;
	} res;

	if (i.m < 12U || i.d < 25U) {
		res.e.when = (echs_instant_t){i.y, 12U, 25U},
		res.e.nwhat = 1U;
		res.st.s = &on;
	} else if (i.d < 26U) {
		res.e.when = (echs_instant_t){i.y, 12U, 26U};
		res.e.nwhat = 1U;
		res.st.s = &off;
	} else {
		res.e.when = (echs_instant_t){i.y + 1, 12U, 25U};
		res.e.nwhat = 1U;
		res.st.s = &on;
	}
	return &res.e;
}


int
main(void)
{
	echs_instant_t start = {2000, 1, 1};

	for (size_t i = 0; i < 4; i++) {
		const struct echs_event_s *nx = xmas_next(start);

		printf("nx %p\n", nx);
		printf("nx %04u-%02u-%02u: %c%s\n",
		       nx->when.y, nx->when.m, nx->when.d,
		       nx->what[0].s->w ? ' ' : '~', nx->what[0].s->d);
		start = nx->when;
	}
	return 0;
}

/* echse.c ends here */
