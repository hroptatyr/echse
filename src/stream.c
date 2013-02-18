/*** stream.c -- stream modules, files, sockets, etc.
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
#include "stream.h"
#include "module.h"

#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif	/* !LIKELY */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif	/* UNLIKELY */

struct echs_strdef_s {
	echs_mod_t m;
	echs_stream_f f;
};

static echs_strdef_t
__strdef_dup(echs_strdef_t x)
{
	struct echs_strdef_s *res = malloc(sizeof(*x));
	*res = *x;
	return res;
}

static struct echs_strdef_s*
__close(const struct echs_strdef_s sd[static 1])
{
	if (sd->m != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(sd->m, "fini_stream")) != NULL) {
			/* call the finaliser */
			((int(*)(void))f)();
		}

		/* and (or otherwise) close the module */
		echs_mod_close(sd->m);
	}
	/* de-const */
	{
		union {
			const void *c;
			void *p;
		} x = {sd};
		return x.p;
	}
}


echs_strdef_t
echs_open(const char *strdef)
{
	struct echs_strdef_s res;
	struct stat st;

	/* try the module thing first */
	if ((res.m = echs_mod_open(strdef)) != NULL) {
		echs_mod_f f;

		if ((f = echs_mod_sym(res.m, "echs_stream")) == NULL) {
			/* nope, unsuitable */
			echs_mod_close(res.m);
			return NULL;
		}
		/* otherwise keep a not about this guy */
		res.f = (echs_stream_f)f;

		/* try and find the initialiser */
		if ((f = echs_mod_sym(res.m, "init_stream")) != NULL &&
		    ((int(*)(void))f)() < 0) {
			/* nope, fuck it */
			__close(&res);
			return NULL;
		}
	} else if (stat(strdef, &st) >= 0 &&
		   S_ISREG(st.st_mode) &&
		   !(st.st_mode & S_IXUSR) &&
		   (res.m = echs_mod_open("echs-file")) != NULL) {
		/* could be a genuine echs file, we use the echs-file DSO
		 * so we don't have to worry about closures and shit */
		echs_mod_f f;

		if ((f = echs_mod_sym(res.m, "init_file")) != NULL &&
		    ((int(*)(const char*))f)(strdef) ||
		    (f = echs_mod_sym(res.m, "echs_stream")) == NULL) {
			/* grrrrr */
			__close(&res);
			return NULL;
		}

		/* otherwise keep a not about this guy */
		res.f = (echs_stream_f)f;
	} else {
		/* super fuckup */
		return NULL;
	}
	return __strdef_dup(&res);
}

void
echs_close(echs_strdef_t sd)
{
	if (LIKELY(sd != NULL)) {
		struct echs_strdef_s *tmp = __close(sd);
		memset(tmp, 0, sizeof(*tmp));
		free(tmp);
	}
	return;
}

echs_stream_f
echs_get_stream(echs_strdef_t sd)
{
	return sd->f;
}

/* stream.c ends here */
