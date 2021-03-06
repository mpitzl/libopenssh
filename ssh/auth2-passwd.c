/* $OpenBSD: auth2-passwd.c,v 1.9 2006/08/03 03:34:41 deraadt Exp $ */
/*
 * Copyright (c) 2000 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <string.h>
#include <stdarg.h>

#include "xmalloc.h"
#include "packet.h"
#include "log.h"
#include "key.h"
#include "hostfile.h"
#include "auth.h"
#include "buffer.h"
#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "monitor_wrap.h"
#include "servconf.h"

/* import */
extern ServerOptions options;

static int
userauth_passwd(struct ssh *ssh)
{
	char *password, *newpass;
	int authenticated = 0;
	int change;
	u_int len, newlen;

	change = ssh_packet_get_char(ssh);
	password = ssh_packet_get_string(ssh, &len);
	if (change) {
		/* discard new password from packet */
		newpass = ssh_packet_get_string(ssh, &newlen);
		memset(newpass, 0, newlen);
		xfree(newpass);
	}
	ssh_packet_check_eom(ssh);

	if (change)
		logit("password change not supported");
	else if (PRIVSEP(auth_password(ssh->authctxt, password)) == 1)
		authenticated = 1;
	memset(password, 0, len);
	xfree(password);
	return authenticated;
}

Authmethod method_passwd = {
	"password",
	userauth_passwd,
	&options.password_authentication
};
