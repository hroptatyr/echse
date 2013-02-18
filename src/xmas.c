/*** xmas.c -- christmas stream
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
#include "echse.h"

#if !defined countof
# define countof(x)		(sizeof(x) / sizeof(*x))
#endif	/* !countof */


/* christmas stream */
echs_event_t
echs_stream(echs_instant_t i)
{
	DEFSTATE(XMAS);
	DEFSTATE(BOXD);
	struct echs_event_s e;

	if (i.m < 12U || i.d < 25U) {
		e.when = (echs_instant_t){i.y, 12U, 25U};
		e.what = ON(XMAS);
	} else if (i.d <= 26U) {
		static const echs_state_t s[] = {OFF(XMAS), ON(BOXD)};
		static const echs_state_t *sp = s;

		if (sp < s + countof(s)) {
			e.when = (echs_instant_t){i.y, 12U, 26U};
			e.what = *sp++;
		} else {
			/* reset state counter */
			sp = s;
			e.when = (echs_instant_t){i.y, 12U, 27U};
			e.what = OFF(BOXD);
		}
	} else {
		e.when = (echs_instant_t){i.y + 1, 12U, 25U};
		e.what = ON(XMAS);
	}
	return e;
}

/* xmas.c ends here */
