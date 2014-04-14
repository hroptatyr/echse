static void
ass_bs(bitint_t *restrict bi, int x)
{
	if (x > 0) {
		unsigned int p = (unsigned int)x % POS_BITZ;
		unsigned int j = (unsigned int)x / POS_BITZ;

		bi->pos[j] |= 1U << p;
	} else {
		unsigned int p = (unsigned int)(-x) % NEG_BITZ;
		unsigned int j = (unsigned int)(-x) / NEG_BITZ;
		bi->neg[j] |= 1U << p;
	}
	return;
}

static void
ass_int(bitint_t *restrict bi, int x)
{
/* perform an insertion sort, with the special order
 * defined by the iterator, also use that sortedness
 * to check for dupes */
	/* i is our candidate now, yay */
	size_t i = *bi->pos >> 1U;
	size_t j;

	if (x >= 0) {
		for (j = 0UL; j < i && bi->neg[j] >= 0 && bi->neg[j] < x; j++);
	} else {
		for (j = 0UL; j < i && bi->neg[j] > x; j++);
	}
	/* now there's either j == i, in which case there's
	 * trivially no dupes, or j < i, then there's no
	 * dupes iff bi->neg[j] != x */
	if (j < i && UNLIKELY(bi->neg[j] == x)) {
		/* dupe, just get on with life */
		return;
	} else if (j < i) {
		/* move the bobs >= j one slot further down the road */
		const size_t nmv = (i - j) * sizeof(*bi->neg);
		memmove(bi->neg + j + 1U, bi->neg + j, nmv);
	}
	/* now it's trivial, just insert at i == j */
	bi->neg[j] = x;
	/* ... and up the counter */
	*bi->pos += 2U;
	return;
}

#undef ass_bs
#undef ass_int
#undef bitint_t
