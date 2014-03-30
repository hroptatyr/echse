#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdio.h>
#include "bitint.h"


int
main(int argc, char *argv[])
{
	bitint383_t x = {0U};
	int v;

	ass_bi383(&x, 36);
	for (bitint_iter_t i = 0U; (v = bi383_next(&i, &x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	ass_bi383(&x, 40U);
	for (bitint_iter_t i = 0U; (v = bi383_next(&i, &x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	ass_bi383(&x, -4U);
	ass_bi383(&x, -200U);
	for (bitint_iter_t i = 0U; (v = bi383_next(&i, &x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	ass_bi383(&x, 0U);
	ass_bi383(&x, 1U);
	ass_bi383(&x, -33U);
	for (bitint_iter_t i = 0U; (v = bi383_next(&i, &x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');
	return 0;
}

/* bitint_test.c ends here */
