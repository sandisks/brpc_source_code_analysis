// bthread 演示（对应 bthread.md）：start_background + join + bthread mutex
#include <gflags/gflags.h>
#include <butil/logging.h>
#include <bthread/bthread.h>

static bthread_mutex_t g_mutex;
static int g_shared = 0;

void* worker(void* arg) {
    int id = *(int*)arg;
    bthread_mutex_lock(&g_mutex);
    g_shared += id;
    LOG(INFO) << "bthread id=" << id << " shared=" << g_shared;
    bthread_mutex_unlock(&g_mutex);
    return nullptr;
}

int main(int argc, char* argv[]) {
    GFLAGS_NAMESPACE::ParseCommandLineFlags(&argc, &argv, true);

    bthread_mutex_init(&g_mutex, nullptr);

    const int N = 4;
    bthread_t tids[N];
    int args[N];
    for (int i = 0; i < N; ++i) {
        args[i] = i + 1;
        bthread_start_background(&tids[i], nullptr, worker, &args[i]);
    }
    for (int i = 0; i < N; ++i) {
        bthread_join(tids[i], nullptr);
    }
    LOG(INFO) << "all bthreads joined, g_shared=" << g_shared;
    bthread_mutex_destroy(&g_mutex);
    return 0;
}
