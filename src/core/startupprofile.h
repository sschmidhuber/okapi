#pragma once

#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

namespace okapi::startupProfile {

// Env-gated startup profiling (OKAPI_PROFILE_STARTUP=1): mark() prints the
// milliseconds elapsed since start() at each checkpoint, so startup
// optimizations can be measured before/after a change.

inline QElapsedTimer &timer() {
    static QElapsedTimer t;
    return t;
}

[[nodiscard]] inline bool enabled() {
    static const bool on = qEnvironmentVariableIsSet("OKAPI_PROFILE_STARTUP");
    return on;
}

inline void start() {
    if (enabled())
        timer().start();
}

inline void mark(const QString &label) {
    if (enabled())
        qWarning().noquote() << "[okapi startup]" << label << timer().elapsed() << "ms";
}

} // namespace okapi::startupProfile
