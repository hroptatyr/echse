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
	/* i is our candidate now, yay */
	size_t i = *bi->pos >> 1U;

	/* just store it here and get on with life
	 * check for dupes though */
	for (size_t j = 0U; j < i; j++) {
		if (UNLIKELY(bi->neg[j] == x)) {
			/* dupe, bugger off */
			return;
		}
	}
	*bi->pos += 2U;
	bi->neg[i] = x;
	return;
}

#undef ass_bs
#undef ass_int
#undef bitint_t
