/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2001-2003, Ximian, Inc.
 */

#include <stdio.h>
#include <stdlib.h>

#include <libsoup/soup.h>

static SoupSession *session;
static GMainLoop *loop;
static gboolean debug, head, quiet;

static void
get_url (const char *url)
{
	const char *name;
	SoupMessage *msg;
	const char *header;

	msg = soup_message_new (head ? "HEAD" : "GET", url);
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);

	soup_session_send_message (session, msg);

	name = soup_message_get_uri (msg)->path;

	if (debug || head) {
		SoupMessageHeadersIter iter;
		const char *hname, *value;
		char *path = soup_uri_to_string (soup_message_get_uri (msg), TRUE);

		g_print ("%s %s HTTP/1.%d\n", msg->method, path,
			 soup_message_get_http_version (msg));
		soup_message_headers_iter_init (&iter, msg->request_headers);
		while (soup_message_headers_iter_next (&iter, &hname, &value))
			g_print ("%s: %s\r\n", hname, value);
		g_print ("\n");

		g_print ("HTTP/1.%d %d %s\n",
			 soup_message_get_http_version (msg),
			 msg->status_code, msg->reason_phrase);

		soup_message_headers_iter_init (&iter, msg->response_headers);
		while (soup_message_headers_iter_next (&iter, &hname, &value))
			g_print ("%s: %s\r\n", hname, value);
		g_print ("\n");
	} else if (msg->status_code == SOUP_STATUS_SSL_FAILED) {
		GTlsCertificateFlags flags;

		if (soup_message_get_https_status (msg, NULL, &flags))
			g_print ("%s: %d %s (0x%x)\n", name, msg->status_code, msg->reason_phrase, flags);
		else
			g_print ("%s: %d %s (no handshake status)\n", name, msg->status_code, msg->reason_phrase);
	} else if (!quiet || SOUP_STATUS_IS_TRANSPORT_ERROR (msg->status_code))
		g_print ("%s: %d %s\n", name, msg->status_code, msg->reason_phrase);

	if (SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
		header = soup_message_headers_get_one (msg->response_headers,
						       "Location");
		if (header) {
			SoupURI *uri;
			char *uri_string;

			if (!debug && !quiet)
				g_print ("  -> %s\n", header);

			uri = soup_uri_new_with_base (soup_message_get_uri (msg), header);
			uri_string = soup_uri_to_string (uri, FALSE);
			get_url (uri_string);
			g_free (uri_string);
			soup_uri_free (uri);
		}
	} else if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		fwrite (msg->response_body->data, 1,
			msg->response_body->length, stdout);
	}
}

static const char *ca_file, *proxy;
static gboolean synchronous, ntlm;

static GOptionEntry entries[] = {
	{ "ca-file", 'c', 0,
	  G_OPTION_ARG_STRING, &ca_file,
	  "Use FILE as the TLS CA file", "FILE" },
	{ "debug", 'd', 0,
	  G_OPTION_ARG_NONE, &debug,
	  "Show HTTP headers", NULL },
	{ "head", 'h', 0,
	  G_OPTION_ARG_NONE, &head,
	  "Do HEAD rather than GET", NULL },
	{ "ntlm", 'n', 0,
	  G_OPTION_ARG_NONE, &ntlm,
	  "Use NTLM authentication", NULL },
	{ "proxy", 'p', 0,
	  G_OPTION_ARG_STRING, &proxy,
	  "Use URL as an HTTP proxy", "URL" },
	{ "quiet", 'q', 0,
	  G_OPTION_ARG_NONE, &quiet,
	  "Don't show HTTP status code", NULL },
	{ "sync", 's', 0,
	  G_OPTION_ARG_NONE, &synchronous,
	  "Use SoupSessionSync rather than SoupSessionAsync", NULL },
	{ NULL }
};

int
main (int argc, char **argv)
{
	GOptionContext *opts;
	const char *url;
	SoupURI *proxy_uri, *parsed;
	GError *error = NULL;

	opts = g_option_context_new (NULL);
	g_option_context_add_main_entries (opts, entries, NULL);
	if (!g_option_context_parse (opts, &argc, &argv, &error)) {
		g_printerr ("Could not parse arguments: %s\n",
			    error->message);
		g_printerr ("%s",
			    g_option_context_get_help (opts, TRUE, NULL));
		exit (1);
	}

	if (argc != 2) {
		g_printerr ("%s",
			    g_option_context_get_help (opts, TRUE, NULL));
		exit (1);
	}
	g_option_context_free (opts);

	url = argv[1];
	parsed = soup_uri_new (url);
	if (!parsed) {
		g_printerr ("Could not parse '%s' as a URL\n", url);
		exit (1);
	}
	soup_uri_free (parsed);

	session = g_object_new (synchronous ? SOUP_TYPE_SESSION_SYNC : SOUP_TYPE_SESSION_ASYNC,
				SOUP_SESSION_SSL_CA_FILE, ca_file,
				SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_CONTENT_DECODER,
				SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_COOKIE_JAR,
				SOUP_SESSION_USER_AGENT, "get ",
				SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
				SOUP_SESSION_USE_NTLM, ntlm,
				NULL);

	if (proxy) {
		proxy_uri = soup_uri_new (proxy);
		if (!proxy_uri) {
			g_printerr ("Could not parse '%s' as URI\n",
				    proxy);
			exit (1);
		}

		g_object_set (G_OBJECT (session), 
			      SOUP_SESSION_PROXY_URI, proxy_uri,
			      NULL);
		soup_uri_free (proxy_uri);
	} else
		soup_session_add_feature_by_type (session, SOUP_TYPE_PROXY_RESOLVER_DEFAULT);

	if (!synchronous)
		loop = g_main_loop_new (NULL, TRUE);

	get_url (url);

	if (!synchronous)
		g_main_loop_unref (loop);

	return 0;
}