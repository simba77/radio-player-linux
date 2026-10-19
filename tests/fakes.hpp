#pragma once

// Двойник libmpv: запоминает вызовы, а события mpv тест испускает сам.
//
// Живёт отдельно, потому что нужен и тестам плеера, и тестам провайдеров,
// MPRIS и интерфейса.

#include "mpv_backend.hpp"

#include <QStringList>

namespace radio::test {

struct FakeMpv : MpvBackend {
    QStringList played;
    int stops = 0;
    int volume = -1;
    bool terminated = false;
    bool failOnPlay = false;

    QString play(const QString &url) override
    {
        if (failOnPlay)
            return QStringLiteral("поток не открылся");
        played.append(url);
        return {};
    }
    void stop() override { ++stops; }
    void setVolume(int value) override { volume = value; }
    void terminate() override { terminated = true; }
};

}  // namespace radio::test
