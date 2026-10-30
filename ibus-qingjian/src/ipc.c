/* 线协议实现：长度前缀显式小端；对端 SO_PEERCRED 必须同 uid。 */
#include "ipc.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

QjIpc qj_ipc = {.fd = -1, .generation = 0};

/* 每次收发共用的截止时间，与 fcitx5 插件一致。 */
#define QJ_DEADLINE_MS 200
/* 单消息上限，与 server 一致。 */
#define QJ_MAX_MESSAGE (16u * 1024u * 1024u)

gboolean qj_ipc_connected(void) { return qj_ipc.fd >= 0; }

void qj_ipc_close(void) {
    if (qj_ipc.fd >= 0) close(qj_ipc.fd);
    qj_ipc.fd = -1;
}

static char *socket_path(void) {
    const char *custom = getenv("QINGJIAN_SOCKET");
    if (custom && *custom == '/') return g_strdup(custom);
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime) return g_strdup_printf("%s/qingjian.sock", runtime);
    return g_strdup_printf("/tmp/qingjian-%ld/qingjian.sock", (long)geteuid());
}

gboolean qj_ipc_open(void) {
    if (qj_ipc_connected()) return TRUE;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return FALSE;
    char *path = socket_path();
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    gboolean ok = path && path[0] == '/' && strlen(path) < sizeof(address.sun_path);
    if (ok) {
        strcpy(address.sun_path, path);
        ok = connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0;
    }
    g_free(path);
    if (ok) {
        struct ucred credentials;
        socklen_t size = sizeof(credentials);
        ok = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == 0
             && credentials.uid == geteuid();
    }
    if (!ok) {
        close(fd);
        return FALSE;
    }
    qj_ipc.fd = fd;
    qj_ipc.generation++;
    return TRUE;
}

static gboolean transfer(void *buffer, size_t length, gboolean write, int64_t deadline_ms) {
    char *data = buffer;
    while (length > 0) {
        int64_t remaining = deadline_ms - g_get_monotonic_time() / 1000;
        if (remaining <= 0) return FALSE;
        struct pollfd pfd = {.fd = qj_ipc.fd,
                             .events = write ? POLLOUT : POLLIN,
                             .revents = 0};
        int ready = poll(&pfd, 1, (int)remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0 || (pfd.revents & (POLLERR | POLLNVAL))) return FALSE;
        ssize_t moved = write ? send(qj_ipc.fd, data, length, MSG_NOSIGNAL)
                              : recv(qj_ipc.fd, data, length, 0);
        if (moved < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (moved <= 0) return FALSE;
        data += moved;
        length -= (size_t)moved;
    }
    return TRUE;
}

static gboolean write_message(const char *body) {
    uint32_t length = (uint32_t)strlen(body);
    unsigned char prefix[4] = {(unsigned char)length, (unsigned char)(length >> 8),
                               (unsigned char)(length >> 16), (unsigned char)(length >> 24)};
    int64_t deadline = g_get_monotonic_time() / 1000 + QJ_DEADLINE_MS;
    return transfer(prefix, 4, TRUE, deadline)
           && transfer((void *)body, length, TRUE, deadline);
}

static JsonNode *read_message(void) {
    unsigned char prefix[4];
    uint32_t length = 0;
    int64_t deadline = g_get_monotonic_time() / 1000 + QJ_DEADLINE_MS;
    if (!transfer(prefix, 4, FALSE, deadline)) return NULL;
    length = (uint32_t)prefix[0] | ((uint32_t)prefix[1] << 8) | ((uint32_t)prefix[2] << 16)
             | ((uint32_t)prefix[3] << 24);
    if (length == 0 || length > QJ_MAX_MESSAGE) return NULL;
    char *body = g_malloc(length);
    if (!transfer(body, length, FALSE, deadline)) {
        g_free(body);
        return NULL;
    }
    JsonParser *parser = json_parser_new();
    JsonNode *root = NULL;
    if (json_parser_load_from_data(parser, body, (gssize)length, NULL))
        root = json_parser_steal_root(parser);
    g_object_unref(parser);
    g_free(body);
    return root;
}

JsonNode *qj_ipc_call(JsonNode *request) {
    if (!qj_ipc_connected()) return NULL;
    char *body = json_to_string(request, FALSE);
    gboolean ok = write_message(body);
    g_free(body);
    JsonNode *response = ok ? read_message() : NULL;
    if (!response) qj_ipc_close();
    return response;
}
