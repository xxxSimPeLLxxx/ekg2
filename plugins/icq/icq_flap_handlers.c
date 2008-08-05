/*
 *  (C) Copyright 2006-2008 Jakub Zawadzki <darkjames@darkjames.ath.cx>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ekg/debug.h>
#include <ekg/dynstuff.h>
#include <ekg/protocol.h>
#include <ekg/sessions.h>
#include <ekg/plugins.h>
#include <ekg/xmalloc.h>

#include "icq.h"
#include "misc.h"

#include "icq_flap_handlers.h"
#include "icq_snac_handlers.h"

static inline char *_icq_makeflap(uint8_t cmd, uint16_t id, uint16_t len) {
	static char buf[FLAP_PACKET_LEN];
	string_t tempstr;

	tempstr = icq_pack("CCWW", (uint32_t) 0x2a, (uint32_t) cmd, (uint32_t) id, (uint32_t) len);
	if (tempstr->len != FLAP_PACKET_LEN) {
		debug_error("_icq_makeflap() critical error\n");
		return NULL;
	}
	memcpy(buf, tempstr->str, FLAP_PACKET_LEN);
	string_free(tempstr, 1);
	return buf;
}

// static inline void _icq_decodeflap();

void icq_makeflap(session_t *s, string_t pkt, uint8_t cmd) {
	icq_private_t *j;

	if (!s || !(j = s->priv) || !pkt) 
		return;

	if (!j->flap_seq)
		j->flap_seq = (rand() & 0x7fff);	/* XXX */

	j->flap_seq++;
	j->flap_seq &= 0x7fff;

	debug_function("icq_makeflap() 0x%x\n", cmd);
	string_insert_n(pkt, 0, _icq_makeflap(cmd, j->flap_seq, pkt->len), FLAP_PACKET_LEN);
}

#define ICQ_FLAP_HANDLER(x) int x(session_t *s, unsigned char *buf, int len)
typedef int (*flap_handler_t)     (session_t * , unsigned char *    , int    );

#define ICQ_FLAP_LOGIN	0x01
static ICQ_FLAP_HANDLER(icq_flap_login) {
	struct {
		uint32_t id;				/* LOGIN PACKET: 0x00 00 00 01 */
		unsigned char *buf;			/* extra data ? */
	} login;

	icq_private_t *j = s->priv;

	debug_function("icq_flap_login()\n");

	if (!ICQ_UNPACK(&(login.buf), "I", &login.id))
		return -1;

	debug("icq_flap_login() id=%.8x extralen=%d\n", login.id, len);

	if (len != 0) {
		debug_error("icq_flap_login() len\n");
		return -2;
	}

	if (login.id != 1) {
		debug_error("icq_flap_login() login.id\n");
		return -2;
	}

	if (j->connecting == 1) {	/* hub connecting */
		if (session_int_get(s, "plaintext_passwd") == 1) {
			string_t str;
			char *password;

			debug("icq_flap_login(1) PLAINTEXT\n");

			str = string_init(NULL);

			icq_pack_append(str, "I", (uint32_t) 1);			/* spkt.flap.pkt.login.id CLI_HELLO */
			/* TLVs */
			icq_pack_append(str, "T", icq_pack_tlv_str(1, s->uid + 4));	/* uid */

/* XXX, miranda assume hash has got max 20 chars? */
			password = icq_encryptpw(session_get(s, "password"));
			icq_pack_append(str, "T", icq_pack_tlv_str(2, password));	/* password */
			xfree(password);

			icq_pack_append(str, "T", icq_pack_tlv_str(0x03, "ICQ Client"));
			icq_pack_append(str, "tW", icq_pack_tlv_word(0x16, 0x010a)); 			/* CLIENT_ID_CODE */
			icq_pack_append(str, "tW", icq_pack_tlv_word(0x17, 0x0006));			/* CLIENT_VERSION_MAJOR */
			icq_pack_append(str, "tW", icq_pack_tlv_word(0x18, 0x0000));			/* CLIENT_VERSION_MINOR */
			icq_pack_append(str, "tW", icq_pack_tlv_word(0x19, 0x0000));			/* CLIENT_VERSION_LESSER */
			icq_pack_append(str, "tW", icq_pack_tlv_word(0x1a, 0x17AB));			/* CLIENT_VERSION_BUILD */
			icq_pack_append(str, "tI", icq_pack_tlv_dword(0x14, 0x00007535));		/* CLIENT_DISTRIBUTION */
			icq_pack_append(str, "T", icq_pack_tlv_str(0x0f, "en"));
			icq_pack_append(str, "T", icq_pack_tlv_str(0x0e, "en"));

			icq_makeflap(s, str, ICQ_FLAP_LOGIN);
			icq_send_pkt(s, str);	str = NULL;

		} else {
			string_t str;

			debug("icq_flap_login(1) MD5\n");

			str = icq_pack("I", (uint32_t) 1);			/* spkt.flap.pkt.login.id CLI_HELLO */
			icq_pack_append(str, "tI", icq_pack_tlv_dword(0x8003, 0x00100000));		/* unknown */
			icq_makeflap(s, str, ICQ_FLAP_LOGIN);
			icq_send_pkt(s, str);	str = NULL;

			str = string_init(NULL);

			icq_pack_append(str, "T", icq_pack_tlv_str(1, s->uid + 4));	/* uid */
			icq_makesnac(s, str, 0x17, 6, 0, 0);
			icq_send_pkt(s, str); str = NULL;
		}

	}
	else
	if (j->connecting == 2) {	/* server connecting */
		string_t str;

		debug("icq_flap_login(2) s=0x%x cookie=0x%x cookielen=%d\n", s, j->cookie, j->cookie ? j->cookie->len : -1);

		if (!j->cookie) {
			debug_error("j->cookie == NULL???\n");
			return -2;
		}

		str = icq_pack("I", (uint32_t) 1);			/* spkt.flap.pkt.login.id CLI_HELLO */
		icq_pack_append(str, "T", icq_pack_tlv(0x06, j->cookie->str, j->cookie->len));

#if 0	/* wo -- not necessary */
		/* Pack client identification details. */
		icq_pack_append(str, "T", icq_pack_tlv_str(0x03, "ICQ Client"));
		icq_pack_append(str, "tW", icq_pack_tlv_word(0x16, 0x010a)); 			/* CLIENT_ID_CODE */
		icq_pack_append(str, "tW", icq_pack_tlv_word(0x17, 0x0006));			/* CLIENT_VERSION_MAJOR */
		icq_pack_append(str, "tW", icq_pack_tlv_word(0x18, 0x0000));			/* CLIENT_VERSION_MINOR */
		icq_pack_append(str, "tW", icq_pack_tlv_word(0x19, 0x0000));			/* CLIENT_VERSION_LESSER */
		icq_pack_append(str, "tW", icq_pack_tlv_word(0x1a, 0x17AB));			/* CLIENT_VERSION_BUILD */
		icq_pack_append(str, "tI", icq_pack_tlv_word(0x14, 0x00007535));		/* CLIENT_DISTRIBUTION */
		icq_pack_append(str, "T", icq_pack_tlv_str(0x0f, "en"));
		icq_pack_append(str, "T", icq_pack_tlv_str(0x0e, "en"));
		icq_pack_append(str, "tI", icq_pack_tlv_word(0x8003, 0x00100000));	/* Unknown */
#endif
		icq_makeflap(s, str, ICQ_FLAP_LOGIN);
		icq_send_pkt(s, str); str = NULL;

		string_free(j->cookie, 1); j->cookie = NULL;
	}
	else {
		debug_error("icq_flap_login(%d) XXX?\n", j->connecting);
		return -2;
	}

	return 0;
}

#define ICQ_FLAP_DATA	0x02

static ICQ_FLAP_HANDLER(icq_flap_data) {
	snac_packet_t snac;
	unsigned char *data;

	debug_function("icq_flap_data()\n");

	if (!ICQ_UNPACK(&(snac.data), "WWWI", &(snac.family), &(snac.cmd), &(snac.flags), &(snac.ref)))
		return -1;

	data = snac.data;

	debug_white("icq_flap_data() SNAC pkt, fam=0x%x cmd=0x%x flags=0x%x ref=0x%x (datalen=%d)\n", snac.family, snac.cmd, snac.flags, snac.ref, len);

	if (snac.flags & 0x8000) {
		uint16_t skip_len;

		if (!icq_unpack(data, &data, &len, "W", &skip_len))
			return -1;

		if (len < skip_len)
			return -1;

		data += skip_len; len -= skip_len;

		debug_white("icq_flap_data() len left: %d\n", len);
	}

	icq_snac_handler(s, snac.family, snac.cmd, data, len);

	return 0;
}

#define ICQ_FLAP_ERROR	0x03
static ICQ_FLAP_HANDLER(icq_flap_error) {
	debug_function("icq_flap_error()\n");
	return 0;
}

#define ICQ_FLAP_CLOSE	0x04

static ICQ_FLAP_HANDLER(icq_flap_close) {
	icq_private_t *j = s->priv;

	struct icq_tlv_list *tlvs;
	icq_tlv_t *login_tlv;

	debug_function("icq_flap_close()\n");

	if (!(tlvs = icq_unpack_tlvs(buf, len, 0)))
		return -1;

	if ((login_tlv = icq_tlv_get(tlvs, 5)) && login_tlv->len) {
		icq_tlv_t *cookie_tlv = icq_tlv_get(tlvs, 0x06);
		char *login_str = xstrndup(login_tlv->buf, login_tlv->len);
		struct sockaddr_in sin;
		string_t pkt;
		char *tmp;
		int port;
		int fd;

		if (!cookie_tlv) {
			debug_error("icq_flap_close() loginTLV, but no cookieTLV?\n");
			icq_tlvs_destroy(&tlvs);
			return -2;
		}

		if (!(tmp = xstrchr(login_str, ':'))) {
			debug(".... TLV[5] == %s not in format IP:PORT ?\n", login_str);
			xfree(login_str);
			icq_tlvs_destroy(&tlvs);
			return -2;
		}

		port = atoi(tmp + 1);
		*tmp = '\0';

		/* ip: login_str...tmp */

		debug("icq_flap_close() Redirect to server %s:%d\n", login_str, port);

		string_free(j->cookie, 1);
		j->cookie = string_init(NULL);
		string_append_raw(j->cookie, (char *) cookie_tlv->buf, cookie_tlv->len);

		/* FlapCliGoodbye() */
		pkt = string_init(NULL);
		icq_makeflap(s, pkt, ICQ_FLAP_CLOSE);
		icq_send_pkt(s, pkt); pkt = NULL;

		j->connecting = 2;

		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			int err = errno;

			debug_error("icq_flap_close() socket() failed..\n");

			errno = err;
			return -2;
		}

		sin.sin_family		= AF_INET;
		sin.sin_addr.s_addr 	= inet_addr(login_str);
		sin.sin_port		= ntohs(port);

		if (connect(fd, (struct sockaddr *) &sin, sizeof(sin))) {
			int err = errno;

			debug_error("icq_flap_close() connect() failed..\n");
			close(fd);
			errno = err;
			return -2;
		}

		j->fd2 = fd;
	} else {
		icq_tlv_t *t_uid = icq_tlv_get(tlvs, 0x01);
		icq_tlv_t *t_url = icq_tlv_get(tlvs, 0x04);			/* [NOT ALWAYS] url, str */
		icq_tlv_t *t_err1= icq_tlv_get(tlvs, 0x08);
		icq_tlv_t *t_err2= icq_tlv_get(tlvs, 0x09);
//		icq_tlv_t *unk	=  icq_tlv_get(l, 0x0C);			/* unk, seen 0x5F 0x65 */

		char *reason = NULL;

		if (t_uid && t_uid->len) {
			char *uid = xstrndup(t_uid->buf, t_uid->len);
			if (xstrcmp(uid, s->uid+4))
				debug("icq_ UID: %s\n", uid);
			xfree(uid);
		}

		if (t_url && t_url->len) {
			char *url = xstrndup(t_url->buf, t_url->len);
			debug("icq_ URL: %s\n", url);
			xfree(url);
		}

		/* XXX, reason */
		/* XXX: t_err1->nr == 0x05 -> mIcq unset password */

		if (t_err1 && t_err1->nr == 24) {
			reason = "You logged in too frequently, please wait 30 minutes before trying again.";
		} else {
			debug("FLAP_CHANNEL4 1048 Error code: %ld\n", t_err2 ? t_err2->nr : t_err1 ? t_err1->nr : -1);
		}

		icq_handle_disconnect(s, reason, EKG_DISCONNECT_FAILURE);	/* XXX */
	}

	icq_tlvs_destroy(&tlvs);

	return 0;
}

#define ICQ_FLAP_PING	0x05
static ICQ_FLAP_HANDLER(icq_flap_ping) {
	struct {
		uint16_t seq;		/* XXX, BE/LE? */
		uint16_t len;		/* XXX, BE/LE? */
		unsigned char *data;
	} ping;

	debug_function("icq_flap_ping()\n");

	if (!ICQ_UNPACK(&(ping.data), "WW", &(ping.seq), (ping.len)))
		return -1;

	if (len) {
		debug("icq_flap_ping() dump");
		icq_hexdump(DEBUG_WHITE, ping.data, len);
	}

	return 0;
}

int icq_flap_handler(session_t *s, int fd, char *b, int len) {
	unsigned char *buf = (unsigned char *) b;
	int next_flap = 0;

	debug_iorecv("icq_flap_loop(%s) fd: %d len: %d\n", s->uid, fd, len);

	while (len >= FLAP_PACKET_LEN) {
		flap_packet_t flap;
		flap_handler_t handler;

		if (next_flap) 
			debug("icq_flap_loop() nextflap restlen: %d\n", len);

		if (buf[0] != 0x2A) {
			debug_error("icq_flap_loop() Incoming packet is not a FLAP: id is %d.\n", buf[0]);
			icq_hexdump(DEBUG_ERROR, (unsigned char *) buf, len);
			return -2;
		}

		if (!ICQ_UNPACK(&(flap.data), "CCWW", &flap.unique, &flap.cmd, &flap.id, &flap.len))
			return -1;

		buf = flap.data;

		debug_white("icq_flap_loop() FLAP PKT cmd=0x%x id=0x%x len: %d bytes (rlen: %d)\n", flap.cmd, flap.id, flap.len, len);

		if (flap.len > len)
			return -1;

/* micq: conn->stat_pak_rcvd++; conn->stat_real_pak_rcvd++; */ 

		switch (flap.cmd) {
			case ICQ_FLAP_LOGIN:	handler = icq_flap_login;	break;
			case ICQ_FLAP_DATA:	handler = icq_flap_data;	break;
			case ICQ_FLAP_ERROR:	handler = icq_flap_error;	break;
			case ICQ_FLAP_CLOSE:	handler = icq_flap_close;	break;
			case ICQ_FLAP_PING:	handler = icq_flap_ping;	break;
			default:		handler = NULL;			break;
		}

		if (!handler) {
			debug("icq_flap_loop() 1884 FLAP with unknown channel %x received.\n", flap.cmd);
			return -2;
		}

		handler(s, flap.data, flap.len);

	/* next flap? */
		buf += (flap.len);
		len -= (flap.len);		
		next_flap = 1;
	}

	return next_flap ? 0 : -1;	/* ??? XXX */
}
