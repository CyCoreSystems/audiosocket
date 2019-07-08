/*
 * Copyright (C) 2019, CyCore Systems, Inc
 *
 * Seán C McCord <scm@cycoresys.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief AudioSocket support for Asterisk
 *
 * \author Seán C McCord <scm@cycoresys.com>
 *
 */

/*** MODULEINFO
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include "errno.h"
#include <uuid/uuid.h>

#include "asterisk/file.h"
#include "asterisk/res_audiosocket.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/uuid.h"
#include "asterisk/format_cache.h"

#define	MODULE_DESCRIPTION	"AudioSocket support functions for Asterisk"

#define MAX_CONNECT_TIMEOUT_MSEC 2000

static int handle_audiosocket_connection(const char *server, const struct ast_sockaddr addr, const int netsockfd);

const int audiosocket_connect(const char *server) {
   int s = -1;
   struct ast_sockaddr *addrs;
   int num_addrs = 0, i = 0;

   if (ast_strlen_zero(server)) {
      ast_log(LOG_ERROR, "no AudioSocket server provided");
      return -1;
   }
   if (!(num_addrs = ast_sockaddr_resolve(&addrs, server, PARSE_PORT_REQUIRE, AST_AF_UNSPEC))) {
      ast_log(LOG_ERROR, "failed to resolve AudioSocket service");
      return -1;
   }

   /* Connect to AudioSocket service */
   for (i = 0; i < num_addrs; i++) {
		if (!ast_sockaddr_port(&addrs[i])) {
         ast_log(LOG_WARNING, "no port provided");
         continue;
		}

		if ((s = socket(addrs[i].ss.ss_family, SOCK_STREAM, IPPROTO_TCP)) < 0) {
			ast_log(LOG_WARNING, "unable to create socket: %s\n", strerror(errno));
			continue;
		}

		if (ast_fd_set_flags(s, O_NONBLOCK)) {
			close(s);
			continue;
		}

      if (ast_connect(s, &addrs[i]) && errno == EINPROGRESS) {

         ast_verbose("attempting to handle connected socket\n");
			if (handle_audiosocket_connection(server, addrs[i], s)) {
				close(s);
				continue;
			}

      } else {
         ast_log(LOG_ERROR, "connection to %s failed with unexpected error: %s\n",
               ast_sockaddr_stringify(&addrs[i]), strerror(errno));
      }

      break;
   }
   ast_free(addrs);

   if (i == num_addrs) {
      ast_log(LOG_ERROR, "failed to connect to AudioSocket service");
      return -1;
   }

   ast_verbose("connected to AudioSocket\n");
   return s;
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

   ast_verbose("polling AudioSocket connection\n");
	while ((res = ast_poll(pfds, 1, MAX_CONNECT_TIMEOUT_MSEC)) != 1) {
		if (errno != EINTR) {
			if (!res) {
				ast_log(LOG_WARNING, "AudioSocket connection to '%s' timed out after MAX_CONNECT_TIMEOUT_MSEC (%d) milliseconds.\n",
					server, MAX_CONNECT_TIMEOUT_MSEC);
			} else {
				ast_log(LOG_WARNING, "Connect to '%s' failed: %s\n", server, strerror(errno));
			}

			return -1;
		}
	}

   ast_verbose("setting AudioSocket options\n");
	if (getsockopt(pfds[0].fd, SOL_SOCKET, SO_ERROR, &conresult, &reslen) < 0) {
		ast_log(LOG_WARNING, "Connection to %s failed with error: %s\n",
			ast_sockaddr_stringify(&addr), strerror(errno));
		return -1;
	}

   ast_verbose("checking result\n");
	if (conresult) {
		ast_log(LOG_WARNING, "Connecting to '%s' failed for url '%s': %s\n",
			ast_sockaddr_stringify(&addr), server, strerror(conresult));
		return -1;
	}

	return 0;
}

const int audiosocket_init(const int svc, struct ast_uuid *id) {
   uuid_t uu;
   char idBuf[AST_UUID_STR_LEN+1];

   ast_verbose("checking for UUID\n");
   /*
   if (ast_uuid_is_nil(id)) {
      ast_log(LOG_WARNING, "No UUID for AudioSocket");
      return -1;
   }
   */

   // FIXME: this is ridiculous, but we cannot see inside ast_uuid to extract
   // the underlying bytes; thus, we parse the string again.  
   //
   ast_verbose("validating UUID\n");
   if (uuid_parse(ast_uuid_to_str(id, idBuf, AST_UUID_STR_LEN), uu)) {
      ast_log(LOG_ERROR, "Failed to parse UUID");
      return -1;
   }


   int ret = 0;
   uint8_t *buf = ast_malloc(3+16);

   buf[0] = 0x01;
   buf[1] = 0x00;
   buf[2] = 0x10;
   memcpy(buf+3, uu, 16);

   ast_verbose("sending initialization packet\n");
   if (write(svc, buf, 3+16) != 3+16) {
      ast_log(LOG_WARNING, "Failed to write data to audiosocket");
      ret = -1;
   }
   ast_verbose("wrote id packet\n");

   ast_free(buf);
   return ret;
}

const int audiosocket_send_frame(const int svc, const struct ast_frame *f) {

   int ret = 0;
   uint8_t kind = 0x10; // always 16-bit, 8kHz signed linear mono, for now
   uint8_t *buf, *p;

   buf = ast_malloc(3 + f->datalen);
   p = buf;

   *(p++) = kind;
   *(p++) = f->datalen >> 8;
   *(p++) = f->datalen & 0xff;
   memcpy(p, f->data.ptr, f->datalen);

   if(write(svc, buf, 3+f->datalen) != 3+f->datalen) {
      ast_log(LOG_WARNING, "Failed to write data to audiosocket");
      ret = -1;
   }

   ast_free(buf);
   return ret;
}

struct ast_frame *audiosocket_receive_frame(const int svc) {

   int i = 0, n = 0, ret = 0;;
   int not_audio = 0;
	static struct ast_frame f;

   uint8_t kind;
   uint8_t len_high;
   uint8_t len_low;
   uint16_t len = 0;
   uint8_t* data;

   n = read(svc, &kind, 1);
   if (n < 0 && errno == EAGAIN) {
      return &ast_null_frame;
   }
   if (n == 0) {
      return &ast_null_frame;
   }
   if (n != 1) {
      ast_log(LOG_WARNING, "Failed to read type header from audiosocket\n");
      return NULL;
   }
   if (kind != 0x10) {
      // read but ignore non-audio message
      ast_log(LOG_WARNING, "Received non-audio audiosocket message\n");
      not_audio = 1;
   }

   n = read(svc, &len_high, 1);
   if (n != 1) {
      ast_log(LOG_WARNING, "Failed to read data length from audiosocket\n");
      return NULL;
   }
   len += len_high * 256;
   n = read(svc, &len_low, 1);
   if (n != 1) {
      ast_log(LOG_WARNING, "Failed to read data length from audiosocket\n");
      return NULL;
   }
   len += len_low;

   if (len < 1) {
      return &ast_null_frame;
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
      return NULL;
   }

   if(not_audio) {
      return &ast_null_frame;
   }

	f.frametype = AST_FRAME_VOICE;
	f.subclass.format = ast_format_slin;
	f.src = "AudioSocket";
	f.data.ptr = data;
	f.datalen = len;
	f.samples = len/2;
   f.mallocd = AST_MALLOCD_DATA;

   return &f;
}

static int load_module(void)
{
	ast_verb(1, "Loading AudioSocket Support module\n");
   return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
	ast_verb(1, "Unloading AudioSocket Support module\n");
   return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "AudioSocket support",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND,
);
