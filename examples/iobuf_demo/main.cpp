// IOBuf 演示（对应 iobuf.md）：append / cutn / 零拷贝拼接
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <butil/iobuf.h>

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    butil::IOBuf buf;
    buf.append("hello");
    buf.append(" world");
    LOG(INFO) << "buf=" << buf << " length=" << buf.length();

    // cutn：切走前 5 字节（拷贝到裸指针）
    char head[8] = {0};
    size_t n = buf.cutn(head, 5);
    LOG(INFO) << "cut " << n << " bytes: \"" << head << "\"";
    LOG(INFO) << "remaining=" << buf;

    // 零拷贝拼接另一个 IOBuf
    butil::IOBuf tail;
    tail.append(" + tail");
    buf.append(tail);
    LOG(INFO) << "after append another IOBuf: " << buf;

    // 切给另一个 IOBuf（零拷贝）
    butil::IOBuf piece;
    buf.cutn(&piece, 5);
    LOG(INFO) << "piece=" << piece << " rest=" << buf;
    return 0;
}
