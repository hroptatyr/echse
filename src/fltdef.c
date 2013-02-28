/*** fltdef.c -- filter modules, files, sockets, etc.
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
#include <string.h>
#include <sys/stat.h>

#include "echse.h"
#include "fltdef.h"
#include "module.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */


echs_fltdef_t
echs_open_fltdef(echs_instant_t i, const char *fltdef)
{
	typedef echs_filter_t(*make_filter_f)(echs_instant_t, ...);
	struct echs_fltdef_s res;

	/* try the module thing first */
	if ((res.m = echs_mod_open(fltdef)) != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(res.m, "make_echs_filter")) == NULL) {
			/* nope, unsuitable */
			echs_mod_close(res.m);
			goto fuckup;
		} else if ((res.f = ((make_filter_f)f)(i)).f == NULL) {
			/* no filters returned */
			echs_close_fltdef(res);
			goto fuckup;
		}
	} else {
	fuckup:
		/* super fuckup */
		return (echs_fltdef_t){.m = NULL};
	}
	return res;
}

void
echs_close_fltdef(echs_fltdef_t sd)
{
	typedef void(*free_filter_f)(echs_filter_t);

	if (sd.m != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(sd.m, "free_echs_filter")) != NULL) {
			/* call the finaliser */
			((free_filter_f)f)(sd.f);
		}

		/* and (or otherwise) close the module */
		echs_mod_close(sd.m);
	}
	return;
}

/* fltdef.c ends here */
