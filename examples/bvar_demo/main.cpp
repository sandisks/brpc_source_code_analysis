// bvar 演示（对应 bvar.md）：Adder + LatencyRecorder + 自定义指标
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <bvar/bvar.h>
#include <bthread/bthread.h>

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    // 累加器 + 每秒速率 + 60s 窗口
    bvar::Adder<int64_t> g_count("bvar_demo", "request_count");
    bvar::PerSecond<bvar::Adder<int64_t>> g_qps(&g_count, 1);
    bvar::LatencyRecorder g_latency("bvar_demo_op");

    for (int i = 0; i < 20; ++i) {
        g_count << 1;
        butil::Timer tm; tm.start();
        bthread_usleep(50000 + (i % 5) * 30000);   // 模拟 50~170ms 耗时
        tm.stop();
        g_latency << tm.u_elapsed();
        LOG(INFO) << "iter=" << i
                  << " count=" << g_count.get_value()
                  << " qps=" << g_qps.get_value()
                  << " latency_avg=" << g_latency.latency() << "us"
                  << " p99=" << g_latency.latency_percentile(0.99) << "us";
    }
    LOG(INFO) << "Done. 配合 Server 时打开 http://127.0.0.1:8000/vars 可查看";
    return 0;
}
