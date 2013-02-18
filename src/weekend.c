/*** weekend.c -- sat+sun stream
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
#include "instant.h"

#if !defined countof
# define countof(x)		(sizeof(x) / sizeof(*x))
#endif	/* !countof */

static unsigned int
__get_wday(echs_instant_t i)
{
/* sakamoto method, stolen from dateutils */
	static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
	int year = i.y;
	int res;

	year -= i.m < 3;
	res = year + year / 4U - year / 100U + year / 400U;
	res += t[i.m - 1U] + i.d;
	return (unsigned int)res % 7U;
}


/* new-year stream */
echs_event_t
echs_stream(echs_instant_t i)
{
	DEFSTATE(SAT);
	DEFSTATE(SUN);
	struct echs_event_s e;
	unsigned int wd;

	switch ((wd = __get_wday(i))) {
		static const echs_state_t s[] = {
			ON(SAT), OFF(SAT), ON(SUN), OFF(SUN)
		};
		static const echs_state_t *sp;
	case 0/*S*/:
		if (sp == s + 1) {
			e.when = i;
			e.what = *(sp = s + 2);
			break;
		}
	case 1/*M*/:
		if (sp == s + 2) {
			e.when = (echs_instant_t){i.y, i.m, i.d + 1U};
			e.what = *(sp = s + 3);
			break;
		}
	case 2/*T*/:
	case 3/*W*/:
	case 4/*R*/:
	case 5/*F*/:
		/* point to next sat */
		e.when = (echs_instant_t){i.y, i.m, i.d + (6U - wd)};
		e.what = *(sp = s);
		break;
	case 6/*A*/:
		e.when = (echs_instant_t){i.y, i.m, i.d + 1U};
		e.what = *(sp = s + 1);
		break;
	default:
		abort();
	}
	e.when = echs_instant_fixup(e.when);
	return e;
}

/* weekend.c ends here */
