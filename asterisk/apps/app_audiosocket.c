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
#include "asterisk/res_audiosocket.h"
#include "asterisk/utils.h"
#include "asterisk/format_cache.h"

#define AST_MODULE "app_audiosocket"
//#define AST_MODULE_SELF "app_audiosocket"
#define AUDIOSOCKET_CONFIG "audiosocket.conf"
#define MAX_CONNECT_TIMEOUT_MSEC 2000
#define CHANNEL_INPUT_TIMEOUT_MS 5000

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
static int audiosocket_run(struct ast_channel *chan, struct ast_uuid *id, const int svc);

static int audiosocket_exec(struct ast_channel *chan, const char *data)
{
   char *parse;

   AST_DECLARE_APP_ARGS(args,
         AST_APP_ARG(idStr);
         AST_APP_ARG(server);
   );

   int s = 0;
   struct ast_uuid *id;

   /* Parse and validate arguments */
   parse = ast_strdupa(data);
   AST_STANDARD_APP_ARGS(args, parse);
   if (ast_strlen_zero(args.idStr)) {
      ast_log(LOG_ERROR, "UUID is required\n");
      return -1;
   }
   if ( (id = ast_str_to_uuid(args.idStr)) == NULL ) {
      ast_log(LOG_ERROR, "UUID '%s' could not be parsed\n", args.idStr);
      return -1;
   }
   if( (s = audiosocket_connect(args.server)) < 0 ) {
      ast_log(LOG_ERROR, "failed to connect to AudioSocket\n");
   }

   ast_verbose("running AudioSocket '%s'\n", args.idStr);
   audiosocket_run(chan, id, s);
   close(s);
   ast_verbose("exiting audiosocket '%s'\n", args.idStr);

   return 0;
}

static int audiosocket_run(struct ast_channel *chan, struct ast_uuid *id, const int svc) {

   if (ast_set_write_format(chan, ast_format_slin)) {
      ast_log(LOG_ERROR, "Failed to set write format to SLINEAR\n");
      return 1;
   }
   if (ast_set_read_format(chan, ast_format_slin)) {
      ast_log(LOG_ERROR, "Failed to set read format to SLINEAR\n");
      return 1;
   }

   if (audiosocket_init(svc, id)) {
      return 1;
   }

	while (ast_waitfor(chan, CHANNEL_INPUT_TIMEOUT_MS) > -1) {

      // Check channel state
      if( ast_channel_state(chan) != AST_STATE_UP ) {
         ast_verbose("Channel hung up\n");
         return 0;
      }

		struct ast_frame *f = ast_read(chan);
      if(!f) {
         ast_log(LOG_WARNING, "No frame received\n");
         return 1;
      }

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

      // Send audiosocket data to channel
      if(!(f = audiosocket_receive_frame(svc))) {
         ast_log(LOG_ERROR, "Failed to receive frame from audiosocket message\n");
         return 1;
      }
      if(ast_write(chan, f)) {
         ast_log(LOG_WARNING, "Failed to forward frame to channel\n");
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
