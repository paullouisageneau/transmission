/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

struct tr_websocket_task;

typedef enum
{
    TR_WEBSOCKET_CLOSE_WHEN_IDLE,
    TR_WEBSOCKET_CLOSE_NOW
} tr_websocket_close_mode;

void tr_websocketClose(tr_session* session, tr_websocket_close_mode close_mode);

typedef void (* tr_websocket_recv_func)(tr_session* session, bool timeout_flag, void const* response,
    size_t response_byte_count, void* user_data);

struct tr_websocket_task* tr_websocketRun(tr_session* session, char const* url, tr_websocket_recv_func done_func,
    void* done_func_user_data);

#ifdef __cplusplus
}
#endif
