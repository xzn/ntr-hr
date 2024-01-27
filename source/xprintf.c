#include "xprintf.h"

int xsprintf(char*, const char*, ...) {
	// TODO
	return 0;
}

int xsnprintf(char*, size_t, const char*, ...) {
	// TODO
	return 0;
}

int strnjoin(char *dst, size_t dst_len, const char *s1, const char *s2) {
	if (!dst || !dst_len || !s1 || !s2) return 1;

	while (*s1 && --dst_len)
		*dst++ = *s1++;
	if (dst_len)
		while (*s2 && --dst_len)
			*dst++ = *s2++;
	*dst = '\0';

	return *s1 || *s2;
}
