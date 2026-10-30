/* qingjian IBus 引擎：按键/焦点/候选事件 → LinuxEvent IPC → KeyResult 帧渲染。
 * 状态机与 qingjian/apps/linux/fcitx5/src/qingjian.cpp 对齐：
 * 连接失效时清除预编辑、放行输入；旧帧（revision 不前进）不重画。
 */
#include "engine.h"

#include "ipc.h"
#include "keymap.h"

#include <json-glib/json-glib.h>

/* 组句期间问一次 Server 的间隔，与 fcitx5 插件一致；本地整句模型/云联想的结果晚一拍到。 */
#define QJ_POLL_INTERVAL_MS 80
#define QJ_MAX_CANDIDATES 9
#define QJ_PROTOCOL_VERSION 8
#define QJ_LINUX_UI_VERSION 3

static guint64 next_session = 1;
static guint64 next_context = 1;
static GSList *engines = NULL;

static gboolean poll_tick(gpointer data);
static void render_frame(QjEngine *e, JsonObject *identity, JsonObject *frame);

/* ---------------- 事件构造：根节点由调用方释放 ---------------- */

/* {"LinuxEvent": {"session": N, "event": {name: body}}}；body 的所有权转移进来。 */
static JsonNode *object_event(QjEngine *e, const char *name, JsonObject *body) {
    JsonObject *inner = json_object_new();
    json_object_set_object_member(inner, name, body);
    JsonObject *outer = json_object_new();
    json_object_set_int_member(outer, "session", e->session);
    json_object_set_object_member(outer, "event", inner);
    JsonObject *root = json_object_new();
    json_object_set_object_member(root, "LinuxEvent", outer);
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    return node;
}

/* 单元变体（Reset）：{"LinuxEvent": {"session": N, "event": "Reset"}} */
static JsonNode *unit_event(QjEngine *e, const char *name) {
    JsonObject *outer = json_object_new();
    json_object_set_int_member(outer, "session", e->session);
    json_object_set_string_member(outer, "event", name);
    JsonObject *root = json_object_new();
    json_object_set_object_member(root, "LinuxEvent", outer);
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    return node;
}

static JsonNode *focus_event(QjEngine *e, gboolean focused) {
    JsonObject *body = json_object_new();
    json_object_set_boolean_member(body, "focused", focused);
    return object_event(e, "Focus", body);
}

static JsonNode *caps_event(QjEngine *e) {
    /* IBus 没有密码/只读语义的回调，首版按普通输入上报。 */
    JsonObject *body = json_object_new();
    json_object_set_boolean_member(body, "sensitive", FALSE);
    json_object_set_boolean_member(body, "password", FALSE);
    json_object_set_boolean_member(body, "disabled", FALSE);
    return object_event(e, "Capabilities", body);
}

static JsonNode *key_event(QjEngine *e, JsonObject *mapped, gboolean release) {
    JsonObject *body = json_object_new();
    json_object_set_object_member(body, "event", mapped);
    json_object_set_boolean_member(body, "release", release);
    return object_event(e, "Key", body);
}

static JsonNode *deactivate_event(QjEngine *e, gboolean focus_out) {
    /* GNOME 的 preedit 由 shell 画，不是应用内联文本，client_preedit 恒 false。 */
    JsonObject *body = json_object_new();
    json_object_set_boolean_member(body, "focus_out", focus_out);
    json_object_set_boolean_member(body, "client_preedit", FALSE);
    json_object_set_boolean_member(body, "capability_changed", FALSE);
    return object_event(e, "Deactivate", body);
}

static JsonNode *panel_event(QjEngine *e, const char *name, gboolean has_index,
                             guint64 index, gboolean next) {
    JsonObject *body = json_object_new();
    json_object_set_object_member(body, "identity",
                                  json_object_ref(e->identity));
    if (has_index)
        json_object_set_int_member(body, "index", index);
    else
        json_object_set_boolean_member(body, "next", next);
    return object_event(e, name, body);
}

static JsonNode *poll_request(QjEngine *e) {
    JsonObject *body = json_object_new();
    json_object_set_int_member(body, "session", e->session);
    JsonObject *root = json_object_new();
    json_object_set_object_member(root, "Poll", body);
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    return node;
}

static JsonNode *open_session_request(QjEngine *e) {
    JsonObject *body = json_object_new();
    json_object_set_int_member(body, "session", e->session);
    json_object_set_string_member(body, "app", "ibus");
    json_object_set_int_member(body, "protocol", QJ_PROTOCOL_VERSION);
    JsonObject *root = json_object_new();
    json_object_set_object_member(root, "OpenSession", body);
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    return node;
}

static JsonNode *linux_hello_request(QjEngine *e) {
    JsonObject *body = json_object_new();
    json_object_set_int_member(body, "version", QJ_LINUX_UI_VERSION);
    json_object_set_int_member(body, "session", e->session);
    json_object_set_int_member(body, "generation", e->generation);
    json_object_set_string_member(body, "context", e->context);
    JsonObject *root = json_object_new();
    json_object_set_object_member(root, "LinuxHello", body);
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    return node;
}

/* ---------------- JSON 读取小工具 ---------------- */

static JsonObject *wrap(JsonNode *node, const char *name) {
    if (node == NULL || !JSON_NODE_HOLDS_OBJECT(node)) return NULL;
    JsonObject *root = json_node_get_object(node);
    JsonNode *member = root ? json_object_get_member(root, name) : NULL;
    return (member && JSON_NODE_HOLDS_OBJECT(member)) ? json_node_get_object(member) : NULL;
}

static const gchar *member_string(JsonObject *object, const char *name) {
    JsonNode *node = object ? json_object_get_member(object, name) : NULL;
    return (node && JSON_NODE_HOLDS_VALUE(node)) ? json_node_get_string(node) : NULL;
}

static gint64 member_int(JsonObject *object, const char *name) {
    JsonNode *node = object ? json_object_get_member(object, name) : NULL;
    return (node && JSON_NODE_HOLDS_VALUE(node)) ? json_node_get_int(node) : -1;
}

static JsonObject *member_object(JsonObject *object, const char *name) {
    JsonNode *node = object ? json_object_get_member(object, name) : NULL;
    return (node && JSON_NODE_HOLDS_OBJECT(node)) ? json_node_get_object(node) : NULL;
}

static JsonArray *member_array(JsonObject *object, const char *name) {
    JsonNode *node = object ? json_object_get_member(object, name) : NULL;
    return (node && JSON_NODE_HOLDS_ARRAY(node)) ? json_node_get_array(node) : NULL;
}

static gboolean member_bool(JsonObject *object, const char *name) {
    JsonNode *node = object ? json_object_get_member(object, name) : NULL;
    return (node && JSON_NODE_HOLDS_VALUE(node)) ? json_node_get_boolean(node) : FALSE;
}

static JsonObject *member_identity(QjEngine *e) {
    return e->identity;
}

static guint64 identity_revision(JsonObject *identity) {
    return identity ? (guint64)member_int(identity, "revision") : 0;
}

/* 展示身份（generation/context/revision）是否属于当前连接的会话。 */
static gboolean identity_matches(QjEngine *e, JsonObject *identity) {
    return identity != NULL
           && member_int(identity, "generation") == (gint64)e->generation
           && g_strcmp0(member_string(identity, "context"), e->context) == 0
           && member_int(identity, "revision") >= 0;
}

static void take_identity(QjEngine *e, JsonObject *identity) {
    if (e->identity != NULL) json_object_unref(e->identity);
    e->identity = json_object_ref(identity);
}

static void drop_display(QjEngine *e) {
    if (e->identity != NULL) {
        json_object_unref(e->identity);
        e->identity = NULL;
    }
}

/* ---------------- 断连与显示清理 ---------------- */

static void hide_ui(QjEngine *e) {
    ibus_engine_hide_preedit_text((IBusEngine *)e);
    ibus_engine_hide_lookup_table((IBusEngine *)e);
    ibus_engine_hide_auxiliary_text((IBusEngine *)e);
}

static void stop_polling(QjEngine *e) {
    if (e->poll_id != 0) {
        g_source_remove(e->poll_id);
        e->poll_id = 0;
    }
}

static void local_invalidate(QjEngine *e) {
    e->opened = FALSE;
    e->focused = FALSE;
    stop_polling(e);
    drop_display(e);
    hide_ui(e);
}

static void disconnect_all(void) {
    qj_ipc_close();
    for (GSList *item = engines; item != NULL; item = item->next) local_invalidate(item->data);
}

/* ---------------- 握手 ---------------- */

static gboolean qj_engine_open(QjEngine *e) {
    if (e->opened) return TRUE;
    if (!qj_ipc_open()) return FALSE;
    e->generation = qj_ipc.generation;

    /* OpenSession：Linux UI 协议 v3 必须就位。 */
    JsonNode *open = open_session_request(e);
    JsonNode *open_reply = qj_ipc_call(open);
    json_node_unref(open);
    JsonObject *update = wrap(open_reply, "Update");
    JsonObject *linux_ui = update ? member_object(update, "linux_ui") : NULL;
    gboolean ok = linux_ui != NULL && member_int(linux_ui, "version") == QJ_LINUX_UI_VERSION;
    if (open_reply != NULL) json_node_unref(open_reply);
    if (!ok) {
        disconnect_all();
        return FALSE;
    }

    /* LinuxHello：带上下文身份，协商展示回执。 */
    JsonNode *hello = linux_hello_request(e);
    JsonNode *hello_reply = qj_ipc_call(hello);
    json_node_unref(hello);
    JsonObject *hello_body = wrap(hello_reply, "LinuxHello");
    ok = hello_body != NULL && member_int(hello_body, "version") == QJ_LINUX_UI_VERSION
         && member_int(hello_body, "session") == (gint64)e->session;
    if (hello_reply != NULL) json_node_unref(hello_reply);
    if (!ok) {
        disconnect_all();
        return FALSE;
    }
    e->opened = TRUE;

    /* Capabilities 必须先于任何 Key：server 拒绝未确认能力的输入。 */
    JsonNode *caps = caps_event(e);
    JsonNode *caps_reply = qj_ipc_call(caps);
    json_node_unref(caps);
    ok = wrap(caps_reply, "KeyResult") != NULL;
    if (caps_reply != NULL) json_node_unref(caps_reply);
    if (!ok) {
        disconnect_all();
        return FALSE;
    }
    return TRUE;
}

/* ---------------- 帧渲染 ---------------- */

static gchar *frame_preedit_text(JsonObject *frame, gint64 *cursor_chars) {
    GString *text = g_string_new("");
    JsonArray *segments = member_array(frame, "preedit");
    if (segments != NULL) {
        guint count = json_array_get_length(segments);
        for (guint i = 0; i < count; i++) {
            JsonObject *segment = json_array_get_object_element(segments, i);
            const gchar *piece = member_string(segment, "text");
            if (piece != NULL) g_string_append(text, piece);
        }
    }
    glong length = g_utf8_strlen(text->str, -1);
    *cursor_chars = member_int(frame, "cursor");
    if (*cursor_chars < 0 || *cursor_chars > length) *cursor_chars = length;
    return g_string_free(text, FALSE);
}

static gboolean frame_is_empty(JsonObject *frame) {
    JsonArray *segments = member_array(frame, "preedit");
    JsonArray *items = member_array(member_object(frame, "candidates"), "items");
    return (segments == NULL || json_array_get_length(segments) == 0)
           && (items == NULL || json_array_get_length(items) == 0);
}

/* 候选一行：词 + 译词（学习语言的注释，第一个义项；生词加标记）。 */
static gchar *candidate_label_text(JsonObject *item) {
    const gchar *text = member_string(item, "text");
    if (text == NULL) text = "";
    JsonObject *translation = member_object(item, "translation");
    JsonArray *senses = translation ? member_array(translation, "senses") : NULL;
    if (senses == NULL || json_array_get_length(senses) == 0) return g_strdup(text);
    JsonObject *sense = json_array_get_object_element(senses, 0);
    const gchar *gloss = member_string(sense, "text");
    if (gloss == NULL || *gloss == '\0') return g_strdup(text);
    return g_strdup_printf("%s  %s%s", text, gloss,
                           member_bool(sense, "fresh") ? " ·生" : "");
}

/* 展示回执：页上可见的译词记进词汇本（学习记录的一部分）。 */
static void acknowledge_display(QjEngine *e, JsonArray *items) {
    if (e->identity == NULL) return;
    JsonObject *body = json_object_new();
    json_object_set_int_member(body, "session", e->session);
    json_object_set_object_member(body, "identity", json_object_ref(e->identity));
    JsonArray *senses = json_array_new();
    if (items != NULL) {
        guint count = json_array_get_length(items);
        for (guint i = 0; i < count; i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            JsonObject *translation = member_object(item, "translation");
            JsonArray *glosses = translation ? member_array(translation, "senses") : NULL;
            if (glosses != NULL && json_array_get_length(glosses) > 0) {
                JsonArray *pair = json_array_new();
                json_array_add_int_element(pair, i);
                json_array_add_int_element(pair, 0);
                json_array_add_array_element(senses, pair);
            }
        }
    }
    json_object_set_array_member(body, "senses", senses);
    JsonObject *root = json_object_new();
    json_object_set_object_member(root, "DisplayAcknowledged", body);
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    JsonNode *reply = qj_ipc_call(node);
    json_node_unref(node);
    if (reply != NULL) json_node_unref(reply);
}

static void render_frame(QjEngine *e, JsonObject *identity, JsonObject *frame) {
    IBusEngine *base = (IBusEngine *)e;
    if (frame == NULL || frame_is_empty(frame)) {
        hide_ui(e);
        stop_polling(e);
        drop_display(e);
        return;
    }
    take_identity(e, identity);

    /* 拼音行：整段下划线，光标按字符位置。 */
    gint64 cursor = 0;
    gchar *preedit = frame_preedit_text(frame, &cursor);
    if (*preedit != '\0') {
        IBusText *text = ibus_text_new_from_string(preedit);
        ibus_text_append_attribute(text, IBUS_ATTR_TYPE_UNDERLINE, IBUS_ATTR_UNDERLINE_SINGLE,
                                   0, -1);
        ibus_engine_update_preedit_text(base, text, (guint)cursor, TRUE);
        g_object_unref(text);
    } else {
        ibus_engine_hide_preedit_text(base);
    }
    g_free(preedit);

    /* 候选页。 */
    JsonArray *items = member_array(member_object(frame, "candidates"), "items");
    guint count = items ? json_array_get_length(items) : 0;
    if (count > QJ_MAX_CANDIDATES) count = QJ_MAX_CANDIDATES;
    if (count > 0) {
        gint64 highlight = member_int(frame, "highlight");
        if (highlight < 0 || (guint64)highlight >= count) highlight = 0;
        IBusLookupTable *table = ibus_lookup_table_new(QJ_MAX_CANDIDATES, (guint)highlight,
                                                       TRUE, FALSE);
        ibus_lookup_table_set_orientation(
            table, g_strcmp0(member_string(frame, "layout"), "vertical") == 0
                       ? IBUS_ORIENTATION_VERTICAL
                       : IBUS_ORIENTATION_HORIZONTAL);
        for (guint i = 0; i < count; i++) {
            JsonObject *item = json_array_get_object_element(items, i);
            gchar *line = candidate_label_text(item);
            ibus_lookup_table_append_candidate(table, ibus_text_new_from_string(line));
            g_free(line);
            ibus_lookup_table_append_label(
                table, ibus_text_new_from_printf("%u", i + 1));
        }
        ibus_engine_update_lookup_table(base, table, TRUE);
        g_object_unref(table);
    } else {
        ibus_engine_hide_lookup_table(base);
    }

    /* 辅助行：优先提示气泡，其次整句补全（server 侧 Tab 接受）。 */
    const gchar *notice = member_string(frame, "notice");
    const gchar *sentence = member_string(frame, "sentence");
    if (notice != NULL || sentence != NULL) {
        gchar *line = notice != NULL ? g_strdup(notice) : g_strdup_printf("⇥ %s", sentence);
        IBusText *text = ibus_text_new_from_string(line);
        ibus_engine_update_auxiliary_text(base, text, TRUE);
        g_object_unref(text);
        g_free(line);
    } else {
        ibus_engine_hide_auxiliary_text(base);
    }

    acknowledge_display(e, items);

    /* 组句期间保持轮询节拍。 */
    stop_polling(e);
    e->poll_id = g_timeout_add(QJ_POLL_INTERVAL_MS, poll_tick, e);
}

/* ---------------- 事件交换 ---------------- */

typedef enum {
    EXCHANGE_FAILED = -1, /* 连接坏了，本地已全部失效 */
    EXCHANGE_IGNORED = 0, /* server 不认识这个会话（延迟通知），保持现状 */
    EXCHANGE_OK = 1,
} ExchangeResult;

/* 发一条 LinuxEvent 并应用 KeyResult；identity 通过校验且 revision 前进时渲染帧。
 * consumed_out 拿到「是否吃掉这次按键」。 */
static ExchangeResult exchange(QjEngine *e, JsonNode *event, gboolean *consumed_out) {
    if (consumed_out != NULL) *consumed_out = FALSE;
    if (!qj_engine_open(e)) return EXCHANGE_FAILED;

    JsonNode *response = qj_ipc_call(event);
    if (response == NULL) {
        disconnect_all();
        return EXCHANGE_FAILED;
    }
    JsonObject *result = wrap(response, "KeyResult");
    if (result == NULL) {
        gboolean was_ignored = wrap(response, "Ignored") != NULL;
        json_node_unref(response);
        if (was_ignored) return EXCHANGE_IGNORED;
        disconnect_all();
        return EXCHANGE_FAILED;
    }
    ExchangeResult outcome = EXCHANGE_OK;
    gboolean consumed = FALSE;
    if (member_int(result, "session") != (gint64)e->session) {
        outcome = EXCHANGE_FAILED;
    } else {
        JsonObject *identity = member_object(result, "identity");
        const gchar *verdict = member_string(result, "outcome");
        JsonNode *commit_node = result ? json_object_get_member(result, "commit") : NULL;
        const gchar *commit = (commit_node && JSON_NODE_HOLDS_VALUE(commit_node))
                                  ? json_node_get_string(commit_node)
                                  : NULL;
        consumed = g_strcmp0(verdict, "Consumed") == 0;
        if (!identity_matches(e, identity) || verdict == NULL
            || (!consumed && g_strcmp0(verdict, "Passthrough") != 0)) {
            outcome = EXCHANGE_FAILED;
        } else {
            if (consumed_out != NULL) *consumed_out = consumed;
            if (commit != NULL)
                ibus_engine_commit_text((IBusEngine *)e, ibus_text_new_from_string(commit));
            guint64 revision = identity_revision(identity);
            JsonObject *frame = member_object(result, "frame");
            if (frame != NULL
                && revision > identity_revision(member_identity(e))) {
                render_frame(e, identity, frame);
            }
        }
    }
    json_node_unref(response);
    if (outcome == EXCHANGE_FAILED) disconnect_all();
    return outcome;
}

static gboolean poll_tick(gpointer data) {
    QjEngine *e = data;
    e->poll_id = 0;
    if (!e->opened) return G_SOURCE_REMOVE;
    JsonNode *request = poll_request(e);
    JsonNode *response = qj_ipc_call(request);
    json_node_unref(request);
    if (response == NULL) {
        disconnect_all();
        return G_SOURCE_REMOVE;
    }
    JsonObject *update = wrap(response, "Update");
    JsonObject *identity = update ? member_object(update, "identity") : NULL;
    if (update == NULL || member_int(update, "session") != (gint64)e->session
        || !identity_matches(e, identity)) {
        json_node_unref(response);
        disconnect_all();
        return G_SOURCE_REMOVE;
    }
    /* revision 没动：帧内容没变，不重画也不重复回报。 */
    if (identity_revision(identity) > identity_revision(member_identity(e))) {
        JsonObject *frame = member_object(update, "frame");
        render_frame(e, identity, frame);
    }
    json_node_unref(response);
    return e->poll_id != 0 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

/* ---------------- IBusEngine 虚方法 ---------------- */

static gboolean qj_process_key_event(IBusEngine *base, guint keyval, guint keycode,
                                     guint state) {
    (void)keycode;
    QjEngine *e = (QjEngine *)base;
    gboolean release = (state & IBUS_RELEASE_MASK) != 0;
    JsonObject *mapped = qj_key_event(keyval, state & ~IBUS_RELEASE_MASK);
    JsonNode *event = key_event(e, mapped, release);
    gboolean consumed = FALSE;
    exchange(e, event, &consumed);
    json_node_unref(event);
    return consumed;
}

static void qj_focus_in(IBusEngine *base) {
    QjEngine *e = (QjEngine *)base;
    if (!qj_engine_open(e)) return;
    if (e->focused) return;
    e->focused = TRUE;
    JsonNode *event = focus_event(e, TRUE);
    exchange(e, event, NULL);
    json_node_unref(event);
}

static void qj_focus_out(IBusEngine *base) {
    QjEngine *e = (QjEngine *)base;
    if (!e->opened || !e->focused) return;
    e->focused = FALSE;
    stop_polling(e);
    JsonNode *event = focus_event(e, FALSE);
    exchange(e, event, NULL);
    json_node_unref(event);
}

static void qj_reset(IBusEngine *base) {
    QjEngine *e = (QjEngine *)base;
    if (!e->opened) return;
    JsonNode *event = unit_event(e, "Reset");
    exchange(e, event, NULL);
    json_node_unref(event);
    stop_polling(e);
    drop_display(e);
    hide_ui(e);
}

static void qj_disable(IBusEngine *base) {
    QjEngine *e = (QjEngine *)base;
    if (!e->opened) return;
    stop_polling(e);
    /* 失焦提交：把没定案的拼音原样交给应用（server 的 Deactivate 语义）。 */
    JsonNode *event = deactivate_event(e, FALSE);
    exchange(e, event, NULL);
    json_node_unref(event);
    drop_display(e);
    hide_ui(e);
}

static void page(QjEngine *e, gboolean next) {
    if (!e->opened || e->identity == NULL) return;
    JsonNode *event = panel_event(e, "Page", FALSE, 0, next);
    exchange(e, event, NULL);
    json_node_unref(event);
}

static void qj_page_down(IBusEngine *base) { page((QjEngine *)base, TRUE); }
static void qj_page_up(IBusEngine *base) { page((QjEngine *)base, FALSE); }

static void qj_candidate_clicked(IBusEngine *base, guint index, guint button, guint state) {
    (void)button;
    (void)state;
    QjEngine *e = (QjEngine *)base;
    if (!e->opened || e->identity == NULL || index >= QJ_MAX_CANDIDATES) return;
    JsonNode *event = panel_event(e, "Candidate", TRUE, index, FALSE);
    exchange(e, event, NULL);
    json_node_unref(event);
}

/* ---------------- 生命周期 ---------------- */

G_DEFINE_TYPE(QjEngine, qj_engine, IBUS_TYPE_ENGINE)

static void qj_engine_init(QjEngine *e) {
    e->session = next_session++;
    e->context = g_strdup_printf("ibus-%lu-%lu", (unsigned long)getpid(), next_context++);
    e->identity = NULL;
    e->poll_id = 0;
    e->opened = FALSE;
    e->focused = FALSE;
    engines = g_slist_prepend(engines, e);
}

static void qj_engine_finalize(GObject *object) {
    QjEngine *e = (QjEngine *)object;
    engines = g_slist_remove(engines, e);
    stop_polling(e);
    if (e->opened) {
        /* 发不出去也没关系：连接断开时 server 会回收全部会话。 */
        JsonObject *body = json_object_new();
        json_object_set_int_member(body, "session", e->session);
        JsonObject *root = json_object_new();
        json_object_set_object_member(root, "CloseSession", body);
        JsonNode *node = json_node_new(JSON_NODE_OBJECT);
        json_node_take_object(node, root);
        JsonNode *reply = qj_ipc_call(node);
        json_node_unref(node);
        if (reply != NULL) json_node_unref(reply);
    }
    drop_display(e);
    g_free(e->context);
    G_OBJECT_CLASS(qj_engine_parent_class)->finalize(object);
}

static void qj_engine_class_init(QjEngineClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = qj_engine_finalize;
    IBusEngineClass *engine_class = IBUS_ENGINE_CLASS(klass);
    engine_class->process_key_event = qj_process_key_event;
    engine_class->focus_in = qj_focus_in;
    engine_class->focus_out = qj_focus_out;
    engine_class->reset = qj_reset;
    engine_class->disable = qj_disable;
    engine_class->page_up = qj_page_up;
    engine_class->page_down = qj_page_down;
    engine_class->candidate_clicked = qj_candidate_clicked;
}
