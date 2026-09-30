// Negative control for QDV-HYG: never compiled. The library starts no thread of its own.
#include <QThread>

void spawn() {
    QThread worker;
    worker.start();
}
