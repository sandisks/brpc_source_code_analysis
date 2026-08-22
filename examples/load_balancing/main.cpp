// Load Balancing 演示（对应 load_balancing.md）
// 用 list:// 指向两个 server，负载均衡算法 la（Locality-Aware）
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include "echo.pb.h"

DEFINE_string(target, "list://127.0.0.1:8005,127.0.0.1:8006", "servers");
DEFINE_string(lb, "la", "load balancer name: rr/random/la/c_murmurhash");
DEFINE_int32(count, 10, "number of requests");

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = 1000;
    // 负载均衡算法作为 Init 的第 2 个参数：rr/random/la/c_murmurhash 等
    if (channel.Init(FLAGS_target.c_str(), FLAGS_lb.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to init channel with lb=" << FLAGS_lb;
        return -1;
    }
    example::EchoService_Stub stub(&channel);

    for (int i = 0; i < FLAGS_count; ++i) {
        brpc::Controller cntl;
        example::EchoRequest req;
        example::EchoResponse res;
        req.set_message("lb-" + std::to_string(i));
        stub.Echo(&cntl, &req, &res, nullptr);
        if (cntl.Failed()) {
            LOG(ERROR) << "[" << i << "] failed: " << cntl.ErrorText();
        } else {
            LOG(INFO) << "[" << i << "] got=\"" << res.message()
                      << "\" routed_to=" << cntl.remote_side();
        }
    }
    return 0;
}
