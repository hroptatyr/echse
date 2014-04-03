#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#include <stdio.h>
#include "bitint.h"


int
main(int argc, char *argv[])
{
	bitint31_t x = {0U};

	x = ass_bi31(x, 19);
	printf("%x %x\n", x.pos, x.neg);

	x = ass_bi31(x, 4);
	printf("%x %x\n", x.pos, x.neg);

	x = ass_bi31(x, -4);
	printf("%x %x\n", x.pos, x.neg);

	x = ass_bi31(x, 0);
	printf("%x %x\n", x.pos, x.neg);
	x = ass_bi31(x, 1);
	printf("%x %x\n", x.pos, x.neg);
	x = ass_bi31(x, -2);
	printf("%x %x\n", x.pos, x.neg);
	return 0;
}

/* bitint_test.c ends here */
