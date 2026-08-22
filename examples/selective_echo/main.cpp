// SelectiveChannel 演示（对应 combo_channels.md）
// 同时发到两个子 channel，取最快返回的那个
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include <brpc/selective_channel.h>
#include "echo.pb.h"

DEFINE_int32(port1, 8001, "first server port");
DEFINE_int32(port2, 8002, "second server port");
DEFINE_int32(count, 3, "number of requests");

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    brpc::SelectiveChannel channel;
    brpc::ChannelOptions sc_opt;
    sc_opt.timeout_ms = 500;
    // 第 1 个参数是负载均衡算法：rr/random 等，把请求分发给子 channel
    if (channel.Init("rr", &sc_opt) != 0) {
        LOG(ERROR) << "Fail to init SelectiveChannel";
        return -1;
    }

    brpc::ChannelOptions copt;
    copt.protocol = "baidu_std";
    copt.timeout_ms = 500;

    brpc::Channel* sub1 = new brpc::Channel();
    brpc::Channel* sub2 = new brpc::Channel();
    std::string a1 = "127.0.0.1:" + std::to_string(FLAGS_port1);
    std::string a2 = "127.0.0.1:" + std::to_string(FLAGS_port2);
    if (sub1->Init(a1.c_str(), &copt) != 0 ||
        sub2->Init(a2.c_str(), &copt) != 0) {
        LOG(ERROR) << "Fail to init sub channels";
        return -1;
    }
    // 两个子 channel 指向两个独立 echo server（AddChannel 的第二个参数是句柄，可为空）
    channel.AddChannel(sub1, NULL);
    channel.AddChannel(sub2, NULL);

    example::EchoService_Stub stub(&channel);
    for (int i = 0; i < FLAGS_count; ++i) {
        brpc::Controller cntl;
        example::EchoRequest req;
        example::EchoResponse res;
        req.set_message("selective-" + std::to_string(i));
        stub.Echo(&cntl, &req, &res, nullptr);
        if (cntl.Failed()) {
            LOG(ERROR) << "Selective RPC failed: " << cntl.ErrorText();
        } else {
            LOG(INFO) << "[selective] got=\"" << res.message()
                      << "\" from=" << cntl.remote_side();
        }
    }
    return 0;
}
