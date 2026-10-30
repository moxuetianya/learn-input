/* qingjian IPC：同用户 Unix socket + 4 字节小端长度前缀 + JSON。
 * 与 qingjian/apps/linux/fcitx5/src/ipc/connection.cpp 同一套线协议：
 * 非阻塞 fd 内同步 poll，共享 200ms 截止时间；任何一步失败关闭连接。
 */
#ifndef QJ_IPC_H
#define QJ_IPC_H

#include <json-glib/json-glib.h>
#include <glib.h>

/* 全局共享连接；generation 在每次重连后递增，engine 侧用它识别 stale 会话。 */
typedef struct {
    int fd;
    guint64 generation;
} QjIpc;

extern QjIpc qj_ipc;

/* 连接 server（QINGJIAN_SOCKET / $XDG_RUNTIME_DIR/qingjian.sock / /tmp/qingjian-<uid>/）。
 * 已连接时直接返回 TRUE。 */
gboolean qj_ipc_open(void);

gboolean qj_ipc_connected(void);

/* 同步请求-响应。request 的根节点是单成员对象（如 {"Poll":{...}}）。
 * 返回响应根节点（调用方负责 json_node_unref），失败返回 NULL 并关闭连接。 */
JsonNode *qj_ipc_call(JsonNode *request);

void qj_ipc_close(void);

#endif
