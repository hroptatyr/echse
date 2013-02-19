/*** strdef.c -- stream modules, files, sockets, etc.
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
#include "strdef.h"
#include "module.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */


echs_strdef_t
echs_open(echs_instant_t i, const char *strdef)
{
	typedef echs_stream_t(*make_stream_f)(echs_instant_t, ...);
	struct echs_strdef_s res;
	struct stat st;

	/* try the module thing first */
	if ((res.m = echs_mod_open(strdef)) != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(res.m, "make_echs_stream")) == NULL) {
			/* nope, unsuitable */
			echs_mod_close(res.m);
			goto fuckup;
		} else if ((res.s = ((make_stream_f)f)(i)).f == NULL) {
			/* no stream returned */
			echs_close(res);
			goto fuckup;
		}
	} else if (stat(strdef, &st) >= 0 &&
		   S_ISREG(st.st_mode) &&
		   !(st.st_mode & S_IXUSR) &&
		   (res.m = echs_mod_open("echs-file")) != NULL) {
		/* could be a genuine echs file, we use the echs-file DSO
		 * so we don't have to worry about closures and shit */
		echs_mod_f f;

		if ((f = echs_mod_sym(res.m, "make_echs_stream")) == NULL) {
			/* nope, unsuitable */
			echs_mod_close(res.m);
			goto fuckup;
		} else if ((res.s = ((make_stream_f)f)(i, strdef)).f == NULL) {
			/* no stream returned */
			echs_close(res);
			goto fuckup;
		}
	} else {
	fuckup:
		/* super fuckup */
		return (echs_strdef_t){.m = NULL};
	}
	return res;
}

void
echs_close(echs_strdef_t sd)
{
	typedef void(*free_stream_f)(echs_stream_t);

	if (sd.m != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(sd.m, "free_echs_stream")) != NULL) {
			/* call the finaliser */
			((free_stream_f)f)(sd.s.clo);
		}

		/* and (or otherwise) close the module */
		echs_mod_close(sd.m);
	}
	return;
}

/* strdef.c ends here */
