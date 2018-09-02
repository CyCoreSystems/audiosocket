/*
 * app_audiosocket
 *
 * Copyright (C) 2018, CyCore Systems, Inc.
 *
 * Seán C McCord <scm@cycoresys.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief AudioSocket application -- transmit and receive audio through a TCP socket
 *
 * \author Seán C McCord <scm@cycoresys.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "errno.h"
#include <uuid/uuid.h>

#include "asterisk/file.h"
#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/app.h"
#include "asterisk/uuid.h"
#include "asterisk/utils.h"
#include "asterisk/format_cache.h"

#define AST_MODULE "app_audiosocket"
//#define AST_MODULE_SELF "app_audiosocket"
#define AUDIOSOCKET_CONFIG "audiosocket.conf"
#define MAX_CONNECT_TIMEOUT_MSEC 2000

/*** DOCUMENTATION
	<application name="AudioSocket" language="en_US">
		<synopsis>
			Transmit and receive audio between channel and TCP socket
		</synopsis>
		<syntax>
			<parameter name="uuid" required="true">
            <para>UUID is the universally-unique identifier of the call for the audio socket service.  This ID must conform to the string form of a standard UUID.</para>
			</parameter>
      </syntax>
		<syntax>
			<parameter name="service" required="true">
            <para>Service is the name or IP address and port number of the audio socket service to which this call should be connected.  This should be in the form host:port, such as myserver:9019 </para>
			</parameter>
      </syntax>
		<description>
			<para>Connects to the given TCP service, then transmits channel audio over that socket.  In turn, audio is received from the socket and sent to the channel.  Only audio frames will be transmitted.</para>
         <para>Protocol is specified at https://github.com/CyCoreSystems/audiosocket/.</para>
			<para>This application does not automatically answer and should generally be
			preceeded by an application such as Answer() or Progress().</para>
		</description>
	</application>
 ***/

static const char app[] = "AudioSocket";

static int audiosocket_exec(struct ast_channel *chan, const char *data);
static int handle_audiosocket_connection(const char *server, const struct ast_sockaddr addr, const int netsockfd);
static int audiosocket_init(const uuid_t id, const int svc);
static int audiosocket_send_frame(const int svc, const struct ast_frame *f);
static int audiosocket_forward_frame(const int svc, struct ast_channel *chan);
static int audiosocket_run(struct ast_channel *chan, const uuid_t id, const int svc);

static int audiosocket_exec(struct ast_channel *chan, const char *data)
{
   char *parse;

   AST_DECLARE_APP_ARGS(args,
         AST_APP_ARG(uuid);
         AST_APP_ARG(service);
   );

   int s = 0;
   struct ast_sockaddr *addrs;
   int num_addrs = 0, i = 0;
   uuid_t id;

   /* Parse and validate arguments */
   parse = ast_strdupa(data);
   AST_STANDARD_APP_ARGS(args, parse);
   if (ast_strlen_zero(args.uuid)) {
      ast_log(LOG_ERROR, "UUID is required");
      return -1;
   }
   if (uuid_parse(args.uuid, id)) {
      ast_log(LOG_ERROR, "Failed to parse UUID");
      return -1;
   }
   if (!(num_addrs = ast_sockaddr_resolve(&addrs, args.service, PARSE_PORT_REQUIRE, AST_AF_UNSPEC))) {
      ast_log(LOG_ERROR, "Failed to resolve service");
      return -1;
   }

   /* Connect to AudioSocket service */
   for (i = 0; i < num_addrs; i++) {
		if (!ast_sockaddr_port(&addrs[i])) {
         ast_log(LOG_WARNING, "No port provided");
         continue;
		}

		if ((s = socket(addrs[i].ss.ss_family, SOCK_STREAM, IPPROTO_TCP)) < 0) {
			ast_log(LOG_WARNING, "Unable to create socket: %s\n", strerror(errno));
			continue;
		}

		if (ast_fd_set_flags(s, O_NONBLOCK)) {
			close(s);
			continue;
		}

      if (ast_connect(s, &addrs[i]) && errno == EINPROGRESS) {

			if (handle_audiosocket_connection(args.service, addrs[i], s)) {
				close(s);
				continue;
			}

      } else {
         ast_log(LOG_ERROR, "Connection to %s failed with unexpected error: %s\n",
               ast_sockaddr_stringify(&addrs[i]), strerror(errno));
      }

      break;
   }
   ast_free(addrs);

   if (i == num_addrs) {
      ast_log(LOG_ERROR, "Failed to connect to service");
      return -1;
   }

   ast_verbose("running audiosocket\n");
   audiosocket_run(chan, id, s);
   close(s);
   ast_verbose("exiting audiosocket\n");

   return 0;
}

/*!
 * \internal
 * \brief Handle the connection that was started by launch_netscript.
 *
 * \param server Url that we are trying to connect to.
 * \param addr Address that host was resolved to.
 * \param netsockfd File descriptor of socket.
 *
 * \retval 0 when connection is succesful.
 * \retval 1 when there is an error.
 */
static int handle_audiosocket_connection(const char *server, const struct ast_sockaddr addr, const int netsockfd)
{
	struct pollfd pfds[1];
	int res, conresult;
	socklen_t reslen;

	reslen = sizeof(conresult);

	pfds[0].fd = netsockfd;
	pfds[0].events = POLLOUT;

	while ((res = ast_poll(pfds, 1, MAX_CONNECT_TIMEOUT_MSEC)) != 1) {
		if (errno != EINTR) {
			if (!res) {
				ast_log(LOG_WARNING, "AudioSocket connection to '%s' timed out after MAX_CONNECT_TIMEOUT_MSEC (%d) milliseconds.\n",
					server, MAX_CONNECT_TIMEOUT_MSEC);
			} else {
				ast_log(LOG_WARNING, "Connect to '%s' failed: %s\n", server, strerror(errno));
			}

			return 1;
		}
	}

	if (getsockopt(pfds[0].fd, SOL_SOCKET, SO_ERROR, &conresult, &reslen) < 0) {
		ast_log(LOG_WARNING, "Connection to %s failed with error: %s\n",
			ast_sockaddr_stringify(&addr), strerror(errno));
		return 1;
	}

	if (conresult) {
		ast_log(LOG_WARNING, "Connecting to '%s' failed for url '%s': %s\n",
			ast_sockaddr_stringify(&addr), server, strerror(conresult));
		return 1;
	}

	return 0;
}

static int audiosocket_init(const uuid_t id, const int svc) {
   int ret = 0;
   uint8_t *buf = ast_malloc(3+16);

   buf[0] = 0x01;
   buf[1] = 0x00;
   buf[2] = 0x10;
   memcpy(buf+3, id, 16);

   if(write(svc, buf, 3+16) != 3+16) {
      ast_log(LOG_WARNING, "Failed to write data to audiosocket");
      ret = 1;
   }
   ast_verbose("wrote id packet");

   ast_free(buf);
   return ret;
}

static int audiosocket_send_frame(const int svc, const struct ast_frame *f) {

   int ret = 0;
   uint8_t kind = 0x10;
   uint8_t *buf, *p;

   buf = ast_malloc(3 + f->datalen);
   p = buf;

   *(p++) = kind;
   *(p++) = f->datalen >> 8;
   *(p++) = f->datalen & 0xff;
   memcpy(p, f->data.ptr, f->datalen);

   if(write(svc, buf, 3+f->datalen) != 3+f->datalen) {
      ast_log(LOG_WARNING, "Failed to write data to audiosocket");
      ret = 1;
   }

   ast_free(buf);
   return ret;
}

/*
static uint8_t* pack_uint8(uint8_t* dest, uint8_t src) {
   *(dest++) = src;
   return dest;
}

static uint8_t* pack_uint16be(uint8_t* dest, uint16_t src) {
   *(dest++) = src >> 8;
   *(dest++) = src & 0xff;
   return dest;
}

static uint16_t unpack_uint16be(uint8_t* src) {
   uint16_t out = 0;
   out += src[0] * 256;
   out += src[1];
   return 1;
}
*/

/* audiosocket_forward_frame reads a message from the audiosocket and forwards it to the asterisk channel */
static int audiosocket_forward_frame(const int svc, struct ast_channel *chan) {

   int i = 0, n = 0, ret = 0;;
   int not_audio = 0;
	struct ast_frame f;

   uint8_t kind;
   uint8_t len_high;
   uint8_t len_low;
   uint16_t len = 0;
   uint8_t* data;

   n = read(svc, &kind, 1);
   if (n < 0 && errno == EAGAIN) {
      return 0;
   }
   if (n == 0) {
      return 0;
   }
   if (n != 1) {
      ast_log(LOG_WARNING, "Failed to read type header from audiosocket\n");
      return 1;
   }
   if (kind != 0x10) {
      // read but ignore non-audio message
      ast_log(LOG_WARNING, "Received non-audio audiosocket message\n");
      not_audio = 1;
   }

   n = read(svc, &len_high, 1);
   if (n != 1) {
      ast_log(LOG_WARNING, "Failed to read data length from audiosocket\n");
      return 2;
   }
   len += len_high * 256;
   n = read(svc, &len_low, 1);
   if (n != 1) {
      ast_log(LOG_WARNING, "Failed to read data length from audiosocket\n");
      return 2;
   }
   len += len_low;

   if (len < 1) {
      return 0;
   }

   data = ast_malloc(len);
   ret = 0;
   n = 0;
   i = 0;
   while (i < len) {
      n = read(svc, data+i, len-i);
      if (n < 0) {
         ast_log(LOG_ERROR, "Failed to read data from audiosocket\n");
         ret = n;
         break;
      }
      if (n == 0) {
         ast_log(LOG_ERROR, "Insufficient data read from audiosocket\n");
         ret = -1;
         break;
      }
      i += n;
   }

   if (ret != 0) {
      return ret;
   }

   if(not_audio) {
      return 0;
   }

	f = (struct ast_frame){
		.frametype = AST_FRAME_VOICE,
		.subclass.format = ast_format_slin,
		.src = "AudioSocket",
		.data.ptr = data,
		.datalen = len,
		.samples = len/2,
      .mallocd = AST_MALLOCD_DATA,
	};

   return ast_write(chan, &f);
}

static int audiosocket_run(struct ast_channel *chan, const uuid_t id, const int svc) {

   if (ast_set_write_format(chan, ast_format_slin)) {
      ast_log(LOG_ERROR, "Failed to set write format to SLINEAR\n");
      return 1;
   }
   if (ast_set_read_format(chan, ast_format_slin)) {
      ast_log(LOG_ERROR, "Failed to set read format to SLINEAR\n");
      return 1;
   }

   if (audiosocket_init(id, svc)) {
      return 1;
   }

	while (ast_waitfor(chan, -1) > -1) {

      // Check channel state
      if( ast_channel_state(chan) != AST_STATE_UP ) {
         ast_verbose("Channel hung up\n");
         return 0;
      }

		struct ast_frame *f = ast_read(chan);
		if (f) {
         f->delivery.tv_sec = 0;
         f->delivery.tv_usec = 0;
         if (f->frametype != AST_FRAME_VOICE) {
            ast_verbose("Ignoring non-voice frame\n");
         } else {

            // Send audio frame to audiosocket
            if(audiosocket_send_frame(svc, f)) {
               ast_log(LOG_ERROR, "Failed to forward channel frame to audiosocket\n");
               ast_frfree(f);
               return 1;
            }
         }

         ast_frfree(f);
      }

      // Send audiosocket data to channel
      if(audiosocket_forward_frame(svc, chan)) {
         ast_log(LOG_ERROR, "Failed to forward audiosocket message to channel\n");
         return 1;
      }

	}
	return 0;
}

static int unload_module(void)
{
	return ast_unregister_application(app);
}

static int load_module(void)
{
   return ast_register_application_xml(app, audiosocket_exec);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "AudioSocket Application");
