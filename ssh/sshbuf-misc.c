/*	$OpenBSD$	*/
/*
 * Copyright (c) 2011 Damien Miller
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <resolv.h>

#include "err.h"
#define SSHBUF_INTERNAL
#include "sshbuf.h"

void
sshbuf_dump(struct sshbuf *buf, FILE *f)
{
	u_char *p = sshbuf_ptr(buf);
	size_t i, len = sshbuf_len(buf);

	/* XXX replace with pretty hexdump + ASCII */
	fprintf(f, "buffer %p len = %zu\n", buf, len);
	for (i = 0; i < len; i++) {
		if (i > 0 && (i % 32) == 0)
			fprintf(f, "\n");
		fprintf(f, "%02x", p[i]);
	}
	fprintf(f, "\n");
}

char *
sshbuf_dtob16(struct sshbuf *buf)
{
	size_t i, j, len = sshbuf_len(buf);
	u_char *p = sshbuf_ptr(buf);
	char *ret;
	const char hex[] = "0123456789abcdef";

	if (len == 0)
		return strdup("");
	if (SIZE_MAX / 2 <= len || (ret = malloc(len * 2 + 1)) == NULL)
		return NULL;
	for (i = j = 0; i < len; i++) {
		ret[j++] = hex[(p[i] >> 4) & 0xf];
		ret[j++] = hex[p[i] & 0xf];
	}
	ret[j] = '\0';
	return ret;
}

char *
sshbuf_dtob64(struct sshbuf *buf)
{
	size_t len = sshbuf_len(buf), plen;
	u_char *p = sshbuf_ptr(buf);
	char *ret;
	int r;

	if (len == 0)
		return strdup("");
	plen = ((len + 2) / 3) * 4 + 1;
	if (SIZE_MAX / 2 <= len || (ret = malloc(plen)) == NULL)
		return NULL;
	if ((r = b64_ntop(p, len, ret, plen)) == -1) {
		bzero(ret, plen);
		free(ret);
		return NULL;
	}
	return ret;
}

int
sshbuf_b64tod(struct sshbuf *buf, const char *b64)
{
	size_t plen = strlen(b64);
	int nlen, r;
	u_char *p;

	if (plen == 0)
		return 0;
	if ((p = malloc(plen)) == NULL)
		return SSH_ERR_ALLOC_FAIL;
	if ((nlen = b64_pton(b64, p, plen)) < 0) {
		bzero(p, plen);
		free(p);
		return SSH_ERR_INVALID_FORMAT;
	}
	if ((r = sshbuf_put(buf, p, nlen)) < 0) {
		bzero(p, plen);
		free(p);
		return r;
	}
	bzero(p, plen);
	free(p);
	return 0;
}
