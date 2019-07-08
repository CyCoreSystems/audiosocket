/*
 * Copyright (C) 2019, CyCore Systems, Inc.
 *
 * Seán C McCord <scm@cycoresys.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 * \brief AudioSocket support functions
 *
 * \author Seán C McCord <scm@cycoresys.com>
 *
 */

#ifndef _ASTERISK_RES_AUDIOSOCKET_H
#define _ASTERISK_RES_AUDIOSOCKET_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <uuid/uuid.h>

#include "asterisk/frame.h"
#include "asterisk/uuid.h"

/*!
 * \brief Send the initial message to an AudioSocket server
 *
 * \param server The server address, including port.
 *
 * \retval socket file descriptor for AudioSocket on success
 * \retval -1 on error
 */
const int audiosocket_connect(const char *server);

/*!
 * \brief Send the initial message to an AudioSocket server
 *
 * \param svc The file descriptor of the network socket to the AudioSocket server.
 * \param id The UUID to send to the AudioSocket server to uniquely identify this connection.
 *
 * \retval 0 on success
 * \retval -1 on error
 */
const int audiosocket_init(const int svc, const char *id);

/*!
 * \brief Send an Asterisk audio frame to an AudioSocket server
 *
 * \param svc The file descriptor of the network socket to the AudioSocket server.
 * \param f The Asterisk audio frame to send.
 *
 * \retval 0 on success
 * \retval -1 on error
 */
const int audiosocket_send_frame(const int svc, const struct ast_frame *f);

/*!
 * \brief Receive an Asterisk frame from an AudioSocket server
 *
 * This returned object is an ao2 reference counted object.
 *
 * Any attribute in the returned \ref hepv3_capture_info that is a
 * pointer should point to something that is allocated on the heap,
 * as it will be free'd when the \ref hepv3_capture_info object is
 * reclaimed.
 *
 * \param payload The payload to send to the HEP capture node
 * \param len     Length of \ref payload
 *
 * \retval A \ref ast_frame on success
 * \retval NULL on error
 */
struct ast_frame *audiosocket_receive_frame(const int svc);

#endif /* _ASTERISK_RES_AUDIOSOCKET_H */
