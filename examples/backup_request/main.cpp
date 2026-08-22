// Backup Request 演示（对应 backup_request.md）
// client 设 backup_request_ms，命中慢请求时自动发备份
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/time.h>
#include <brpc/channel.h>
#include "echo.pb.h"

DEFINE_string(target, "list://127.0.0.1:8003,127.0.0.1:8004", "servers");
DEFINE_int32(backup_request_ms, 2, "send backup after this ms");
DEFINE_int32(count, 10, "number of requests");

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Channel channel;
    brpc::ChannelOptions options;
    options.protocol = "baidu_std";
    options.timeout_ms = 100;
    options.backup_request_ms = FLAGS_backup_request_ms;  // 关键
    // Init(url, load_balancer_name, options)：list:// + la 做备份请求的流量分发
    if (channel.Init(FLAGS_target.c_str(), "la", &options) != 0) {
        LOG(ERROR) << "Fail to init channel";
        return -1;
    }
    example::EchoService_Stub stub(&channel);

    for (int i = 0; i < FLAGS_count; ++i) {
        brpc::Controller cntl;
        example::EchoRequest req;
        example::EchoResponse res;
        req.set_message("backup-" + std::to_string(i));
        butil::Timer tm; tm.start();
        stub.Echo(&cntl, &req, &res, nullptr);
        tm.stop();
        if (cntl.Failed()) {
            LOG(ERROR) << "[" << i << "] failed: " << cntl.ErrorText();
        } else {
            LOG(INFO) << "[" << i << "] got=\"" << res.message()
                      << "\" cost=" << tm.u_elapsed() << "us"
                      << " (backup_request_ms="
                      << cntl.backup_request_ms() << ")";
        }
    }
    return 0;
}
