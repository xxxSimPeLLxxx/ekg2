/* $Id$ */

/*
 *  (C) Copyright 2003 Wojtek Kaniewski <wojtekka@irc.pl
 * 		  2004 Piotr Kupisiewicz <deletek@ekg2.org>
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

#include "ekg2-config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>

#include <libgadu.h>

#include <ekg/commands.h>
#include <ekg/dynstuff.h>
#include <ekg/msgqueue.h>
#include <ekg/protocol.h>
#include <ekg/sessions.h>
#include <ekg/stuff.h>
#include <ekg/userlist.h>
#include <ekg/themes.h>
#include <ekg/vars.h>
#include <ekg/xmalloc.h>
#include <ekg/log.h>

#include "dcc.h"
#include "gg.h"
#include "misc.h"
#include "pubdir.h"
#include "pubdir50.h"

static int gg_plugin_destroy();
static int gg_theme_init();

plugin_t gg_plugin = {
	name: "gg",
	pclass: PLUGIN_PROTOCOL,
	destroy: gg_plugin_destroy,
	theme_init: gg_theme_init,
};

int gg_private_init(session_t *s)
{
	gg_private_t *g;
	
	if (!s)
		return -1;

	if (xstrncasecmp(session_uid_get(s), "gg:", 3))
		return -1;

	g = xmalloc(sizeof(gg_private_t));
	memset(g, 0, sizeof(gg_private_t));

	userlist_free(s);
	userlist_read(s);
	session_private_set(s, g);

	return 0;
}

int gg_private_destroy(session_t *s)
{
	gg_private_t *g;
	list_t l;
	
	if (!s)
		return -1;

	if (xstrncasecmp(session_uid_get(s), "gg:", 3))
		return -1;

	if (!(g = session_private_get(s)))
		return -1;

	if (g->sess)
		gg_free_session(g->sess);

	for (l = g->searches; l; l = l->next)
		gg_pubdir50_free((gg_pubdir50_t) l->data);

	xfree(g);

	session_private_set(s, NULL);

	return 0;
}

int gg_userlist_added_handle(void *data, va_list ap)
{
	char **uid = va_arg(ap, char**);
	char **params = va_arg(ap, char**);
	int *quiet = va_arg(ap, int*);
	
	gg_command_modify("add", (const char **) params, session_current, NULL, *quiet);

	uid = NULL;

	return 0;
}


int gg_session_handle(void *data, va_list ap)
{
	char **uid = va_arg(ap, char**);
	session_t *s = session_find(*uid);
	
	if (!s)
		return 0;

	if (data)
		gg_private_init(s);
	else
		gg_private_destroy(s);

	return 0;
}

int gg_user_offline_handle(void *data, va_list ap)
{
	userlist_t **__u = va_arg(ap, userlist_t**), *u = *__u;
	session_t **__session = va_arg(ap, session_t**), *session = *__session;
	gg_private_t *g = session_private_get(session);
	int uin = atoi(u->uid + 3);

        gg_remove_notify_ex(g->sess, uin, gg_userlist_type(u));
	ekg_group_add(u, "__offline");
        print("modify_offline", format_user(session, u->uid));
        gg_add_notify_ex(g->sess, uin, gg_userlist_type(u));

	return 0;
}

int gg_user_online_handle(void *data, va_list ap)
{
        userlist_t **__u = va_arg(ap, userlist_t**), *u = *__u;
        session_t **__session = va_arg(ap, session_t**), *session = *__session;
        gg_private_t *g = session_private_get(session);
	int quiet = (int ) data;
        int uin = atoi(u->uid + 3);

        gg_remove_notify_ex(g->sess, uin, gg_userlist_type(u));
        ekg_group_remove(u, "__offline");
        printq("modify_online", format_user(session, u->uid));
        gg_add_notify_ex(g->sess, uin, gg_userlist_type(u));

	return 0;
}

int gg_status_show_handle(void *data, va_list ap)
{
        char **uid = va_arg(ap, char**);
        session_t *s = session_find(*uid);
        userlist_t *u;
        struct in_addr i;
        struct tm *t;
        time_t n;
        int mqc, now_days;
        char *tmp, *priv, *r1, *r2, buf[100];
	gg_private_t *g;

        if (!s) {
                debug("Function gg_status_show_handle() called with NULL data\n");
                return -1;
        }
        if (!(g = session_private_get(s)))
                return -1;


        if (config_profile)
                print("show_status_profile", config_profile);

        if ((u = userlist_find(s, s->uid)) && u->nickname)
                print("show_status_uid_nick", s->uid, u->nickname);
        else
                print("show_status_uid", s->uid);

        n = time(NULL);
        t = localtime(&n);
        now_days = t->tm_yday;

        t = localtime(&s->last_conn);
        strftime(buf, sizeof(buf), format_find((t->tm_yday == now_days) ? "show_status_last_conn_event_today" : "show_status_last_conn_event"), t);

        if (!g->sess || g->sess->state != GG_STATE_CONNECTED) {
                char *tmp = format_string(format_find("show_status_notavail"));

                print("show_status_status", tmp, "");

                if (s->last_conn)
                        print("show_status_disconnected_since", buf);
                if ((mqc = msg_queue_count()))
                        print("show_status_msg_queue", itoa(mqc));

                xfree(tmp);

                return 0;
        }

        if (GG_S_F(g->sess->status))
                priv = format_string(format_find("show_status_private_on"));
        else
                priv = format_string(format_find("show_status_private_off")); 

        r1 = xstrmid(s->descr, 0, GG_STATUS_DESCR_MAXSIZE);
        r2 = xstrmid(s->descr, GG_STATUS_DESCR_MAXSIZE, -1);

        tmp = format_string(format_find(ekg_status_label(s->status, s->descr, "show_status_")), r1, r2);

        xfree(r1);
        xfree(r2);

        i.s_addr = g->sess->server_addr;

        print("show_status_status", tmp, priv);
#ifdef __GG_LIBGADU_HAVE_OPENSSL
        if (g->sess->ssl)
                print("show_status_server_tls", inet_ntoa(i), itoa(g->sess->port));
        else
#endif
                print("show_status_server", inet_ntoa(i), itoa(g->sess->port));
        print("show_status_connected_since", buf);

        xfree(tmp);
        xfree(priv);

        return 0;
}

/*
 * str_to_uin()
 *
 * funkcja, kt�ra zajmuje si� zamian� stringa na
 * liczb� i sprawdzeniem, czy to prawid�owy uin.
 *
 * zwraca uin lub 0 w przypadku b��du.
 */
uin_t str_to_uin(const char *text)
{
        char *tmp;
        long num;

        if (!text)
                return 0;

        errno = 0;
        num = strtol(text, &tmp, 0);

        if (*text == '\0' || *tmp != '\0')
                return 0;

        if ((errno == ERANGE || (num == LONG_MAX || num == LONG_MIN)) || num > UINT_MAX || num < 0)
                return 0;

        return (uin_t) num;
}

/* 
 * trzeba doda� numer u�ytkownika do listy os�b, o kt�rych
 * zmianach status�w chcemy by� informowani 
 */
int gg_add_notify_handle(void *data, va_list ap)
{
	char **session_uid = va_arg(ap, char**);
	session_t *s = session_find(*session_uid);
 	char **uid = va_arg(ap, char**);
	gg_private_t *g;

	if (!s) {
		debug("Function gg_add_notify_handle() called with NULL data\n");
		return -1;
	}
        if (!(g = session_private_get(s)))
		return -1;

	gg_add_notify_ex(g->sess, str_to_uin(strchr(*uid, ':') + 1), gg_userlist_type(userlist_find(s, s->uid))); 
	return 0;
}

/*
 * trzeba usun��� numer u�ytkownika z listy os�b, o kt�rych
 * zmianach status�w chcemy by� informowani
 */
int gg_remove_notify_handle(void *data, va_list ap)
{
        char **session_uid = va_arg(ap, char**);
        session_t *s = session_find(*session_uid);
        char **uid = va_arg(ap, char**);
        gg_private_t *g;

        if (!s) {
                debug("Function gg_remove_notify_handle() called with NULL data\n");
                return -1;
        }
        if (!(g = session_private_get(s)))
                return -1;

        gg_remove_notify(g->sess, str_to_uin(strchr(*uid, ':') + 1));
        return 0;
}

/*
 * gg_print_version()
 *
 * wy�wietla wersj� pluginu i biblioteki.
 */
int gg_print_version(void *data, va_list ap)
{
	char **tmp1 = array_make(GG_DEFAULT_CLIENT_VERSION, ", ", 0, 1, 0);
	char *tmp2 = array_join(tmp1, ".");
	char *tmp3 = saprintf("libgadu %s (headers %s), protocol %s (0x%.2x)", gg_libgadu_version(), GG_LIBGADU_VERSION, tmp2, GG_DEFAULT_PROTOCOL_VERSION);

	print("generic", tmp3);

	xfree(tmp3);
	xfree(tmp2);
	array_free(tmp1);

	return 0;
}

/*
 * gg_validate_uid()
 *
 * sprawdza, czy dany uid jest poprawny i czy plugin do obs�uguje.
 */
int gg_validate_uid(void *data, va_list ap)
{
	char **uid = va_arg(ap, char **);
	int *valid = va_arg(ap, int *);

	if (!*uid)
		return 0;

	if (!xstrncasecmp(*uid, "gg:", 3))
		(*valid)++;
	
	return 0;
}

/*
 * gg_ping_timer_handler()
 *
 * pinguje serwer co jaki� czas, je�li jest nadal po��czony.
 */
static void gg_ping_timer_handler(int type, void *data)
{
	session_t *s = session_find((char*) data);
	gg_private_t *g;

	if (type == 1) {
		xfree(data);
		return;
	}

	if (!s || session_connected_get(s) != 1)
		return;

	if ((g = session_private_get(s))) {
		char buf[100];

		gg_ping(g->sess);

		snprintf(buf, sizeof(buf), "ping-%s", s->uid + 3);
		timer_add(&gg_plugin, buf, 180, 0, gg_ping_timer_handler, xstrdup(s->uid));
	}
}

/*
 * gg_session_handler_success()
 *
 * obs�uga udanego po��czenia z serwerem.
 */
static void gg_session_handler_success(session_t *s)
{
	char *__session = xstrdup(session_uid_get(s));
	gg_private_t *g = session_private_get(s);
        const char *status;
	char *descr;
	char buf[100];

	if (!g || !g->sess) {
		debug("[gg] gg_session_handler_success() called with null gg_private_t\n");
		return;
	}

	session_connected_set(s, 1);
	session_unidle(s);

	query_emit(NULL, "protocol-connected", &__session);
	xfree(__session);

	gg_userlist_send(g->sess, s->userlist);

	s->last_conn = time(NULL);
	
	/* zapiszmy adres serwera */
	if (session_int_get(s, "connection_save") == 1) {
		struct in_addr addr;		

		addr.s_addr = g->sess->server_addr;
		session_set(s, "server", inet_ntoa(addr));
		session_int_set(s, "port", g->sess->port);
	}

	/* pami�tajmy, �eby pingowa� */
	snprintf(buf, sizeof(buf), "ping-%s", s->uid + 3);
	timer_add(&gg_plugin, buf, 180, 0, gg_ping_timer_handler, xstrdup(s->uid));

 	descr = xstrdup(session_descr_get(s));
        status = session_status_get(s);

	gg_iso_to_cp(descr);

        /* ustawiamy sw�j status */
        if (s->descr) {
                int _status = gg_text_to_status(status, descr);

                _status = GG_S(_status);
                if (session_int_get(s, "private")) 
                        _status |= GG_STATUS_FRIENDS_MASK;

               gg_change_status_descr(g->sess, _status, descr);
        } else {
                int _status = gg_text_to_status(status, NULL);

                _status = GG_S(_status);
                if (session_int_get(s, "private")) 
                        _status |= GG_STATUS_FRIENDS_MASK;

                gg_change_status(g->sess, _status);
        }

}

/*
 * gg_session_handler_failure()
 *
 * obs�uga nieudanego po��czenia.
 */
static void gg_session_handler_failure(session_t *s, struct gg_event *e)
{
	const char *reason = "conn_failed_unknown";
	gg_private_t *g = session_private_get(s);

	switch (e->event.failure) {
		case GG_FAILURE_CONNECTING:
			reason = "conn_failed_connecting";
			break;
		case GG_FAILURE_INVALID:
			reason = "conn_failed_invalid";
			break;
		case GG_FAILURE_READING:
			reason = "conn_failed_disconnected";
			break;
		case GG_FAILURE_WRITING:
			reason = "conn_failed_disconnected";
			break;
		case GG_FAILURE_PASSWORD:
			reason = "conn_failed_password";
			break;
		case GG_FAILURE_404:
			reason = "conn_failed_404";
			break;
#ifdef __GG_LIBGADU_HAVE_OPENSSL
		case GG_FAILURE_TLS:
			reason = "conn_failed_tls";
			break;
#endif
		default:
			break;
	}

	if (session_int_get(s, "connection_save") == 1) {
		session_set(s, "server", NULL);
		session_int_set(s, "port", GG_DEFAULT_PORT);
	}
			

	gg_free_session(g->sess);
	g->sess = NULL;

	{
		char *__session = xstrdup(session_uid_get(s));
		char *__reason = xstrdup(format_find(reason));
		int __type = EKG_DISCONNECT_FAILURE;

		query_emit(NULL, "protocol-disconnected", &__session, &__reason, &__type, NULL);

		xfree(__reason);
		xfree(__session);
	}
}

/*
 * gg_session_handler_disconnect()
 *
 * obs�uga roz��czenia z powodu pod��czenia drugiej sesji.
 */
static void gg_session_handler_disconnect(session_t *s)
{
	char *__session = xstrdup(session_uid_get(s));
	char *__reason = NULL;
	int __type = EKG_DISCONNECT_FORCED;
	gg_private_t *g = session_private_get(s);
	
	session_connected_set(s, 0);
		
	query_emit(NULL, "protocol-disconnected", &__session, &__reason, &__type, NULL);

	xfree(__session);
	xfree(__reason);

	gg_logoff(g->sess);		/* zamknie gniazdo */
	gg_free_session(g->sess);
	g->sess = NULL;
}

/*
 * gg_session_handler_status()
 *
 * obs�uga zmiany stanu przez u�ytkownika.
 */
static void gg_session_handler_status(session_t *s, uin_t uin, int status, const char *descr, uint32_t ip, uint16_t port)
{
	char *__session, *__uid, *__status, *__descr, *__host = NULL;
	int __port = 0;
	time_t when = time(NULL);

	__session = xstrdup(session_uid_get(s));
	__uid = saprintf("gg:%d", uin);
	__status = xstrdup(gg_status_to_text(status));
	__descr = xstrdup(descr);
	gg_cp_to_iso(__descr);

	if (ip)
		__host = xstrdup(inet_ntoa(*((struct in_addr*)(&ip))));

	__port = port;

	query_emit(NULL, "protocol-status", &__session, &__uid, &__status, &__descr, &__host, &__port, &when, NULL);

	xfree(__host);
	xfree(__descr);
	xfree(__status);
	xfree(__uid);
	xfree(__session);
}

/*
 * gg_session_handler_msg()
 *
 * obs�uga przychodz�cych wiadomo�ci.
 */
void gg_session_handler_msg(session_t *s, struct gg_event *e)
{
	char *__session, *__sender, *__text, *__format, *__seq, **__rcpts = NULL;
	int i, __class = 0;
	int ekgbeep = EKG_TRY_BEEP;
	time_t __sent;

	__session = xstrdup(session_uid_get(s));
	__sender = saprintf("gg:%d", e->event.msg.sender);
	__text = xstrdup(e->event.msg.message);
	gg_cp_to_iso(__text);
	__sent = e->event.msg.time;
	__seq = NULL;
	__format = NULL;
	__class = EKG_MSGCLASS_CHAT;

	if ((e->event.msg.msgclass & 0x0f) == GG_CLASS_CHAT || (e->event.msg.msgclass & GG_CLASS_QUEUED))
		__class = EKG_MSGCLASS_CHAT;

	if (gg_config_dcc && (e->event.msg.msgclass & GG_CLASS_CTCP)) {
		char *__host = NULL, uid[16];
		int __port = -1, __valid = 1;
		struct gg_dcc *d;
		userlist_t *u;
		watch_t *w;

		snprintf(uid, sizeof(uid), "gg:%d", e->event.msg.sender);

		if (!(u = userlist_find(s, uid)))
			return;

		query_emit(NULL, "protocol-dcc-validate", &__host, &__port, &__valid, NULL);
		xfree(__host);

		if (!__valid) {
			char *tmp = saprintf("/ignore %s", uid);
			print_status("dcc_attack", format_user(s, uid));
			command_exec(NULL, s, tmp, 0);
			xfree(tmp);

			return;
		}

		if (!(d = gg_dcc_get_file(u->ip, u->port, atoi(session_uid_get(s) + 3), e->event.msg.sender))) {
			print_status("dcc_error", strerror(errno));
			return;
		}

		w = watch_add(&gg_plugin, d->fd, d->check, 0, gg_dcc_handler, d);
		watch_timeout_set(w, d->timeout);

		return;
	}

	if (e->event.msg.msgclass & GG_CLASS_CTCP)
		return;

	if (e->event.msg.sender == 0)
		__class = EKG_MSGCLASS_SYSTEM;

	for (i = 0; i < e->event.msg.recipients_count; i++)
		array_add(&__rcpts, saprintf("gg:%d", e->event.msg.recipients[i]));
	
	if (e->event.msg.formats && e->event.msg.formats_length) {
		unsigned char *p = e->event.msg.formats;
		int i, len = xstrlen(__text);
		
		__format = xmalloc(len * sizeof(uint32_t));

		for (i = 0; i < e->event.msg.formats_length; ) {
			int j, pos = p[i] + p[i + 1] * 256;
			uint32_t val = 0;
			
			if ((p[i + 3] & GG_FONT_BOLD))
				val |= EKG_FORMAT_BOLD;

			if ((p[i + 3] & GG_FONT_ITALIC))
				val |= EKG_FORMAT_ITALIC;
			
			if ((p[i + 3] & GG_FONT_UNDERLINE))
				val |= EKG_FORMAT_UNDERLINE;
			
			if ((p[i + 3] & GG_FONT_COLOR)) {
				val |= EKG_FORMAT_COLOR | p[i + 4] | (p[i + 5] << 8) | (p[i + 6] << 16);
				i += 3;
			}

			i += 3;

			for (j = pos; j < len; j++)
				__format[j] = val;
		}
	}
				
	query_emit(NULL, "protocol-message", &__session, &__sender, &__rcpts, &__text, &__format, &__sent, &__class, &__seq, &ekgbeep, NULL);

	xfree(__seq);
	xfree(__text);
	xfree(__sender);
	xfree(__session);
	xfree(__format);
}

/*
 * gg_session_handler_ack()
 *
 * obs�uga potwierdze� wiadomo�ci.
 */
static void gg_session_handler_ack(session_t *s, struct gg_event *e)
{
	char *__session, *__rcpt, *__seq, *__status;

	__session = xstrdup(session_uid_get(s));
	__rcpt = saprintf("gg:%d", e->event.ack.recipient);
	__seq = strdup(itoa(e->event.ack.seq));

	switch (e->event.ack.status) {
		case GG_ACK_DELIVERED:
			__status = xstrdup(EKG_ACK_DELIVERED);
			break;
		case GG_ACK_QUEUED:
			__status = xstrdup(EKG_ACK_QUEUED);
			break;
		case GG_ACK_NOT_DELIVERED:
			__status = xstrdup(EKG_ACK_DROPPED);
			break;
		default:
			debug("[gg] unknown message ack status. consider upgrade\n");
			__status = xstrdup(EKG_ACK_UNKNOWN);
			break;
	}

	query_emit(NULL, "protocol-message-ack", &__session, &__rcpt, &__seq, &__status, NULL);
	
	xfree(__status);
	xfree(__seq);
	xfree(__rcpt);
	xfree(__session);
}

/*
 * gg_reconnect_handler()
 *
 * obs�uga powt�rnego po��czenia
 */

void gg_reconnect_handler(int type, void *data)
{
	session_t* s = session_find((char*) data);
	char *tmp;

	if (!s || session_connected_get(s) == 1)
		return;
	
	tmp = xstrdup("/connect");
	command_exec(NULL, s, tmp, 0);
	xfree(tmp);
}

/*
 * gg_session_handler_userlist()
 *
 * support for userlist's events 
 *
 */
static void gg_session_handler_userlist(session_t *s, struct gg_event *e)
{
        switch (e->event.userlist.type) {
                case GG_USERLIST_GET_REPLY:
                {
	                print("userlist_get_ok");

                        if (e->event.userlist.reply) {
				list_t l;
				gg_private_t *g = session_private_get(s);

                                /* remove all contacts from notification list on server */
				for (l = s->userlist; l; l = l->next) {
                                        userlist_t *u = l->data;
					char *parsed;
					
					if (!u || !(parsed = xstrchr(u->uid, ':')))
						continue;

                                        gg_remove_notify_ex(g->sess, str_to_uin(parsed + 1), gg_userlist_type(u));
                                }

                                gg_cp_to_iso(e->event.userlist.reply);
				userlist_set(s, e->event.userlist.reply);
		                gg_userlist_send(g->sess, s->userlist);

                                config_changed = 1;
                        }

                        break;
                }

                case GG_USERLIST_PUT_REPLY:
                {
                        switch (gg_userlist_put_config) {
                                case 0:
                                        print("userlist_put_ok");
                                        break;
                                case 1:
                                        print("userlist_config_put_ok");
                                        break;
                                case 2:
                                        print("userlist_clear_ok");
                                        break;
                                case 3:
                                        print("userlist_config_clear_ok");
                                        break;
                        }
                        break;
                }
        }
}

/*
 * gg_session_handler()
 *
 * obs�uga zdarze� przy po��czeniu gg.
 */
void gg_session_handler(int type, int fd, int watch, void *data)
{
	gg_private_t *g = session_private_get((session_t*) data);
	struct gg_event *e;
	int broken = 0;

	if (type == 1) {
		/* tutaj powinni�my usun�� dane watcha. nie, dzi�kuj�. */
		return;
	}

	if (!g || !g->sess) {
		debug("[gg] gg_session_handler() called with NULL gg_session\n");
		return;
	}

	if (type == 2) {
		if (g->sess->state != GG_STATE_CONNECTING_GG) {
	                int reconnect_delay;
			session_t *s = (session_t*) data;
			print("conn_timeout", session_name(s));

		        reconnect_delay = session_int_get((session_t*) data, "auto_reconnect");
                	if (reconnect_delay && reconnect_delay != -1)
                        	timer_add(&gg_plugin, "reconnect", reconnect_delay, 0, gg_reconnect_handler, xstrdup(((session_t*) data)->uid));

			gg_free_session(g->sess);
			g->sess = NULL;
			return;
		}

		/* je�li jest GG_STATE_CONNECTING_GG to ka�emy stwierdzi�
		 * b��d (EINPROGRESS) i ��czy� si� z kolejnym kandydatem. */
	}

	if (!(e = gg_watch_fd(g->sess))) {
		char *__session = xstrdup(session_uid_get((session_t*) data));
		char *__reason = NULL;
		int __type = EKG_DISCONNECT_NETWORK;
		int reconnect_delay;

		session_connected_set((session_t*) data, 0);
		
		query_emit(NULL, "protocol-disconnected", &__session, &__reason, &__type, NULL);
			
		xfree(__reason);
		xfree(__session);

		gg_free_session(g->sess);
		g->sess = NULL;
		
		reconnect_delay = session_int_get((session_t*) data, "auto_reconnect");
		if (reconnect_delay && reconnect_delay != -1)
			timer_add(&gg_plugin, "reconnect", reconnect_delay, 0, gg_reconnect_handler, xstrdup(((session_t*)data)->uid));

		return;
	}

	switch (e->type) {
		case GG_EVENT_NONE:
			break;

		case GG_EVENT_CONN_SUCCESS:
			gg_session_handler_success(data);
			break;
			
		case GG_EVENT_CONN_FAILED:
			gg_session_handler_failure(data, e);
			broken = 1;
			break;

		case GG_EVENT_DISCONNECT:
			gg_session_handler_disconnect(data);
			broken = 1;
			break;

		case GG_EVENT_NOTIFY:
		case GG_EVENT_NOTIFY_DESCR:
		{
			struct gg_notify_reply *n;

			n = (e->type == GG_EVENT_NOTIFY) ? e->event.notify : e->event.notify_descr.notify;

			for (; n->uin; n++) {
				char *descr = (e->type == GG_EVENT_NOTIFY_DESCR) ? e->event.notify_descr.descr : NULL;

				gg_session_handler_status(data, n->uin, n->status, descr, n->remote_ip, n->remote_port);
			}
			
			break;
		}
			
		case GG_EVENT_STATUS:
			gg_session_handler_status(data, e->event.status.uin, e->event.status.status, e->event.status.descr, 0, 0);
			break;

#ifdef GG_STATUS60
		case GG_EVENT_STATUS60:
			gg_session_handler_status(data, e->event.status60.uin, e->event.status60.status, e->event.status60.descr, e->event.status60.remote_ip, e->event.status60.remote_port);
			break;
#endif

#ifdef GG_NOTIFY_REPLY60
		case GG_EVENT_NOTIFY60:
		{
			int i;

			for (i = 0; e->event.notify60[i].uin; i++)
				gg_session_handler_status(data, e->event.notify60[i].uin, e->event.notify60[i].status, e->event.notify60[i].descr, e->event.notify60[i].remote_ip, e->event.notify60[i].remote_port);

			break;
		}
#endif

		case GG_EVENT_MSG:
			gg_session_handler_msg(data, e);
			break;

		case GG_EVENT_ACK:
			gg_session_handler_ack(data, e);
			break;

		case GG_EVENT_PUBDIR50_SEARCH_REPLY:
			gg_session_handler_search50(data, e);
			break;

		case GG_EVENT_PUBDIR50_WRITE:
			gg_session_handler_change50(data, e);
			break;

		case GG_EVENT_USERLIST:
			gg_session_handler_userlist(data, e);
			break;

		default:
			debug("[gg] unhandled event 0x%.4x, consider upgrade\n", e->type);
	}

	if (!broken && g->sess->state != GG_STATE_IDLE && g->sess->state != GG_STATE_ERROR) {
		watch_t *w = watch_add(&gg_plugin, g->sess->fd, g->sess->check, 0, gg_session_handler, data);
		watch_timeout_set(w, g->sess->timeout);
	}

	gg_event_free(e);
}

void gg_changed_private(session_t *s, const char *var)
{
	gg_private_t *g = (s) ? session_private_get(s) : NULL;
	const char *status = session_status_get(s);
	char *descr = xstrdup(session_descr_get(s));

	if (!session_connected_get(s))
		return;

	gg_iso_to_cp(descr);

	
        if (s->descr) {
                int _status = gg_text_to_status(status, descr);

                _status = GG_S(_status);
                if (session_int_get(s, "private"))
                        _status |= GG_STATUS_FRIENDS_MASK;

               gg_change_status_descr(g->sess, _status, descr);
        } else {
                int _status = gg_text_to_status(status, NULL);

                _status = GG_S(_status);
                if (session_int_get(s, "private"))
                        _status |= GG_STATUS_FRIENDS_MASK;

                gg_change_status(g->sess, _status);
        }
}

/*
 * changed_proxy()
 *
 * funkcja wywo�ywana przy zmianie warto�ci zmiennej ,,gg:proxy''.
 */
void gg_changed_proxy(session_t *s, const char *var)
{
	char **auth, **userpass = NULL, **hostport = NULL;
	const char *gg_config_proxy;
	
	gg_proxy_port = 0;
	xfree(gg_proxy_host);
	gg_proxy_host = NULL;
	xfree(gg_proxy_username);
	gg_proxy_username = NULL;
	xfree(gg_proxy_password);
	gg_proxy_password = NULL;
	gg_proxy_enabled = 0;	

	if (!(gg_config_proxy = session_get(s, var)))
		return;

	auth = array_make(gg_config_proxy, "@", 0, 0, 0);

	if (!auth[0] || !xstrcmp(auth[0], "")) {
		array_free(auth);
		return; 
	}
	
	gg_proxy_enabled = 1;

	if (auth[0] && auth[1]) {
		userpass = array_make(auth[0], ":", 0, 0, 0);
		hostport = array_make(auth[1], ":", 0, 0, 0);
	} else
		hostport = array_make(auth[0], ":", 0, 0, 0);
	
	if (userpass && userpass[0] && userpass[1]) {
		gg_proxy_username = xstrdup(userpass[0]);
		gg_proxy_password = xstrdup(userpass[1]);
	}

	gg_proxy_host = xstrdup(hostport[0]);
	gg_proxy_port = (hostport[1]) ? atoi(hostport[1]) : 8080;

	array_free(hostport);
	array_free(userpass);
	array_free(auth);
}


static int gg_theme_init()
{
        /* pobieranie tokenu */
        format_add("gg_token", _("%> Token was written to the file %T%1%n\n"), 1);
        format_add("gg_token_ocr", _("%> Token: %T%1%n\n"), 1);
        format_add("gg_token_body", "%1\n", 1);
        format_add("gg_token_failed", _("%! Error when token was getting: %1\n"), 1);
        format_add("gg_token_timeout", _("%! Token getting time ran out\n"), 1);
        format_add("gg_token_unsupported", _("%! Your operating system doesn't support tokens\n"), 1);
        format_add("gg_token_missing", _("%! First get token by function %Ttoken%n\n"), 1);
	
	return 0;
}

void gg_setvar_default() 
{
	xfree(gg_config_dcc_dir);
	xfree(gg_config_dcc_ip);
	xfree(gg_config_dcc_limit);

	gg_config_display_token = 1;
	gg_config_dcc = 0;
	gg_config_dcc_dir = NULL;
	gg_config_dcc_ip = NULL;
	gg_config_dcc_limit = xstrdup("30/30");
	gg_config_dcc_port = 1550;
}

int gg_plugin_init()
{
	list_t l;

	plugin_register(&gg_plugin);

	gg_setvar_default();

	query_connect(&gg_plugin, "set-vars-default", gg_setvar_default, NULL);
	query_connect(&gg_plugin, "protocol-validate-uid", gg_validate_uid, NULL);
	query_connect(&gg_plugin, "plugin-print-version", gg_print_version, NULL);
	query_connect(&gg_plugin, "session-added", gg_session_handle, (void *)1);
	query_connect(&gg_plugin, "session-removed", gg_session_handle, (void *)0);
	query_connect(&gg_plugin, "add-notify", gg_add_notify_handle, NULL);
	query_connect(&gg_plugin, "remove-notify", gg_remove_notify_handle, NULL);
	query_connect(&gg_plugin, "status-show", gg_status_show_handle, NULL);
	query_connect(&gg_plugin, "user-offline", gg_user_offline_handle, NULL);
	query_connect(&gg_plugin, "user-online", gg_user_online_handle, NULL);
        query_connect(&gg_plugin, "protocol-unignore", gg_user_online_handle, (void *)1);
	query_connect(&gg_plugin, "userlist-added", gg_userlist_added_handle, NULL);

	gg_register_commands();
	
        variable_add(&gg_plugin, "display_token", VAR_BOOL, 1, &gg_config_display_token, NULL, NULL, NULL);
	variable_add(&gg_plugin, "dcc", VAR_BOOL, 1, &gg_config_dcc, gg_changed_dcc, NULL, NULL);
	variable_add(&gg_plugin, "dcc_dir", VAR_STR, 1, &gg_config_dcc_dir, NULL, NULL, NULL);
	variable_add(&gg_plugin, "dcc_ip", VAR_STR, 1, &gg_config_dcc_ip, gg_changed_dcc, NULL, NULL);
	variable_add(&gg_plugin, "dcc_limit", VAR_STR, 1, &gg_config_dcc_limit, NULL, NULL, NULL);
	variable_add(&gg_plugin, "dcc_port", VAR_INT, 1, &gg_config_dcc_port, gg_changed_dcc, NULL, NULL);

	plugin_var_add(&gg_plugin, "alias", VAR_STR, 0, 0, NULL);
	plugin_var_add(&gg_plugin, "auto_away", VAR_INT, "600", 0, NULL);
	plugin_var_add(&gg_plugin, "auto_back", VAR_INT, "0", 0, NULL);	
        plugin_var_add(&gg_plugin, "auto_connect", VAR_BOOL, "0", 0, NULL);
        plugin_var_add(&gg_plugin, "auto_find", VAR_INT, "0", 0, NULL);
        plugin_var_add(&gg_plugin, "auto_reconnect", VAR_INT, "10", 0, NULL);
        plugin_var_add(&gg_plugin, "connection_save", VAR_INT, "0", 0, NULL);
	plugin_var_add(&gg_plugin, "default", VAR_BOOL, "0", 0, changed_var_default);
        plugin_var_add(&gg_plugin, "display_notify", VAR_INT, "-1", 0, NULL);
        plugin_var_add(&gg_plugin, "local_ip", VAR_STR, 0, 0, NULL);
	plugin_var_add(&gg_plugin, "log_formats", VAR_STR, "xml,simple", 0, NULL);
        plugin_var_add(&gg_plugin, "password", VAR_STR, "foo", 1, NULL);
        plugin_var_add(&gg_plugin, "port", VAR_INT, "8074", 0, NULL);
	plugin_var_add(&gg_plugin, "proxy", VAR_STR, 0, 0, gg_changed_proxy);
	plugin_var_add(&gg_plugin, "proxy_forwarding", VAR_STR, 0, 0, NULL);
	plugin_var_add(&gg_plugin, "private", VAR_BOOL, "0", 0, gg_changed_private);
	plugin_var_add(&gg_plugin, "server", VAR_STR, 0, 0, NULL);

	gg_debug_handler = ekg_debug_handler;
	gg_debug_level = 255;

	for (l = sessions; l; l = l->next)
		gg_private_init((session_t*) l->data);
	
	return 0;
}

static int gg_plugin_destroy()
{
	list_t l;

	for (l = gg_reminds; l; l = l->next) {
		struct gg_http *h = l->data;

		watch_remove(&gg_plugin, h->fd, h->check);
		gg_pubdir_free(h);
	}

	for (l = gg_registers; l; l = l->next) {
		struct gg_http *h = l->data;

		watch_remove(&gg_plugin, h->fd, h->check);
		gg_pubdir_free(h);
	}

	for (l = gg_unregisters; l; l = l->next) {
		struct gg_http *h = l->data;

		watch_remove(&gg_plugin, h->fd, h->check);
		gg_pubdir_free(h);
	}

	for (l = gg_userlists; l; l = l->next) {
		struct gg_http *h = l->data;

		watch_remove(&gg_plugin, h->fd, h->check);
		gg_pubdir_free(h);
	}

	xfree(gg_register_password);
	gg_register_password = NULL;
	xfree(gg_register_email);
	gg_register_email = NULL;

	for (l = sessions; l; l = l->next)
		gg_private_destroy((session_t*) l->data);

	plugin_unregister(&gg_plugin);

	return 0;
}

/*
 * userlist_read()
 *
 * wczytuje list� kontakt�w z pliku ~/.ekg/gg:NUMER-userlist w postaci eksportu
 * tekstowego listy kontakt�w windzianego klienta.
 *
 * 0/-1
 */
int userlist_read(session_t *session)
{
        const char *filename;
        char *buf;
        FILE *f;
        char *tmp=saprintf("%s-userlist", session->uid);

        if (!(filename = prepare_path(tmp, 0))) {
                xfree(tmp);
                return -1;
        }       
        xfree(tmp);
        
        if (!(f = fopen(filename, "r")))
                return -1;
                        
        while ((buf = read_file(f))) {
                userlist_t u;

                memset(&u, 0, sizeof(u));
                        
                if (buf[0] == '#' || (buf[0] == '/' && buf[1] == '/')) {
                        xfree(buf);
                        continue;
                }
                
                userlist_add_entry(session,buf);
        
                xfree(buf);
        }

        fclose(f);
                
        return 0;
} 


/*
 * Local Variables:
 * mode: c
 * c-file-style: "k&r"
 * c-basic-offset: 8
 * indent-tabs-mode: t
 * End:
 */
