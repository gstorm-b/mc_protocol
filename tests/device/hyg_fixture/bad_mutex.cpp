// Negative control for QDV-HYG: never compiled. The library takes no mutex.
#include <QMutex>

void guarded() {
    static QMutex lock;
    lock.lock();
    lock.unlock();
}
