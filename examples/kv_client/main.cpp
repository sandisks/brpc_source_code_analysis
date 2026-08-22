// brpc + leveldb KV 客户端示例
// 通过 RPC 调用 kv_server：先 Set 写入，再 Get 读回，并读取一个不存在的键
#include <iostream>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/channel.h>
#include "kv.pb.h"

DEFINE_string(target, "127.0.0.1:8004", "kv server address");
DEFINE_int32(timeout_ms, 1000, "RPC timeout in ms");

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
    example::KvService_Stub stub(&channel);

    // 1) Set 写入两对键值
    const char* keys[] = {"greeting", "answer"};
    const char* vals[] = {"hello from brpc+leveldb", "42"};
    for (int i = 0; i < 2; ++i) {
        brpc::Controller cntl;
        example::KvSetRequest req;
        example::KvSetResponse res;
        req.set_key(keys[i]);
        req.set_value(vals[i]);
        stub.Set(&cntl, &req, &res, nullptr);   // nullptr => 同步
        if (cntl.Failed()) {
            LOG(ERROR) << "Set " << keys[i] << " RPC failed: " << cntl.ErrorText();
        } else {
            LOG(INFO) << "Set " << keys[i] << " => " << vals[i]
                      << " (ok=" << res.ok() << ")";
        }
    }

    // 2) Get 读回
    for (int i = 0; i < 2; ++i) {
        brpc::Controller cntl;
        example::KvGetRequest req;
        example::KvGetResponse res;
        req.set_key(keys[i]);
        stub.Get(&cntl, &req, &res, nullptr);
        if (cntl.Failed()) {
            LOG(ERROR) << "Get " << keys[i] << " RPC failed: " << cntl.ErrorText();
        } else {
            LOG(INFO) << "Get " << keys[i] << " => found=" << res.found()
                      << " value=\"" << res.value() << "\"";
        }
    }

    // 3) 读一个不存在的键（演示 IsNotFound 路径）
    {
        brpc::Controller cntl;
        example::KvGetRequest req;
        example::KvGetResponse res;
        req.set_key("not-exist");
        stub.Get(&cntl, &req, &res, nullptr);
        if (cntl.Failed()) {
            LOG(ERROR) << "Get not-exist RPC failed: " << cntl.ErrorText();
        } else {
            LOG(INFO) << "Get not-exist => found=" << res.found();
        }
    }

    LOG(INFO) << "kv_client done.";
    return 0;
}
