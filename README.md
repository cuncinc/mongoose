简易分布式共识系统

### Introduction
星型拓扑的分布式共识系统，使用websocket通信，构建于mongoose库之上。

server相当于星型拓扑中的中心节点，client相当于星型拓扑中的普通节点。每个client只与server连接，要发送到其他client的消息都要通过server转发。

server本身只负责转发消息，不参与共识；此外，server还管理client节点的加入和退出，每到client加入或退出，server都会广播通知其他client。

消息有两种格式：
1. 普通的ws消息，纯文本形式。这是client-server间的消息，不转发给其他client。包括节点上线、心跳等。
2. json-rpc消息，json格式。这是client-client间的消息，server转发给其他client。

### Build

编译运行server
```bash
$ rm server;
$ gcc -o server mongoose.c mongoose.h jsonrpc_server.c;
$ ./server 55000
```
server监听55000端口


编译运行client
```bash
$ rm client;
$ gcc -o client mongoose.c mongoose.h jsonrpc_client.c;
$ ./client 127.0.0.1:55000
```
server位于127.0.0.1:55000，可以开启多个client