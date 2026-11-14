#pragma once

#include "mediarecord.h"
#include <QHash>

namespace media_mood {

enum class Mood { Any, Relaxing, Resonant, Energizing, Nostalgic };

inline QString label(Mood mood)
{
    switch (mood) {
    case Mood::Any: return QStringLiteral("不限氛围");
    case Mood::Relaxing: return QStringLiteral("轻松一下");
    case Mood::Resonant: return QStringLiteral("寻找共鸣");
    case Mood::Energizing: return QStringLiteral("提振精神");
    case Mood::Nostalgic: return QStringLiteral("怀旧片刻");
    }
    return {};
}

inline bool matches(const MediaRecord& record, Mood mood)
{
    // Editorial tags for the included sample catalog, keyed by stable media ID.
    static const QHash<QString, QList<Mood>> tags{
        {"uci:AA13", {Mood::Relaxing}},
        {"uci:AA16", {Mood::Relaxing, Mood::Nostalgic}},
        {"uci:AA29", {Mood::Relaxing, Mood::Nostalgic}},
        {"tvmaze:8", {Mood::Resonant, Mood::Energizing}},
        {"tvmaze:49", {Mood::Relaxing, Mood::Energizing}},
        {"tvmaze:58", {Mood::Relaxing}},
        {"tvmaze:80", {Mood::Relaxing, Mood::Resonant}},
        {"musicbrainz:217f8c48-b9e8-4b55-9a92-d7df82362a66", {Mood::Resonant, Mood::Nostalgic}},
        {"musicbrainz:df124fff-1c3b-4ac4-a305-4540abdd907b", {Mood::Energizing}},
        {"musicbrainz:5d3d6af1-8c4e-4fdb-a7e9-70e819d7b0db", {Mood::Energizing}},
        {"musicbrainz:7cb72d02-4186-49ae-857c-3b8344755135", {Mood::Energizing}},
        {"musicbrainz:efa1ec75-f467-48cc-a792-1a4adbeac590", {Mood::Relaxing, Mood::Resonant, Mood::Nostalgic}}
    };
    const auto it = tags.constFind(record.id);
    return mood == Mood::Any || (it != tags.cend() && it->contains(mood));
}

} // namespace media_mood
