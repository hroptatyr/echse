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

#if !defined UNUSED
# define UNUSED(x)		__attribute__((unused)) x
#endif	/* UNUSED */


SCM_SYNTAX(s_defstrm, "defstrm", scm_i_makbimacro, scm_m_defstrm);
SCM_GLOBAL_SYMBOL(scm_sym_defstrm, s_defstrm);

SCM_SYNTAX(s_deffilt, "deffilt", scm_i_makbimacro, scm_m_deffilt);
SCM_GLOBAL_SYMBOL(scm_sym_deffilt, s_deffilt);

SCM_GLOBAL_KEYWORD(k_from, "from");

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

	SCM_SETCAR(expr, scm_sym_define);
	SCM_SETCDR(
		expr,
		scm_list_2(
			sym,
			scm_list_2(scm_sym_load_strm, dso)));

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
	SCM cdr_expr = SCM_CDR(expr);
	SCM sym;
	SCM dso;

	sym = SCM_CAR(cdr_expr);
	SCM_VALIDATE_SYMBOL(1, sym);

	dso = scm_symbol_to_string(sym);

	SCM_SETCAR(expr, scm_sym_define);
	SCM_SETCDR(
		expr,
		scm_list_2(
			sym,
			scm_list_2(scm_sym_load_filt, dso)));

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
static void
inner_main(void *UNUSED(closure), int argc, char **argv)
{
	init_echs_lisp();
	scm_shell(argc, argv);
	return;
}

int
main (int argc, char **argv)
{
	scm_boot_guile(argc, argv, inner_main, 0);
	return 0;
}
#endif	/* STANDALONE */

/* echs-lisp.c ends here */
