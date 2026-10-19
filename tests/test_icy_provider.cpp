// ICY-метаданные прямых потоков: что libmpv отдаёт в `metadata`.

#include "fakes.hpp"
#include "metadata/icy.hpp"
#include "support.hpp"

#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

using namespace radio;
using namespace Qt::StringLiterals;

namespace {

const Station STATION{u"Поток"_s, u"https://radio.invalid/stream.mp3"_s, u"s"_s};

struct IcyFixture {
    test::FakeMpv mpv;
    IcyProvider provider{&mpv};
    QSignalSpy tracks{&provider, &MetadataProvider::trackChanged};
    QSignalSpy cleared{&provider, &MetadataProvider::trackCleared};
};

}  // namespace

TEST_CASE_METHOD(IcyFixture, "an icy title becomes a track")
{
    provider.start(STATION);
    emit mpv.metadataChanged({{u"icy-title"_s, u"TIESTO - Both"_s}, {u"icy-pub"_s, u"1"_s}});
    REQUIRE(tracks.count() == 1);
    const auto track = tracks[0][0].value<TrackInfo>();
    CHECK(track.artist == "TIESTO"_L1);
    CHECK(track.title == "Both"_L1);
}

TEST_CASE_METHOD(IcyFixture, "a plain title is used when there is no icy title")
{
    provider.start(STATION);
    emit mpv.metadataChanged({{u"title"_s, u"Просто название"_s}});
    REQUIRE(tracks.count() == 1);
    CHECK(tracks[0][0].value<TrackInfo>().title == u"Просто название"_s);
}

TEST_CASE_METHOD(IcyFixture, "a repeated title is not a new track")
{
    provider.start(STATION);
    emit mpv.metadataChanged({{u"icy-title"_s, u"A - B"_s}});
    emit mpv.metadataChanged({{u"icy-title"_s, u"A - B"_s}});
    CHECK(tracks.count() == 1);
}

TEST_CASE_METHOD(IcyFixture, "empty metadata is silence, not an error")
{
    provider.start(STATION);
    emit mpv.metadataChanged({});
    CHECK(tracks.isEmpty());
    CHECK(cleared.isEmpty());
}

TEST_CASE_METHOD(IcyFixture, "a stopped provider no longer listens")
{
    provider.start(STATION);
    emit mpv.metadataChanged({{u"icy-title"_s, u"A - B"_s}});
    provider.stop();
    CHECK(cleared.count() == 1);

    emit mpv.metadataChanged({{u"icy-title"_s, u"C - D"_s}});
    CHECK(tracks.count() == 1);
}

TEST_CASE_METHOD(IcyFixture, "a provider that never heard a track clears nothing")
{
    provider.start(STATION);
    provider.stop();
    CHECK(cleared.isEmpty());
}
