/* ibus-engine-qingjian：IBus 引擎进程入口。
 * ibus-daemon 按 component 描述拉起本进程；daemon 消失时退出，等 daemon 重新拉起。 */
#include "engine.h"

#include <ibus.h>

static void disconnected(IBusBus *bus, gpointer user_data) {
    (void)bus;
    (void)user_data;
    ibus_quit();
}

gint main(gint argc, gchar **argv) {
    (void)argc;
    if (argc > 1 && (g_strcmp0(argv[1], "--version") == 0
                     || g_strcmp0(argv[1], "-V") == 0)) {
        g_print("ibus-engine-qingjian %s\n", QJ_VERSION);
        return 0;
    }
    ibus_init();
    IBusBus *bus = ibus_bus_new();
    if (bus == NULL || !ibus_bus_is_connected(bus)) {
        g_printerr("ibus-engine-qingjian: session bus 上没有 ibus-daemon\n");
        return 1;
    }
    g_signal_connect(bus, "disconnected", G_CALLBACK(disconnected), NULL);

    IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(bus));
    ibus_factory_add_engine(factory, "qingjian", QJ_TYPE_ENGINE);
    /* 必须申请 component 描述里的名字：daemon 靠这个 DBus 名发现工厂进程就绪。 */
    ibus_bus_request_name(bus, "org.qingjian.IBus.Qingjian", 0);
    ibus_main();
    g_object_unref(factory);
    g_object_unref(bus);
    return 0;
}
