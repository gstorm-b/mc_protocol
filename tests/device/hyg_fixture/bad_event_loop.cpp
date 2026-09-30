// Negative control for QDV-HYG: never compiled. A nested event loop is forbidden.
#include <QEventLoop>

void nested() {
    QEventLoop loop;
    loop.exec();
}
