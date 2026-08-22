// 最小 bRPC 服务端示例（对应 server_basics.md）
// 编译见仓库根目录 CMakeLists.txt
#include <iostream>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include "echo.pb.h"

DEFINE_int32(port, 8000, "TCP Port of this server");

class EchoServiceImpl : public example::EchoService {
public:
    EchoServiceImpl() {}
    ~EchoServiceImpl() {}

    void Echo(google::protobuf::RpcController* controller,
              const example::EchoRequest* request,
              example::EchoResponse* response,
              google::protobuf::Closure* done) override {
        // ClosureGuard：RAII 方式确保 done->Run() 一定被调用
        brpc::ClosureGuard done_guard(done);

        brpc::Controller* cntl = static_cast<brpc::Controller*>(controller);
        const std::string& msg = request->message();
        response->set_message(msg);   // 原样回显

        LOG(INFO) << "[server] recv=\"" << msg << "\" log_id=" << cntl->log_id();
    }
};

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Server server;
    EchoServiceImpl echo_service;

    if (server.AddService(&echo_service,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add EchoService";
        return -1;
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;   // 不断开空闲连接
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start server on port " << FLAGS_port;
        return -1;
    }
    LOG(INFO) << "EchoServer listening on port " << FLAGS_port;

    server.RunUntilAskedToQuit();     // 阻塞，直到 Ctrl-C
    return 0;
}
