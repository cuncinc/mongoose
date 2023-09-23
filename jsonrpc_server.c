/**
 * 中转服务器
 *
 * 1. ws格式的消息是发给服务器的，由服务器处理，不转发。此类消息包括：节点加入、节点退出、心跳
 * 2. rpc格式的消息是发给节点的，直接转发
 *
 * WS协议客户端使用c->data[0]='W'标记
 * 节点使用c->data[1]='N'标记
 */

#include "mongoose.h"

#define HEARTBEAT_INTERVAL 10

static struct mg_mgr mgr;
static int node_cnt = 0;

/**
 * 向除了发送者以为的其他节点转发消息，一般由节点发出
 * @param wm 消息
 * @param me 发送者
 * @param mgr 管理器，用于获取所有连接
 */
static void forward(struct mg_ws_message *wm, const struct mg_connection *me)
{
    // 将wm转发到其他不是me的节点
    fprintf(stdout, "[Forward] %s\n", wm->data.ptr);
    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
        if (me == c)
            continue;
        if (c->data[0] != 'W' || c->data[1] != 'N')
            continue;

        mg_ws_send(c, wm->data.ptr, wm->data.len, WEBSOCKET_OP_TEXT);
    }
}

/**
 * 向所有节点广播消息，一般由服务器发出
 * @param msg 消息
 * @param len 消息长度
 * @param mgr 管理器，用于获取所有连接
 */
static void broadcast(char *msg, int len, const struct mg_connection *me)
{
    fprintf(stdout, "[Broadcast] %s\n", msg);

    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
        if (c->data[0] != 'W' || c->data[1] != 'N')
            continue;
        if (me == c)
            continue;

        mg_ws_send(c, msg, len, WEBSOCKET_OP_TEXT);
    }
}

/**
 * 节点数量处理函数，单播，发送给新加入的节点
 * @param c 连接
 */
static void node_cnt_handler(struct mg_connection *c)
{
    // 发送节点数量
    char msg[100] = {0};
    int len = mg_snprintf(msg, sizeof(msg), "node_cnt:%d", node_cnt);
    mg_ws_send(c, msg, len, WEBSOCKET_OP_TEXT); // 单播
    fprintf(stdout, "[Send] [%d.%d.%d.%d:%d] node_cnt:%d\n", c->rem.ip[0], c->rem.ip[1], c->rem.ip[2], c->rem.ip[3], c->rem.port, node_cnt);
}

static void node_leave_handler(struct mg_connection *c)
{
    // fprintf(stdout, "[Broadcast] node_leave:[%d.%d.%d.%d:%d]\n", c->rem.ip[0], c->rem.ip[1], c->rem.ip[2], c->rem.ip[3], c->rem.port);
    // 广播节点离开
    char msg[100] = {0};
    int len = mg_snprintf(msg, sizeof(msg), "node_leave:[%d.%d.%d.%d:%d]", c->rem.ip[0], c->rem.ip[1], c->rem.ip[2], c->rem.ip[3], c->rem.port);
    broadcast(msg, len, c);
    --node_cnt;
}

static void node_add_handler(struct mg_connection *c)
{
    // fprintf(stdout, "[Broadcast] node_add:[%d.%d.%d.%d:%d]\n", c->rem.ip[0], c->rem.ip[1], c->rem.ip[2], c->rem.ip[3], c->rem.port);
    // 广播新节点加入
    char message[100] = {0};
    int len = mg_snprintf(message, sizeof(message), "node_add:[%d.%d.%d.%d:%d]", c->rem.ip[0], c->rem.ip[1], c->rem.ip[2], c->rem.ip[3], c->rem.port);
    broadcast(message, len, c);
    ++node_cnt;
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data)
{
    if (ev == MG_EV_WS_MSG)
    {
        fprintf(stdout, "MG_EV_WS_MSG\n");

        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        char *msg = (char *)wm->data.ptr;
        fprintf(stdout, "[Received] [%d.%d.%d.%d:%d] ", c->rem.ip[0], c->rem.ip[1], c->rem.ip[2], c->rem.ip[3], c->rem.port);
        fprintf(stdout, "%s\n", msg);

        if (c->data[1] = 'N' && msg[0] == '{') // 1.来自节点的消息 2.是json-rpc格式。才转发给其他节点
            forward(wm, c);
        else if (strstr(msg, "i_am_node")) // 收到客户端的i_am_node消息，标记为节点
        {
            c->data[1] = 'N'; // 标记此连接为节点
            node_add_handler(c);
            node_cnt_handler(c);
        }
    }
    else if (ev == MG_EV_WS_OPEN)
    {
        fprintf(stdout, "MG_EV_WS_OPEN\n");
        // 通知计时器函数是什么类型，如果不使用计时器，可以不用设置
        c->data[0] = 'W'; // Mark this connection as an established WS client
    }
    else if (ev == MG_EV_CLOSE)
    {
        fprintf(stdout, "MG_EV_CLOSE\n");
        if (c->data[0] == 'W' && c->data[1] == 'N') // 节点退出
            node_leave_handler(c);
    }
    else if (ev == MG_EV_HTTP_MSG)
    {
        fprintf(stdout, "MG_EV_HTTP_MSG\n");
        struct mg_http_message *hm = (struct mg_http_message *)ev_data;
        fprintf(stdout, "WS_UPGRADE\n");
        mg_ws_upgrade(c, hm, NULL);
    }
    else if (ev == MG_EV_OPEN)
    {
        fprintf(stdout, "MG_EV_OPEN\n");
    }

    (void)fn_data;
}

static void timer_fn(void *arg)
{
    // fprintf(stdout, "[Broadcast] heartbeat\n");
    // Broadcast message to all connected websocket clients.
    for (struct mg_connection *c = mgr.conns; c != NULL; c = c->next)
    {
        if (c->data[0] != 'W')
            continue;
        mg_ws_send(c, "ping", 4, WEBSOCKET_OP_TEXT);
        // mg_ws_printf(c, WEBSOCKET_OP_TEXT, "{%m:%m,%m:%d}",
        //              MG_ESC("method"), MG_ESC("heartbeat"),
        //              MG_ESC("id"), 1);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        exit(1);
    char *port = argv[1];
    char s_listen_on[100] = {0};
    sprintf(s_listen_on, "0.0.0.0:%s", port);

    mg_mgr_init(&mgr);      // Init event manager
    mg_log_set(MG_LL_INFO); // Set log level

    // mg_timer_add(&mgr, HEARTBEAT_INTERVAL * 1000, MG_TIMER_REPEAT, timer_fn, NULL); // Init timer

    printf("Starting WS Server on %s\n", s_listen_on);
    mg_http_listen(&mgr, s_listen_on, ev_handler, &mgr); // Create HTTP listener
    for (;;)
        mg_mgr_poll(&mgr, 500); // Infinite event loop
    mg_mgr_free(&mgr);          // Deallocate event manager
    return 0;
}