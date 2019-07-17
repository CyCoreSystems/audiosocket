/*
 * Copyright (C) 2019, CyCore Systems, Inc.
 *
 * Seán C McCord <scm@cycoresys.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \author Seán C McCord <scm@cycoresys.com>
 *
 * \brief AudioSocket Channel
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>res_audiosocket</depend>
	<support_level>core</support_level>
 ***/

#include "asterisk.h"
#include <uuid/uuid.h>

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/uuid.h"
#include "asterisk/res_audiosocket.h"
#include "asterisk/pbx.h"
#include "asterisk/acl.h"
#include "asterisk/app.h"
#include "asterisk/causes.h"
#include "asterisk/format_cache.h"

struct audiosocket_instance {
   int svc;
   char id[38];
} audiosocket_instance;

/* Forward declarations */
static struct ast_channel *audiosocket_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause);
static int audiosocket_call(struct ast_channel *ast, const char *dest, int timeout);
static int audiosocket_hangup(struct ast_channel *ast);
static struct ast_frame *audiosocket_read(struct ast_channel *ast);
static int audiosocket_write(struct ast_channel *ast, struct ast_frame *f);

/* AudioSocket channel driver declaration */
static struct ast_channel_tech audiosocket_channel_tech = {
	.type = "AudioSocket",
	.description = "AudioSocket Channel Driver",
	.requester = audiosocket_request,
	.call = audiosocket_call,
	.hangup = audiosocket_hangup,
	.read = audiosocket_read,
	.write = audiosocket_write,
};

/*! \brief Function called when we should read a frame from the channel */
static struct ast_frame *audiosocket_read(struct ast_channel *ast)
{
	struct audiosocket_instance *instance = ast_channel_tech_pvt(ast);

   if (instance == NULL || instance->svc < 1) {
      return NULL;
   }
   return audiosocket_receive_frame(instance->svc);
}

/*! \brief Function called when we should write a frame to the channel */
static int audiosocket_write(struct ast_channel *ast, struct ast_frame *f)
{
	struct audiosocket_instance *instance = ast_channel_tech_pvt(ast);

   if (instance == NULL || instance->svc < 1) {
      return -1;
   }
	return audiosocket_send_frame(instance->svc, f);
}

/*! \brief Function called when we should actually call the destination */
static int audiosocket_call(struct ast_channel *ast, const char *dest, int timeout)
{
	struct audiosocket_instance *instance = ast_channel_tech_pvt(ast);

	ast_queue_control(ast, AST_CONTROL_ANSWER);

   if (ast_set_write_format(ast, ast_format_slin)) {
      ast_log(LOG_ERROR, "Failed to set write format to SLINEAR\n");
      return -1;
   }
   if (ast_set_read_format(ast, ast_format_slin)) {
      ast_log(LOG_ERROR, "Failed to set read format to SLINEAR\n");
      return -1;
   }

   ast_verbose("retrieved AudioSocket instance (%s)(%d)\n", instance->id, instance->svc);

   return audiosocket_init(instance->svc, instance->id);
}

/*! \brief Function called when we should hang the channel up */
static int audiosocket_hangup(struct ast_channel *ast)
{
	struct audiosocket_instance *instance = ast_channel_tech_pvt(ast);

   if(instance != NULL && instance->svc > 0) {
      close(instance->svc);
   }

   ast_channel_tech_pvt_set(ast, NULL);

	return 0;
}

/*! \brief Function called when we should prepare to call the unicast destination */
static struct ast_channel *audiosocket_request(const char *type, struct ast_format_cap *cap, const struct ast_assigned_ids *assignedids, const struct ast_channel *requestor, const char *data, int *cause)
{
	char *parse;
	struct audiosocket_instance *instance;
	struct ast_sockaddr address;
	struct ast_channel *chan;
   struct ast_uuid *id = NULL;
   int fd;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(destination);
		AST_APP_ARG(idStr);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "Destination is required for the 'AudioSocket' channel\n");
		goto failure;
	}
	parse = ast_strdupa(data);
	AST_NONSTANDARD_APP_ARGS(args, parse, '/');

	if (ast_strlen_zero(args.destination)) {
		ast_log(LOG_ERROR, "Destination is required for the 'AudioSocket' channel\n");
		goto failure;
	}
	if (ast_sockaddr_resolve_first_af(&address, args.destination, PARSE_PORT_REQUIRE, AST_AF_UNSPEC)) {
		ast_log(LOG_ERROR, "Destination '%s' could not be parsed\n", args.destination);
		goto failure;
	}

	if (ast_strlen_zero(args.idStr)) {
		ast_log(LOG_ERROR, "UUID is required for the 'AudioSocket' channel\n");
		goto failure;
   }
   if ( (id = ast_str_to_uuid(args.idStr)) == NULL ) {
      ast_log(LOG_ERROR, "UUID '%s' could not be parsed\n", args.idStr);
      goto failure;
   }
   ast_free(id);
   ast_verbose("parsed UUID '%s'\n", args.idStr);

   instance = ast_calloc(1, sizeof(*instance));
   ast_copy_string(instance->id, args.idStr, sizeof(instance->id));

   //instance.id = args.idStr;

	if(ast_format_cap_iscompatible_format(cap, ast_format_slin) == AST_FORMAT_CMP_NOT_EQUAL) {
		struct ast_str *cap_buf = ast_str_alloca(AST_FORMAT_CAP_NAMES_LEN);

		ast_log(LOG_NOTICE, "Asked to get a channel of unsupported format '%s'\n",
			ast_format_cap_get_names(cap, &cap_buf));
      goto failure;
	}

   if( (fd = audiosocket_connect(args.destination)) < 0 ) {
      ast_log(LOG_ERROR, "Failed to connect to AudioSocket server at '%s'\n", args.destination);
      goto failure;
   }
   instance->svc = fd;

	chan = ast_channel_alloc(1, AST_STATE_DOWN, "", "", "", "", "", assignedids,
		requestor, 0, "AudioSocket/%s-%s", args.destination, args.idStr);
	if (!chan) {
		goto failure;
	}
	ast_channel_set_fd(chan, 0, fd);

	ast_channel_tech_set(chan, &audiosocket_channel_tech);

	ast_channel_nativeformats_set(chan, audiosocket_channel_tech.capabilities);
	ast_channel_set_writeformat(chan, ast_format_slin);
	ast_channel_set_rawwriteformat(chan, ast_format_slin);
	ast_channel_set_readformat(chan, ast_format_slin);
	ast_channel_set_rawreadformat(chan, ast_format_slin);

	ast_channel_tech_pvt_set(chan, instance);

   ast_verbose("stored UUID '%s'\n", instance->id);

	pbx_builtin_setvar_helper(chan, "AUDIOSOCKET_UUID", args.idStr);
	pbx_builtin_setvar_helper(chan, "AUDIOSOCKET_SERVICE", args.destination);

	ast_channel_unlock(chan);

	return chan;

failure:
	*cause = AST_CAUSE_FAILURE;
	return NULL;
}

/*! \brief Function called when our module is unloaded */
static int unload_module(void)
{
	ast_channel_unregister(&audiosocket_channel_tech);
	ao2_cleanup(audiosocket_channel_tech.capabilities);
	audiosocket_channel_tech.capabilities = NULL;

	return 0;
}

/*! \brief Function called when our module is loaded */
static int load_module(void)
{
	if (!(audiosocket_channel_tech.capabilities = ast_format_cap_alloc(AST_FORMAT_CAP_FLAG_DEFAULT))) {
		return AST_MODULE_LOAD_DECLINE;
	}
	ast_format_cap_append(audiosocket_channel_tech.capabilities, ast_format_slin, 0);

	if (ast_channel_register(&audiosocket_channel_tech)) {
		ast_log(LOG_ERROR, "Unable to register channel class AudioSocket");
		ao2_ref(audiosocket_channel_tech.capabilities, -1);
		audiosocket_channel_tech.capabilities = NULL;
		return AST_MODULE_LOAD_DECLINE;
	}
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "AudioSocket Channel",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.load_pri = AST_MODPRI_CHANNEL_DRIVER,
	.requires = "res_audiosocket",
);
