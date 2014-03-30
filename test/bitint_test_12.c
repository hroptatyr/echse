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
	ass_bi383(&x, -48);
	ass_bi383(&x, 59);
	ass_bi383(&x, -61);
	for (bitint_iter_t i = 0U; (v = bi383_next(&i, &x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	ass_bi383(&x, 40);
	ass_bi383(&x, -44);
	ass_bi383(&x, 101);
	ass_bi383(&x, -72);
	for (bitint_iter_t i = 0U; (v = bi383_next(&i, &x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	ass_bi383(&x, 0);
	ass_bi383(&x, -102);
	ass_bi383(&x, 122);
	ass_bi383(&x, -133);
	for (bitint_iter_t i = 0U; (v = bi383_next(&i, &x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');

	ass_bi383(&x, -144);
	ass_bi383(&x, 242);
	ass_bi383(&x, -299);
	ass_bi383(&x, 302);
	for (bitint_iter_t i = 0U; (v = bi383_next(&i, &x), i);) {
		printf("got %d\n", v);
	}
	putchar('\n');
	return 0;
}

/* bitint_test.c ends here */
