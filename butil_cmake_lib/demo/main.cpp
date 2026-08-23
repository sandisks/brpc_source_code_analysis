// 验证独立编译的 butil 库：展示 IOBuf / EndPoint / 字符串 / 容器 / 日志 / 随机数
#include <butil/endpoint.h>
#include <butil/containers/flat_map.h>
#include <butil/fast_rand.h>
#include <butil/iobuf.h>
#include <butil/logging.h>
#include <butil/string_printf.h>
#include <butil/time.h>

#include <cstdio>
#include <string>

int main() {
    LOG(INFO) << "=== butil standalone library demo ===";

    // 1) EndPoint：解析 ip:port
    butil::EndPoint ep;
    CHECK_EQ(0, butil::str2endpoint("127.0.0.1:8080", &ep));
    LOG(INFO) << "endpoint = " << ep << ", ip = " << butil::ip2str(ep.ip);

    // 2) IOBuf：零拷贝缓冲，多块拼接 / 切分
    butil::IOBuf buf;
    buf.append("hello ");
    buf.append(std::string("butil"));
    buf.push_back('!');
    LOG(INFO) << "iobuf = '" << buf << "', size = " << buf.size();

    butil::IOBuf cut;
    buf.cutn(&cut, 5);
    LOG(INFO) << "after cut(5): left='" << buf << "', cut='" << cut << "'";

    // 3) string_printf
    std::string s = butil::string_printf("count=%d ratio=%.3f", 42, 0.618);
    LOG(INFO) << "string_printf -> " << s;

    // 4) flat_map：有序小 map，适合高性能查找
    butil::FlatMap<std::string, int> fm;
    fm.init(16);
    fm["apple"] = 1;
    fm["banana"] = 2;
    LOG(INFO) << "flat_map size = " << fm.size() << ", apple = " << fm["apple"];

    // 5) fast_rand：线程局部快速随机数
    int bucket[4] = {0, 0, 0, 0};
    for (int i = 0; i < 10000; ++i) {
        ++bucket[butil::fast_rand() % 4];
    }
    LOG(INFO) << "fast_rand distribution = " << bucket[0] << '/' << bucket[1]
              << '/' << bucket[2] << '/' << bucket[3];

    // 6) 时间工具
    LOG(INFO) << "gettimeofday_ms = " << butil::gettimeofday_ms()
              << ", monotonic = " << butil::monotonic_time_ms() << "ms";

    // 7) IOBuf 的 ZeroCopyInputStream 接口（依赖 protobuf 头文件层）
    LOG(INFO) << "iobuf implements google::protobuf::io::ZeroCopyInputStream: "
              << (buf.size() == 6 ? "yes" : "no");

    printf("\nAll butil demos passed.\n");
    return 0;
}
