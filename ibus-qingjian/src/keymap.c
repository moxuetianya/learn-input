#include "keymap.h"
#include <ibus.h>
#include <string.h>

/* Shift+数字按物理数字行识别（)!@#$%^&*(），字符仍是符号。 */
static const char shifted_digits[] = ")!@#$%^&*(";

static guint virtual_key(guint keyval, guint state) {
    switch (keyval) {
    case IBUS_KEY_BackSpace: return 0x08;
    case IBUS_KEY_Tab:
    case IBUS_KEY_ISO_Left_Tab: return 0x09;
    case IBUS_KEY_Return:
    case IBUS_KEY_KP_Enter: return 0x0d;
    case IBUS_KEY_Escape: return 0x1b;
    case IBUS_KEY_Page_Up:
    case IBUS_KEY_KP_Page_Up: return 0x21;
    case IBUS_KEY_Page_Down:
    case IBUS_KEY_KP_Page_Down: return 0x22;
    case IBUS_KEY_End:
    case IBUS_KEY_KP_End: return 0x23;
    case IBUS_KEY_Home:
    case IBUS_KEY_KP_Home: return 0x24;
    case IBUS_KEY_Left:
    case IBUS_KEY_KP_Left: return 0x25;
    case IBUS_KEY_Up:
    case IBUS_KEY_KP_Up: return 0x26;
    case IBUS_KEY_Right:
    case IBUS_KEY_KP_Right: return 0x27;
    case IBUS_KEY_Down:
    case IBUS_KEY_KP_Down: return 0x28;
    case IBUS_KEY_KP_Insert: return 0x2d;
    case IBUS_KEY_Delete:
    case IBUS_KEY_KP_Delete: return 0x2e;
    case IBUS_KEY_Shift_L:
    case IBUS_KEY_Shift_R: return 0x10;
    case IBUS_KEY_KP_Multiply: return 0x6a;
    case IBUS_KEY_KP_Add: return 0x6b;
    case IBUS_KEY_KP_Separator: return 0x6c;
    case IBUS_KEY_KP_Subtract: return 0x6d;
    case IBUS_KEY_KP_Decimal: return 0x6e;
    case IBUS_KEY_KP_Divide: return 0x6f;
    default: break;
    }
    if (keyval >= IBUS_KEY_KP_0 && keyval <= IBUS_KEY_KP_9)
        return 0x60 + keyval - IBUS_KEY_KP_0;
    if (keyval < 128 && (state & IBUS_SHIFT_MASK)) {
        const char *hit = strchr(shifted_digits, (char)keyval);
        if (hit != NULL) return 0x30 + (guint)(hit - shifted_digits);
    }
    return keyval;
}

static JsonObject *modifiers_object(guint keyval, guint state) {
    JsonObject *modifiers = json_object_new();
    json_object_set_boolean_member(modifiers, "ctrl", (state & IBUS_CONTROL_MASK) != 0);
    json_object_set_boolean_member(modifiers, "shift",
                                   (state & IBUS_SHIFT_MASK) != 0
                                       || keyval == IBUS_KEY_ISO_Left_Tab);
    json_object_set_boolean_member(modifiers, "alt", (state & IBUS_MOD1_MASK) != 0);
    json_object_set_boolean_member(modifiers, "win",
                                   (state & (IBUS_SUPER_MASK | IBUS_HYPER_MASK)) != 0);
    json_object_set_boolean_member(modifiers, "caps", (state & IBUS_LOCK_MASK) != 0);
    /* 中英模式由 server 按 session 记，前端永远报 false。 */
    json_object_set_boolean_member(modifiers, "english_mode", FALSE);
    return modifiers;
}

JsonObject *qj_key_event(guint keyval, guint state) {
    JsonObject *event = json_object_new();
    json_object_set_int_member(event, "virtual_key", virtual_key(keyval, state));
    gunichar ch = ibus_keyval_to_unicode(keyval);
    if (ch >= 0x20 && ch != 0x7f) {
        char utf8[8] = {0};
        g_unichar_to_utf8(ch, utf8);
        json_object_set_string_member(event, "character", utf8);
    } else {
        json_object_set_null_member(event, "character");
    }
    json_object_set_object_member(event, "modifiers", modifiers_object(keyval, state));
    return event;
}
