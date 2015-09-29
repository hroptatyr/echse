/*** dt-strpf.h -- parser and formatter funs for echse
 *
 * Copyright (C) 2011-2014 Sebastian Freundt
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
 **/
#if !defined INCLUDED_dt_strpf_h_
#define INCLUDED_dt_strpf_h_

#include <stddef.h>
#include "instant.h"
#include "range.h"

/**
 * Parse STR with the standard parser. */
extern echs_instant_t dt_strp(const char *str, char **on, size_t len);

/**
 * Print INST into BUF (of size BSZ) and return its length. */
extern size_t dt_strf(char *restrict buf, size_t bsz, echs_instant_t inst);

/**
 * Print INST into BUF (of size BSZ) in ical format and return its length. */
extern size_t dt_strf_ical(char *restrict buf, size_t bsz, echs_instant_t inst);

/**
 * Parse STR as range with the standard parser. */
extern echs_range_t range_strp(const char *str, char **on, size_t len);

/**
 * Print RANGE into BUF (of size BSZ) in ISO format and return its length. */
extern size_t range_strf(char *restrict buf, size_t bsz, echs_range_t range);

#endif	/* INCLUDED_dt_strpf_h_ */
