#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdio.h>
#include "bitint.h"


int
main(int argc, char *argv[])
{
	bitint63_t x = {0U};
	int v;

	x = ass_bi63(x, 36U);
	for (bitint_iter_t i = 0U; (v = bi63_next(&i, x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	x = ass_bi63(x, 40U);
	for (bitint_iter_t i = 0U; (v = bi63_next(&i, x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	x = ass_bi63(x, -4U);
	for (bitint_iter_t i = 0U; (v = bi63_next(&i, x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	x = ass_bi63(x, 0U);
	x = ass_bi63(x, 1U);
	x = ass_bi63(x, -33U);
	for (bitint_iter_t i = 0U; (v = bi63_next(&i, x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');
	return 0;
}

/* bitint_test.c ends here */
