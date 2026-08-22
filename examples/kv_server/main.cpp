// brpc + leveldb KV 服务端示例
// 把 leveldb 当作服务端存储引擎：Set 写入、Get 读取，通过 brpc 暴露成 RPC 服务
// 编译见仓库根目录 CMakeLists.txt
#include <iostream>
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <brpc/server.h>
#include <leveldb/db.h>
#include "kv.pb.h"

DEFINE_int32(port, 8004, "TCP Port of this server");
DEFINE_string(db, "/tmp/brpc_kv_demo", "leveldb database path");

class KvServiceImpl : public example::KvService {
public:
    explicit KvServiceImpl(leveldb::DB* db) : _db(db) {}
    ~KvServiceImpl() {}

    void Get(google::protobuf::RpcController* controller,
             const example::KvGetRequest* request,
             example::KvGetResponse* response,
             google::protobuf::Closure* done) override {
        // ClosureGuard：RAII 方式确保 done->Run() 一定被调用
        brpc::ClosureGuard done_guard(done);

        std::string value;
        leveldb::Status st = _db->Get(leveldb::ReadOptions(), request->key(), &value);
        if (st.ok()) {
            response->set_value(value);
            response->set_found(true);
        } else if (st.IsNotFound()) {
            response->set_found(false);   // 键不存在不是错误
        } else {
            LOG(ERROR) << "[server] leveldb get failed key=" << request->key()
                       << " err=" << st.ToString();
        }
    }

    void Set(google::protobuf::RpcController* controller,
             const example::KvSetRequest* request,
             example::KvSetResponse* response,
             google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);

        leveldb::Status st = _db->Put(leveldb::WriteOptions(),
                                      request->key(), request->value());
        response->set_ok(st.ok());
        if (st.ok()) {
            LOG(INFO) << "[server] set key=\"" << request->key()
                      << "\" value=\"" << request->value() << "\"";
        } else {
            LOG(ERROR) << "[server] leveldb put failed err=" << st.ToString();
        }
    }

private:
    leveldb::DB* _db;
};

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // 打开 leveldb（create_if_missing：目录不存在则创建）
    leveldb::DB* db = nullptr;
    leveldb::Options opts;
    opts.create_if_missing = true;
    leveldb::Status st = leveldb::DB::Open(opts, FLAGS_db, &db);
    if (!st.ok()) {
        LOG(ERROR) << "Fail to open leveldb at " << FLAGS_db
                   << ": " << st.ToString();
        return -1;
    }
    LOG(INFO) << "leveldb opened at " << FLAGS_db;

    brpc::Server server;
    KvServiceImpl kv_service(db);
    if (server.AddService(&kv_service,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add KvService";
        delete db;
        return -1;
    }

    brpc::ServerOptions options;
    options.idle_timeout_sec = -1;   // 不断开空闲连接
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start server on port " << FLAGS_port;
        delete db;
        return -1;
    }
    LOG(INFO) << "KvServer listening on port " << FLAGS_port;

    server.RunUntilAskedToQuit();     // 阻塞，直到 Ctrl-C
    delete db;
    return 0;
}
