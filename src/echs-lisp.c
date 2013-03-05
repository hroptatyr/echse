/*** echs-lisp.c -- bindings for echs-lisp scripts
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
#define HAVE_INTTYPES_H	1
#define HAVE_STDINT_H	1
#include <libguile.h>
#include "strdef.h"
#include "dt-strpf.h"

#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */


#if defined __INTEL_COMPILER
# pragma warning (disable:1418)
# pragma warning (disable:981)
#endif	/* __INTEL_COMPILER */

SCM_SYNTAX(s_defstrm, "defstrm", scm_i_makbimacro, scm_m_defstrm);
SCM_GLOBAL_SYMBOL(scm_sym_defstrm, s_defstrm);

SCM_SYNTAX(s_deffilt, "deffilt", scm_i_makbimacro, scm_m_deffilt);
SCM_GLOBAL_SYMBOL(scm_sym_deffilt, s_deffilt);

SCM_GLOBAL_KEYWORD(k_from, "from");
SCM_GLOBAL_KEYWORD(k_args, "args");

SCM_SYMBOL(sym_load, "load");
SCM_SYMBOL(sym_begin, "begin");
SCM_SYMBOL(sym_pset, "set-object-property!");

static SCM
__begin(SCM form)
{
	return scm_cons(sym_begin, form);
}

static SCM
__load(const char *fn)
{
	return scm_cons2(sym_load, scm_from_locale_string(fn), SCM_EOL);
}

static __attribute__((unused)) SCM
_load(const char *fn)
{
	return scm_cons(__load(fn), SCM_EOL);
}

static SCM
__pset(SCM sym, SCM what, SCM val)
{
	return scm_list_4(sym_pset, sym, what, val);
}

static SCM
_pset(SCM sym, SCM what, SCM val)
{
	return scm_cons(__pset(sym, what, val), SCM_EOL);
}

static SCM
__define(SCM sym, SCM what)
{
	return scm_list_3(scm_sym_define, sym, what);
}

SCM_DEFINE(
	load_strm, "load-strm", 1, 0, 0,
	(SCM dso),
	"Load the stream from DSO.")
{
#define FUNC_NAME	"load-strm"
	return dso;
#undef FUNC_NAME
}
SCM_GLOBAL_SYMBOL(scm_sym_load_strm, s_load_strm);

SCM_DEFINE(
	load_filt, "load-filt", 1, 0, 0,
	(SCM dso),
	"Load the filter from DSO.")
{
#define FUNC_NAME	"load-filt"
	return dso;
#undef FUNC_NAME
}
SCM_GLOBAL_SYMBOL(scm_sym_load_filt, s_load_filt);


/* some macros */
static SCM
scm_m_defstrm(SCM expr, SCM UNUSED(env))
{
#define FUNC_NAME	"defstrm"
	SCM cdr_expr = SCM_CDR(expr);
	SCM sym;
	SCM dso;

	sym = SCM_CAR(cdr_expr);
	SCM_VALIDATE_SYMBOL(1, sym);

	if (!scm_is_null((cdr_expr = SCM_CDR(cdr_expr)))) {
		SCM tmp;

		if (scm_is_keyword(tmp = SCM_CAR(cdr_expr)) &&
		    scm_is_eq(tmp, k_from)) {
			dso = SCM_CAR(SCM_CDR(cdr_expr));
		}
	} else {
		dso = scm_symbol_to_string(sym);
	}

	expr = __define(sym, scm_cons2(scm_sym_load_strm, dso, SCM_EOL));

#if defined DEBUG_FLAG
	{
		SCM port = scm_current_output_port();
		scm_write(expr, port);
		scm_newline(port);
	}
#endif	/* DEBUG_FLAG */
	return expr;
#undef FUNC_NAME
}

static SCM
scm_m_deffilt(SCM expr, SCM UNUSED(env))
{
#define FUNC_NAME	"deffilt"
	SCM tail = SCM_CDR(expr);
	SCM dso = SCM_EOL;
	SCM pset;
	SCM sym;

	sym = SCM_CAR(tail);
	SCM_VALIDATE_SYMBOL(1, sym);

	expr = pset = __begin(SCM_EOL);

	while (!scm_is_null((tail = SCM_CDR(tail)))) {
		SCM tmp;

		if (scm_is_keyword(tmp = SCM_CAR(tail)) &&
		    scm_is_eq(tmp, k_from)) {
			tail = SCM_CDR(tail);
			dso = SCM_CAR(tail);
		} else if (scm_is_keyword(tmp)) {
			tail = SCM_CDR(tail);
			SCM_SETCDR(pset, _pset(sym, tmp, SCM_CAR(tail)));
			pset = SCM_CDR(pset);
		} else {
			/* must be args then innit? */
			SCM_SETCDR(pset, _pset(sym, k_args, SCM_CAR(tail)));
			pset = SCM_CDR(pset);
		}
	}

	if (scm_is_null(dso)) {
		dso = scm_symbol_to_string(sym);
	}

	/* bang the define */
	{
		SCM ls = scm_cons2(scm_sym_load_strm, dso, SCM_EOL);
		SCM d = __define(sym, ls);

		SCM_SETCDR(expr, scm_cons(d, SCM_CDR(expr)));
	}

#if defined DEBUG_FLAG
	{
		SCM port = scm_current_output_port();
		scm_write(expr, port);
		scm_newline(port);
	}
#endif	/* DEBUG_FLAG */
	return expr;
#undef FUNC_NAME
}


void
init_echs_lisp(void)
{
#if !defined SCM_MAGIC_SNARFER
#	include "echs-lisp.x"
#endif	/* SCM_MAGIC_SNARFER */
	return;
}


#if defined STANDALONE
#if defined __INTEL_COMPILER
# pragma warning (disable:593)
# pragma warning (disable:181)
#elif defined __GNUC__
# pragma GCC diagnostic ignored "-Wswitch"
# pragma GCC diagnostic ignored "-Wswitch-enum"
#endif /* __INTEL_COMPILER */
#include "echse-clo.h"
#include "echse-clo.c"
#if defined __INTEL_COMPILER
# pragma warning (default:593)
# pragma warning (default:181)
#elif defined __GNUC__
# pragma GCC diagnostic warning "-Wswitch"
# pragma GCC diagnostic warning "-Wswitch-enum"
#endif	/* __INTEL_COMPILER */

struct clo_s {
	unsigned int ninp;
	const char **inp;
};

static void*
boot(void *clo)
{
	const struct clo_s *c = clo;
	SCM form;
	SCM tail;

	init_echs_lisp();

	/* start out with a begin block */
	form = tail = __begin(SCM_EOL);
	for (unsigned int i = 0; i < c->ninp; i++) {

		SCM_SETCDR(tail, _load(c->inp[i]));
		tail = SCM_CDR(tail);
	}

	scm_eval_x(form, scm_current_module());
	return NULL;
}

int
main(int argc, char **argv)
{
	/* command line options */
	struct echs_args_info argi[1];
	/* date range to scan through */
	echs_instant_t from;
	echs_instant_t till;
	int res = 0;

	if (echs_parser(argc, argv, argi)) {
		res = 1;
		goto out;
	}

	if (argi->from_given) {
		from = dt_strp(argi->from_arg);
	} else {
		from = (echs_instant_t){2000, 1, 1};
	}

	if (argi->till_given) {
		till = dt_strp(argi->till_arg);
	} else {
		till = (echs_instant_t){2037, 12, 31};
	}

	struct clo_s x = {argi->inputs_num, argi->inputs};
	scm_with_guile(boot, &x);

out:
	echs_parser_free(argi);
	return res;
}
#endif	/* STANDALONE */

/* echs-lisp.c ends here */
