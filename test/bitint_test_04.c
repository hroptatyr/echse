#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdio.h>
#include "bitint.h"


int
main(int argc, char *argv[])
{
	bitint31_t x = {0U};
	int v;

	x = ass_bi31(x, 19U);
	for (bitint_iter_t i = 0U; (v = bi31_next(&i, x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	x = ass_bi31(x, 4U);
	for (bitint_iter_t i = 0U; (v = bi31_next(&i, x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	x = ass_bi31(x, -4U);
	for (bitint_iter_t i = 0U; (v = bi31_next(&i, x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	x = ass_bi31(x, 0U);
	x = ass_bi31(x, 1U);
	x = ass_bi31(x, -2U);
	for (bitint_iter_t i = 0U; (v = bi31_next(&i, x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');
	return 0;
}

/* bitint_test.c ends here */
