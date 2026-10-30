/* IBus keyval/state → qingjian 共享协议的 KeyEvent。
 * VK 值与 qingjian/apps/linux/fcitx5/src/key/mapping.cpp 保持一致。
 * 返回新分配的 JsonObject（所有权交给调用方 set_object_member）。 */
#ifndef QJ_KEYMAP_H
#define QJ_KEYMAP_H

#include <json-glib/json-glib.h>

JsonObject *qj_key_event(guint keyval, guint state);

#endif
