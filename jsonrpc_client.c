// client.cpp

#include "mongoose.h"

static struct mg_mgr mgr;
static struct mg_rpc *s_rpc_head = NULL;
static int node_cnt = 0;

static void rpc_sum(struct mg_rpc_req *r)
{
    fprintf(stdout, "rpc_sum ");
    double a = 0.0, b = 0.0;
    int id = 0;
    mg_json_get_num(r->frame, "$.params[0]", &a);
    mg_json_get_num(r->frame, "$.params[1]", &b);
    char *method = mg_json_get_str(r->frame, "$.method");
    fprintf(stdout, "%s a=%lf, b=%lf\n", method, a, b);
    mg_rpc_ok(r, "%g", a + b);
}

static void rpc_mul(struct mg_rpc_req *r)
{
    fprintf(stdout, "rpc_mul\n");
    double a = 0.0, b = 0.0;
    mg_json_get_num(r->frame, "$.params[0]", &a);
    mg_json_get_num(r->frame, "$.params[1]", &b);
    mg_rpc_ok(r, "%g", a * b);
}

static void rpc_check_hash(struct mg_rpc_req *r)
{
    fprintf(stdout, "rpc_check_hash\n");
    // 检查哈希
}

static void rpc_newblock(struct mg_rpc_req *r)
{
    fprintf(stdout, "rpc_newblock\n");
    // 上链，存数据库
}

/**
 * 处理rpc请求，只响应有id项的请求。一般由其他对等节点发出，由服务器转发
 * @param c 与服务器的连接，用于发送响应
 * @param data 请求数据
 */
static void rpc_request_handler(struct mg_connection *c, struct mg_str data)
{
    struct mg_iobuf io = {0, 0, 0, 512};
    struct mg_rpc_req r = {&s_rpc_head, 0, mg_pfn_iobuf, &io, 0, data};
    mg_rpc_process(&r);

    // rpc的响应
    if (io.buf)
        mg_ws_send(c, (char *)io.buf, io.len, WEBSOCKET_OP_TEXT);
    mg_iobuf_free(&io);
}

/**
 * 处理ws请求，一般由服务器发出
 * @param data 请求数据
 */
static void ws_request_handler(struct mg_str data)
{
    const char *msg = data.ptr;
    // 节点加入
    if (strstr(msg, "node_add"))
    {
        ++node_cnt;
        fprintf(stdout, "[System] node_add: %d\n", node_cnt);
    }
    // 节点退出
    else if (strstr(msg, "node_leave"))
    {
        --node_cnt;
        fprintf(stdout, "[System] node_leave: %d\n", node_cnt);
    }
    // 节点数量
    else if (strstr(msg, "node_cnt:"))
    {
        sscanf(msg, "node_cnt:%d", &node_cnt);
        fprintf(stdout, "[System] node_cnt: %d\n", node_cnt);
    }
}

static void ev_handler(struct mg_connection *c, int ev, void *ev_data, void *fn_data)
{
    // fprintf(stdout, "ev_handler: %d\n", ev);
    if (ev == MG_EV_OPEN)
    {
        fprintf(stdout, "MG_EV_OPEN\n");
    }
    else if (ev == MG_EV_WS_OPEN)
    {
        fprintf(stdout, "MG_EV_WS_OPEN\n");
        // 发一个ws消息给Server，告诉Server我是节点
        mg_ws_send(c, "i_am_node", 9, WEBSOCKET_OP_TEXT);
    }
    else if (ev == MG_EV_WS_MSG)
    {
        fprintf(stdout, "MG_EV_WS_MSG\n");
        struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
        fprintf(stdout, "[Received] %s\n", wm->data.ptr);

        if (wm->data.ptr[0] == '{') // json-rpc，节点发的消息
        {
            rpc_request_handler(c, wm->data);
        }
        else // Server发的消息，一般是节点加入或退出
        {
            ws_request_handler(wm->data);
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        exit(1);
    char *ipport = argv[1];
    char server_url[100] = {0};
    sprintf(server_url, "ws://%s", ipport);

    mg_mgr_init(&mgr);
    mg_ws_connect(&mgr, server_url, ev_handler, NULL, NULL);

    mg_rpc_add(&s_rpc_head, mg_str("sum"), rpc_sum, NULL);
    mg_rpc_add(&s_rpc_head, mg_str("mul"), rpc_mul, NULL);
    mg_rpc_add(&s_rpc_head, mg_str("rpc.list"), mg_rpc_list, &s_rpc_head);

    for (;;)
    {
        mg_mgr_poll(&mgr, 1000);
    }
    mg_mgr_free(&mgr);
    // mg_rpc_del(&s_rpc_head, NULL); // Deallocate RPC handlers

    return 0;
}
