/* $OpenBSD: kexecdhs.c,v 1.2 2010/09/22 05:01:29 djm Exp $ */
/*
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
 * Copyright (c) 2010 Damien Miller.  All rights reserved.
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
#include <signal.h>

#include <openssl/ecdh.h>

#include "buffer.h"
#include "key.h"
#include "cipher.h"
#include "kex.h"
#include "log.h"
#include "packet.h"
#include "dh.h"
#include "ssh2.h"
#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "monitor_wrap.h"
#include "dispatch.h"
#include "compat.h"
#include "err.h"

static int input_kex_ecdh_init(int, u_int32_t, struct ssh *);

int
kexecdh_server(struct ssh *ssh)
{
	debug("expecting SSH2_MSG_KEX_ECDH_INIT");
	ssh_dispatch_set(ssh, SSH2_MSG_KEX_ECDH_INIT, &input_kex_ecdh_init);
	return 0;
}

static int
input_kex_ecdh_init(int type, u_int32_t seq, struct ssh *ssh)
{
	Kex *kex = ssh->kex;
	EC_POINT *client_public;
	EC_KEY *server_key = NULL;
	const EC_GROUP *group;
	const EC_POINT *public_key;
	BIGNUM *shared_secret = NULL;
	struct sshkey *server_host_private, *server_host_public;
	u_char *server_host_key_blob = NULL, *signature = NULL;
	u_char *kbuf = NULL, *hash;
	u_int slen, sbloblen;
	size_t klen = 0, hashlen;
	int curve_nid, r;

	if ((curve_nid = kex_ecdh_name_to_nid(kex->name)) == -1) {
		r = SSH_ERR_INVALID_ARGUMENT;
		goto out;
	}
	if ((server_key = EC_KEY_new_by_curve_name(curve_nid)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if (EC_KEY_generate_key(server_key) != 1) {
		r = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}
	group = EC_KEY_get0_group(server_key);

#ifdef DEBUG_KEXECDH
	fputs("server private key:\n", stderr);
	sshkey_dump_ec_key(server_key);
#endif

	if (kex->load_host_public_key == NULL ||
	    kex->load_host_private_key == NULL) {
		r = SSH_ERR_INVALID_ARGUMENT;
		goto out;
	}
	if ((server_host_public = kex->load_host_public_key(kex->hostkey_type,
	    ssh)) == NULL ||
	    (server_host_private = kex->load_host_private_key(kex->hostkey_type,
	    ssh)) == NULL) {
		r = SSH_ERR_NO_HOSTKEY_LOADED;
		goto out;
	}
	if ((client_public = EC_POINT_new(group)) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if ((r = sshpkt_get_ec(ssh, client_public, group)) != 0 ||
	    (r = sshpkt_get_end(ssh)) != 0)
		goto out;

#ifdef DEBUG_KEXECDH
	fputs("client public key:\n", stderr);
	sshkey_dump_ec_point(group, client_public);
#endif
	if (sshkey_ec_validate_public(group, client_public) != 0) {
		sshpkt_disconnect(ssh, "invalid client public key");
		r = SSH_ERR_MESSAGE_INCOMPLETE;
		goto out;
	}

	/* Calculate shared_secret */
	klen = (EC_GROUP_get_degree(group) + 7) / 8;
	if ((kbuf = malloc(klen)) == NULL ||
	    (shared_secret = BN_new()) == NULL) {
		r = SSH_ERR_ALLOC_FAIL;
		goto out;
	}
	if (ECDH_compute_key(kbuf, klen, client_public,
	    server_key, NULL) != (int)klen ||
	    BN_bin2bn(kbuf, klen, shared_secret) == NULL) {
		r = SSH_ERR_LIBCRYPTO_ERROR;
		goto out;
	}

#ifdef DEBUG_KEXECDH
	dump_digest("shared secret", kbuf, klen);
#endif
	/* calc H */
	if ((r = sshkey_to_blob(server_host_public, &server_host_key_blob,
	    &sbloblen)) != 0)
		goto out;
	if ((r = kex_ecdh_hash(
	    kex->evp_md,
	    group,
	    kex->client_version_string,
	    kex->server_version_string,
	    sshbuf_ptr(kex->peer), sshbuf_len(kex->peer),
	    sshbuf_ptr(kex->my), sshbuf_len(kex->my),
	    server_host_key_blob, sbloblen,
	    client_public,
	    EC_KEY_get0_public_key(server_key),
	    shared_secret,
	    &hash, &hashlen)) != 0)
		goto out;

	/* save session id := H */
	if (kex->session_id == NULL) {
		kex->session_id_len = hashlen;
		kex->session_id = malloc(kex->session_id_len);
		if (kex->session_id == NULL) {
			r = SSH_ERR_ALLOC_FAIL;
			goto out;
		}
		memcpy(kex->session_id, hash, kex->session_id_len);
	}

	/* sign H */
	if ((r = PRIVSEP(sshkey_sign(server_host_private, &signature, &slen,
	    hash, hashlen, ssh->compat))) < 0)
		goto out;

	/* destroy_sensitive_data(); */

	public_key = EC_KEY_get0_public_key(server_key);
	/* send server hostkey, ECDH pubkey 'Q_S' and signed H */
	if ((r = sshpkt_start(ssh, SSH2_MSG_KEX_ECDH_REPLY)) != 0 ||
	    (r = sshpkt_put_string(ssh, server_host_key_blob, sbloblen)) != 0 ||
	    (r = sshpkt_put_ec(ssh, public_key, group)) != 0 ||
	    (r = sshpkt_put_string(ssh, signature, slen)) != 0 ||
	    (r = sshpkt_send(ssh)) != 0)
		goto out;

	if ((r = kex_derive_keys(ssh, hash, hashlen, shared_secret)) == 0)
		r = kex_send_newkeys(ssh);
 out:
	if (kex->ec_client_key) {
		EC_KEY_free(kex->ec_client_key);
		kex->ec_client_key = NULL;
	}
	if (server_host_key_blob)
		free(server_host_key_blob);
	if (server_key)
		EC_KEY_free(server_key);
	if (kbuf) {
		bzero(kbuf, klen);
		free(kbuf);
	}
	if (shared_secret)
		BN_clear_free(shared_secret);
	if (signature)
		free(signature);
	return r;
}
