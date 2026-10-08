/**
 * @file qt_compat.h
 * @brief What the workbench needs to build the same with Qt 5.15 and Qt 6: the two Qt APIs with no
 * common form (a JSON number as an integer, the encoding of a `QTextStream` on a device) and a
 * `QObject::connect()` that MSVC 14.44 accepts inside a lambda.
 *
 * Every other difference between the two majors is written in a form both accept; jsonInteger()
 * and useUtf8() hold the only Qt 5 / Qt 6 branches of `tools/mc_workbench`. (`log_model.cpp` has
 * its own `QT_VERSION_CHECK` branches, for an API Qt 6.9 deprecates.)
 */
#pragma once

#include <QJsonValue>
#include <QMetaObject>
#include <QObject>
#include <QTextStream>
#include <QtGlobal>

#include <cmath>
#include <utility>

namespace mc::workbench {

/**
 * @brief Calls the static `QObject::connect()` with @p args, for lambdas inside a member function
 * of a `QObject` that do not capture `this` (the runner factories of the hosts, the start-up
 * lambda of `RunnerThread`).
 *
 * There MSVC 14.44, the toolset of the Qt 5 builds, warns C4573 on `QObject::connect()`: the name
 * also finds QObject's non-static `connect()` overload, which would need `this`. Called from
 * namespace scope, as here, the same call is unambiguous. No Qt version check: the same code for
 * both majors.
 *
 * @param[in] args The arguments of `QObject::connect()`, forwarded unchanged.
 * @return The connection `QObject::connect()` returns.
 */
template <typename... Args> QMetaObject::Connection staticConnect(Args&&... args) {
    return QObject::connect(std::forward<Args>(args)...);
}

/**
 * @brief A JSON number as a 64-bit integer, with the result of Qt 6's `QJsonValue::toInteger()`.
 *
 * Qt 6 calls `toInteger()`. Qt 5.15 has no such function (it stores every JSON number as a
 * double), so the same rule is applied to the double.
 *
 * @param[in] value Any JSON value.
 * @return The value when it is a whole number that fits in `qint64`; otherwise 0 (strings,
 * booleans, fractions, out-of-range numbers).
 */
inline qint64 jsonInteger(const QJsonValue& value) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return value.toInteger();
#else
    if (!value.isDouble()) {
        return 0;
    }
    const double number = value.toDouble();
    // -2^63 <= number < 2^63 (NaN fails both), and no fraction.
    if (number >= -9223372036854775808.0 && number < 9223372036854775808.0 &&
        std::trunc(number) == number) {
        return static_cast<qint64>(number);
    }
    return 0;
#endif
}

/**
 * @brief Makes @p stream read and write UTF-8, the default of Qt 6.
 *
 * Qt 6 already uses UTF-8, so this does nothing there. Qt 5.15 uses the locale's codec (the ANSI
 * code page on Windows) unless told otherwise.
 *
 * @param[in,out] stream A stream on a device whose bytes are UTF-8.
 */
inline void useUtf8(QTextStream& stream) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#else
    Q_UNUSED(stream);
#endif
}

} // namespace mc::workbench
