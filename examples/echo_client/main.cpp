// 最小 bRPC 客户端示例（对应 client_basics.md）
// 演示：同步调用 + 异步调用
#include <iostream>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <brpc/channel.h>
#include <brpc/controller.h>
#include <brpc/callback.h>
#include "echo.pb.h"

DEFINE_string(target, "127.0.0.1:8000", "server address (可带 list:// 前缀+lb)");
DEFINE_int32(timeout_ms, 1000, "RPC timeout in ms");
DEFINE_int32(count, 3, "number of requests");

// 异步回调（brpc::NewCallback 自动管理 cntl/res 的生命周期并 self-delete）
void OnEchoDone(brpc::Controller* cntl,
                example::EchoResponse* res) {
    if (cntl->Failed()) {
        LOG(ERROR) << "Async RPC failed: " << cntl->ErrorText();
    } else {
        LOG(INFO) << "[async] got=\"" << res->message() << "\"";
    }
}

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = FLAGS_timeout_ms;
    // Init(url, load_balancer_name, options)：lb 为空表示直连单点
    if (channel.Init(FLAGS_target.c_str(), "", &options) != 0) {
        LOG(ERROR) << "Fail to init channel to " << FLAGS_target;
        return -1;
    }
    example::EchoService_Stub stub(&channel);

    // 1) 同步调用
    for (int i = 0; i < FLAGS_count; ++i) {
        brpc::Controller cntl;
        example::EchoRequest req;
        example::EchoResponse res;
        req.set_message("hello-" + std::to_string(i));
        butil::Timer tm; tm.start();
        stub.Echo(&cntl, &req, &res, nullptr);   // nullptr => 同步
        tm.stop();
        if (cntl.Failed()) {
            LOG(ERROR) << "Sync RPC failed: " << cntl.ErrorText();
        } else {
            LOG(INFO) << "[sync] got=\"" << res.message()
                      << "\" cost=" << tm.u_elapsed() << "us";
        }
    }

    // 2) 异步调用
    for (int i = 0; i < FLAGS_count; ++i) {
        brpc::Controller* cntl = new brpc::Controller;
        example::EchoRequest* req = new example::EchoRequest;
        example::EchoResponse* res = new example::EchoResponse;
        req->set_message("async-" + std::to_string(i));
        // brpc::NewCallback 绑定回调参数；cntl/res 由闭包持有并自动释放
        stub.Echo(cntl, req, res,
                  brpc::NewCallback(&OnEchoDone, cntl, res));
    }

    // 给异步调用一点时间完成
    bthread_usleep(500000);
    LOG(INFO) << "client done.";
    return 0;
}
