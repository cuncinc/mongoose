// client.cpp

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "mongoose.h"

static struct mg_mgr mgr;
static struct mg_rpc *s_rpc_head = NULL;
static int node_cnt = 0;

// rpc-test
static void rpc_sum_handler(struct mg_rpc_req *r)
{
    fprintf(stdout, "rpc_sum_handler ");
    double a = 0.0, b = 0.0;
    int id = 0;
    mg_json_get_num(r->frame, "$.params[0]", &a);
    mg_json_get_num(r->frame, "$.params[1]", &b);
    char *method = mg_json_get_str(r->frame, "$.method");
    fprintf(stdout, "%s a=%lf, b=%lf\n", method, a, b);
    mg_rpc_ok(r, "%g", a + b);
}

// 分布式共识的rpc方法，包括：1.找到新区块广播 2.投票广播(验证哈希) 3.上链广播(即得到半数以上的投票)

static void new_block_broadcast(char *hash)
{
    fprintf(stdout, "[System] new_block_broadcast:%s\n", hash);
    mg_ws_printf(mgr.conns, WEBSOCKET_OP_TEXT, "{%m:%m,%m:%m}",
                 MG_ESC("method"), MG_ESC("new_block"),
                 MG_ESC("params"), MG_ESC(hash));
}

static void vote_broadcast(char *hash)
{
    fprintf(stdout, "[System] vote_broadcast\n");
    mg_ws_printf(mgr.conns, WEBSOCKET_OP_TEXT, "{%m:%m,%m:%m}",
                 MG_ESC("method"), MG_ESC("vote"),
                 MG_ESC("params"), MG_ESC(hash));
}

static void up_chain_broadcast(char *hash)
{
    fprintf(stdout, "[System] up_chain_broadcast\n");
    mg_ws_printf(mgr.conns, WEBSOCKET_OP_TEXT, "{%m:%m,%m:%m}",
                 MG_ESC("method"), MG_ESC("up_chain"),
                 MG_ESC("params"), MG_ESC(hash));
}

/**
 * 其他节点找到新区块，验证并投票
 */
static void rpc_new_block_handler(struct mg_rpc_req *r)
{
    fprintf(stdout, "rpc_new_block_handler");
    char *hash = mg_json_get_str(r->frame, "$.params");
    vote_broadcast(hash);
}

/**
 * 其他节点投票，记录下所有票数，过半后上链，发起上链广播，投票数据怎么存？
 */
static void rpc_vote_handler(struct mg_rpc_req *r)
{
    fprintf(stdout, "rpc_vote_handler ");
    char *hash = mg_json_get_str(r->frame, "$.params");
}

/**
 * 其他节点发起上链广播，验证后自己也上链，并把有关投票的数据删除
 */
static void rpc_up_chain_handler(struct mg_rpc_req *r)
{
    fprintf(stdout, "rpc_chain ");
    char *hash = mg_json_get_str(r->frame, "$.params");
    fprintf(stdout, "[Info] up_chain: %s", hash);
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

void timerCallback()
{
    srand(time(NULL));
    int x = rand() % 1000000;
    if (x % 5 == 0)
    {
        char hash[10] = {0};
        sprintf(hash, "%d", x);
        new_block_broadcast(hash);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
        exit(1);

    // 设置定时器
    struct itimerval timer;
    timer.it_interval.tv_sec = 3; // 间隔3秒
    timer.it_interval.tv_usec = 0;
    timer.it_value.tv_sec = 3; // 第一次触发也是3秒后
    timer.it_value.tv_usec = 0;
    // 注册信号处理函数
    signal(SIGALRM, timerCallback);
    // 启动定时器
    setitimer(ITIMER_REAL, &timer, NULL);

    // network
    char *ipport = argv[1];
    char server_url[100] = {0};
    sprintf(server_url, "ws://%s", ipport);

    mg_mgr_init(&mgr);
    mg_ws_connect(&mgr, server_url, ev_handler, NULL, NULL);

    mg_rpc_add(&s_rpc_head, mg_str("sum"), rpc_sum_handler, NULL);
    mg_rpc_add(&s_rpc_head, mg_str("new_block"), rpc_new_block_handler, NULL);
    mg_rpc_add(&s_rpc_head, mg_str("vote"), rpc_vote_handler, NULL);
    mg_rpc_add(&s_rpc_head, mg_str("up_chain"), rpc_up_chain_handler, NULL);
    mg_rpc_add(&s_rpc_head, mg_str("rpc.list"), mg_rpc_list, &s_rpc_head);

    for (;;)
    {
        mg_mgr_poll(&mgr, 100);
    }
    mg_mgr_free(&mgr);
    mg_rpc_del(&s_rpc_head, NULL); // Deallocate RPC handlers

    return 0;
}
