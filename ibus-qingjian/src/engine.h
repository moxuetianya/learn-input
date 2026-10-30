/* qingjian IBus 引擎：按键/焦点/候选事件 → LinuxEvent IPC → KeyResult 帧渲染。
 * 状态机与 qingjian/apps/linux/fcitx5/src/qingjian.cpp 对齐：
 * 连接失效时清除预编辑、放行输入；旧帧（revision 不前进）不重画。
 */
#ifndef QJ_ENGINE_H
#define QJ_ENGINE_H

#include <ibus.h>
#include <json-glib/json-glib.h>

#define QJ_TYPE_ENGINE (qj_engine_get_type())
#define QJ_ENGINE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), QJ_TYPE_ENGINE, QjEngine))
#define QJ_IS_ENGINE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), QJ_TYPE_ENGINE))

typedef struct _QjEngine QjEngine;
typedef struct _QjEngineClass QjEngineClass;

struct _QjEngine {
    IBusEngine parent;

    /* 连接内会话编号（本地分配、从 1 递增）。 */
    guint64 session;

    /* 握手时记下的连接代次；server 重启换代后旧会话全部失效。 */
    guint64 generation;

    gboolean opened;
    gboolean focused;

    /* 本上下文的唯一身份串（≤64 字节）。 */
    gchar *context;

    /* 最近一次已画帧的展示身份（含 revision）；NULL 表示当前无展示。 */
    JsonObject *identity;

    guint poll_id;
};

struct _QjEngineClass {
    IBusEngineClass parent_class;
};

GType qj_engine_get_type(void) G_GNUC_CONST;

#endif
