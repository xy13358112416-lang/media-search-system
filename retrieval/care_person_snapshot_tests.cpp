#include "care_person_snapshot.h"
#include "care_person.h"
#include "care_footprint.h"
#include "care_temporal.h"
#include "care_algorithm_common.h"
#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

using namespace care_contract;
using namespace care_algorithms;
namespace {
constexpr qint64 Now = 1794744000; // 2026-11-15T12:00:00Z, fixed synthetic clock.
ResourceSummary work(QString id = "wk_7a1", qint64 warehouse = 1, qint64 item = 10) {
    ResourceSummary value;
    value.resource = {id, {warehouse, item}};
    value.media.id = "42"; // Deliberately collides across every synthetic source.
    value.media.fileName = "Synthetic fixture work";
    value.media.actors = {"Alex Vale", "<script>alert(1)</script>"};
    value.media.creator = "Alex Vale";
    return value;
}
PersonSummary person(QString id = "ps_2c4", QString name = "Alex Vale",
                     QList<PersonRole> roles = {PersonRole::Actor}) {
    return {id, name, roles, true}; // Caller flags must be overwritten.
}
PersonalSnapshot personal(const QList<ResourceSummary>& works) {
    PersonalSnapshot snapshot;
    snapshot.userKey = "usr_5d8";
    snapshot.nowUtc = Now;
    snapshot.readableWorks = works;
    return snapshot;
}
ObservedBehavior view(QString id, const ResourceSummary& work, bool withSnapshot = true) {
    ObservedBehavior value;
    value.userKey = "usr_5d8";
    value.record.event = {id, BehaviorAction::View, work.resource, {}};
    value.record.occurredAtUtc = Now - 60;
    value.record.synthetic = true;
    if (withSnapshot) value.workSnapshot = work;
    return value;
}
}

class CarePersonSnapshotTests : public QObject {
    Q_OBJECT
private slots:
    void exactIdentity_data();
    void exactIdentity();
    void duplicateRolesAndNamesStayDistinct();
    void confirmedPeopleBound();
    void whitespaceTitlePreservesAcceptedMedia();
    void relaxedTitleValidation_data();
    void relaxedTitleValidation();
    void unrelatedContextDoesNotLeak();
    void malformedContext_data();
    void malformedContext();
    void conflictsFail();
    void unavailableFails();
    void historicalSnapshotsStayIntact();
    void footprintCountsConfirmedRoles();
    void temporalFollowPrecedence();
    void syntheticFixtureIsExplicit();
};

void CarePersonSnapshotTests::exactIdentity_data() {
    QTest::addColumn<QString>("creditWorkId");
    QTest::addColumn<qint64>("warehouse");
    QTest::addColumn<qint64>("item");
    QTest::addColumn<bool>("matches");
    QTest::newRow("exact-stable-resource") << QString("wk_7a1") << qint64(1) << qint64(10) << true;
    QTest::newRow("same-work-wrong-warehouse") << QString("wk_7a1") << qint64(2) << qint64(10) << false;
    QTest::newRow("same-work-wrong-item") << QString("wk_7a1") << qint64(1) << qint64(11) << false;
    QTest::newRow("same-source-wrong-work") << QString("wk_other") << qint64(1) << qint64(10) << false;
    QTest::newRow("csv-row-id-is-not-work-id") << QString("42") << qint64(1) << qint64(10) << false;
}
void CarePersonSnapshotTests::exactIdentity() {
    QFETCH(QString, creditWorkId); QFETCH(qint64, warehouse); QFETCH(qint64, item); QFETCH(bool, matches);
    auto current = work();
    current.people = {person("untrusted", "CSV guessed identity")};
    CatalogSnapshot input{true, {current}, {"untrusted"}};
    PersonContext context{true, {{{creditWorkId, {warehouse, item}}, {person()}}}, {"ps_2c4"}};
    const auto result = attachConfirmedPeople(input, context);
    QVERIFY(result.status.ok() && result.value);
    const auto& attached = result.value->readableWorks.first();
    QCOMPARE(attached.resource.workId, current.resource.workId);
    QCOMPARE(attached.resource.source.warehouseId, current.resource.source.warehouseId);
    QCOMPARE(attached.media.actors, current.media.actors);
    QCOMPARE(attached.people.size(), matches ? 1 : 0);
    QCOMPARE(input.readableWorks.first().people.first().personId, QString("untrusted"));
    if (matches) { QCOMPARE(attached.people.first().personId, QString("ps_2c4")); QVERIFY(attached.people.first().followed); }
    else QVERIFY(result.value->followedPeople.isEmpty());
}
void CarePersonSnapshotTests::duplicateRolesAndNamesStayDistinct() {
    auto a = work(); auto b = work("wk_7a1", 2, 10);
    const PersonContext context{true, {
        {a.resource, {person("ps_2c4", "  Alex   Vale ", {PersonRole::Director, PersonRole::Actor, PersonRole::Director}),
                      person("ps_2c4", "Alex Vale", {PersonRole::Musician}), person("ps_8f0", "Alex Vale")}},
        {b.resource, {person("ps_8f0", "Alex Vale")}}}, {"ps_2c4"}};
    const auto result = attachConfirmedPeople(CatalogSnapshot{true, {b, a}, {}}, context);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->readableWorks.size(), 2); // Never collapse sources before matching.
    QCOMPARE(result.value->readableWorks.first().resource.source.warehouseId, qint64(2));
    const auto& people = result.value->readableWorks.last().people;
    QCOMPARE(people.size(), 2);
    QCOMPARE(people.first().personId, QString("ps_2c4"));
    QCOMPARE(people.first().name, QString("Alex Vale"));
    QCOMPARE(people.first().roles, QList<PersonRole>({PersonRole::Actor, PersonRole::Director, PersonRole::Musician}));
    QVERIFY(people.first().followed); QVERIFY(!people.last().followed);
    const auto found = searchPeople({"  ALEX   VALE ", {}}, *result.value);
    QVERIFY(found.status.ok() && found.value);
    QCOMPARE(found.value->items.size(), 2);
}
void CarePersonSnapshotTests::unrelatedContextDoesNotLeak() {
    auto visible = work(); auto privateWork = work("wk_private", 99, 99);
    auto privatePerson = person("ps_hidden", "Other private name");
    const PersonContext context{true, {{visible.resource, {person()}}, {privateWork.resource, {privatePerson}}}, {"ps_hidden"}};
    const auto result = attachConfirmedPeople(CatalogSnapshot{true, {visible}, {}}, context);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->readableWorks.first().people.size(), 1);
    QVERIFY(!result.value->readableWorks.first().people.first().followed);
    QVERIFY(result.value->followedPeople.isEmpty());
    const auto hidden = worksForPerson("ps_hidden", {}, *result.value);
    QCOMPARE(hidden.status.code, warehouse_contract::ErrorCode::NotFound);
}
void CarePersonSnapshotTests::confirmedPeopleBound() {
    const auto current = work();
    PersonContext context{true, {{current.resource, {}}}, {}};
    for (int i = 0; i < 256; ++i) context.credits.first().people.append(person(QString("ps_%1").arg(i)));
    // The cap concerns normalized identities; repeated credits do not inflate it.
    context.credits.first().people.append(person("ps_0", "Alex Vale", {PersonRole::Director}));
    auto result = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, context);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->readableWorks.first().people.size(), 256);
    context.credits.first().people.append(person("ps_overflow"));
    result = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, context);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError);
    QVERIFY(!result.value);
}
void CarePersonSnapshotTests::whitespaceTitlePreservesAcceptedMedia() {
    auto current = work();
    current.media.fileName = "   "; // Existing warehouse media accepts a nonempty whitespace title.
    current.media.category = "  Original category  ";
    current.media.fileType = "MP4";
    current.media.fileSize = 12345;
    current.media.viewCount = 23;
    current.media.releaseDate = QDate(2025, 2, 3);
    current.media.awards = {"Synthetic original award"};
    const auto withoutDirectory = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, {});
    QVERIFY(withoutDirectory.status.ok() && withoutDirectory.value);
    QCOMPARE(withoutDirectory.value->readableWorks.first().media.fileName, current.media.fileName);
    QVERIFY(withoutDirectory.value->readableWorks.first().people.isEmpty());

    const PersonContext context{true, {{current.resource, {person()}}}, {"ps_2c4"}};
    const auto attached = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, context);
    QVERIFY(attached.status.ok() && attached.value);
    const auto& result = attached.value->readableWorks.first();
    QCOMPARE(result.media.id, current.media.id);
    QCOMPARE(result.media.fileName, current.media.fileName);
    QCOMPARE(result.media.category, current.media.category);
    QCOMPARE(result.media.fileType, current.media.fileType);
    QCOMPARE(result.media.fileSize, current.media.fileSize);
    QCOMPARE(result.media.viewCount, current.media.viewCount);
    QCOMPARE(result.media.releaseDate, current.media.releaseDate);
    QCOMPARE(result.media.creator, current.media.creator);
    QCOMPARE(result.media.actors, current.media.actors);
    QCOMPARE(result.media.awards, current.media.awards);
    QCOMPARE(result.people.size(), 1);
    QCOMPARE(result.people.first().personId, QString("ps_2c4"));
    QVERIFY(result.people.first().followed);
}
void CarePersonSnapshotTests::relaxedTitleValidation_data() {
    QTest::addColumn<int>("fault");
    const char* names[] = {"valid-untitled", "invalid-work-id", "invalid-source-id", "invalid-person-id",
        "invalid-role", "empty-person-name", "source-work-collision", "source-metadata-conflict"};
    for (int i = 0; i < 8; ++i) QTest::newRow(names[i]) << i;
}
void CarePersonSnapshotTests::relaxedTitleValidation() {
    QFETCH(int, fault);
    auto current = work(); current.media.fileName = "   "; current.people = {person()};
    QList<ResourceSummary> input{current};
    switch (fault) {
    case 1: input.first().resource.workId = "bad id"; break;
    case 2: input.first().resource.source.itemId = 0; break;
    case 3: input.first().people.first().personId = "bad id"; break;
    case 4: input.first().people.first().roles = {static_cast<PersonRole>(77)}; break;
    case 5: input.first().people.first().name = "   "; break;
    case 6: input.append(current); input.last().resource.workId = "wk_collision"; break;
    case 7: input.append(current); input.last().media.fileSize += 1; break;
    }
    const auto relaxed = detail::canonicalWorks(input, false);
    const auto strictDefault = detail::canonicalWorks(input);
    QCOMPARE(strictDefault.status.code, warehouse_contract::ErrorCode::ServerError);
    QVERIFY(!strictDefault.value); // Existing algorithm callers retain the title rule.
    if (fault == 0) {
        QVERIFY(relaxed.status.ok() && relaxed.value);
        QCOMPARE(relaxed.value->first().media.fileName, current.media.fileName);
    } else {
        QCOMPARE(relaxed.status.code, warehouse_contract::ErrorCode::ServerError);
        QVERIFY(!relaxed.value);
    }
}
void CarePersonSnapshotTests::malformedContext_data() {
    QTest::addColumn<int>("fault");
    const char* names[] = {"empty-id", "whitespace-id", "oversize-id", "empty-name", "control-name", "missing-role",
                           "invalid-role", "invalid-resource-id", "invalid-resource-source", "invalid-follow-id"};
    for (int i = 0; i < 10; ++i) QTest::newRow(names[i]) << i;
}
void CarePersonSnapshotTests::malformedContext() {
    QFETCH(int, fault);
    const auto current = work();
    PersonContext context{true, {{current.resource, {person()}}}, {}};
    auto& credit = context.credits.first(); auto& p = credit.people.first();
    switch (fault) {
    case 0: p.personId.clear(); break;
    case 1: p.personId = "bad id"; break;
    case 2: p.personId = QString(MaxIdentifierUnits + 1, 'a'); break;
    case 3: p.name = "   "; break;
    case 4: p.name = QString("Alex") + QChar(0) + "Vale"; break;
    case 5: p.roles.clear(); break;
    case 6: p.roles = {static_cast<PersonRole>(77)}; break;
    case 7: credit.resource.workId = "bad id"; break;
    case 8: credit.resource.source.itemId = 0; break;
    case 9: context.followedPeople.insert("bad id"); break;
    }
    const auto result = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, context);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError);
    QVERIFY(!result.value);
}
void CarePersonSnapshotTests::conflictsFail() {
    const auto current = work();
    auto conflicting = person(); conflicting.name = "Different canonical name";
    PersonContext context{true, {{current.resource, {person(), conflicting}}}, {}};
    auto result = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, context);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
    context.credits = {{current.resource, {person()}}, {current.resource, {person("ps_other")}}};
    result = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, context);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
    context.credits = {{current.resource, {person()}}, {current.resource, {person()}}};
    result = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, context);
    QVERIFY(result.status.ok() && result.value);
    auto collision = current; collision.resource.workId = "wk_collision";
    result = attachConfirmedPeople(CatalogSnapshot{true, {current, collision}, {}}, context);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
    auto other = work("wk_other", 2, 11);
    context.credits = {{current.resource, {person()}}, {other.resource, {conflicting}}};
    result = attachConfirmedPeople(CatalogSnapshot{true, {current, other}, {}}, context);
    QCOMPARE(result.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!result.value);
}
void CarePersonSnapshotTests::unavailableFails() {
    const auto current = work(); PersonContext unavailable; unavailable.dataAvailable = false;
    const auto catalog = attachConfirmedPeople(CatalogSnapshot{true, {current}, {}}, unavailable);
    QCOMPARE(catalog.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!catalog.value);
    const auto history = attachConfirmedPeople(personal({current}), unavailable);
    QCOMPARE(history.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!history.value);
    const auto catalogFailure = attachConfirmedPeople(CatalogSnapshot{false, {}, {}}, {});
    QCOMPARE(catalogFailure.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!catalogFailure.value);
    auto state = personal({current}); state.historyAvailable = false;
    const auto historyFailure = attachConfirmedPeople(state, {});
    QCOMPARE(historyFailure.status.code, warehouse_contract::ErrorCode::ServerError); QVERIFY(!historyFailure.value);
}
void CarePersonSnapshotTests::historicalSnapshotsStayIntact() {
    auto current = work(); auto previous = current;
    previous.media.category = "Historical category";
    previous.people = {person("ps_old", "Historical name", {PersonRole::Director})};
    auto unknown = current; unknown.people.clear();
    auto deleted = work("wk_deleted", 9, 90); deleted.people = {person("ps_deleted", "Deleted work credit")};
    auto state = personal({current});
    state.events = {view("ev_old", previous), view("ev_unknown", unknown), view("ev_deleted", deleted), view("ev_absent", current, false)};
    const PersonContext context{true, {{current.resource, {person()}}}, {"ps_2c4"}};
    const auto result = attachConfirmedPeople(state, context);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->events.size(), state.events.size());
    for (int i = 0; i < state.events.size(); ++i) {
        const auto& before = state.events[i]; const auto& after = result.value->events[i];
        QCOMPARE(after.userKey, before.userKey); QCOMPARE(after.record.event.eventId, before.record.event.eventId);
        QCOMPARE(after.record.occurredAtUtc, before.record.occurredAtUtc); QCOMPARE(after.record.synthetic, before.record.synthetic);
        QCOMPARE(after.workSnapshot.has_value(), before.workSnapshot.has_value());
        if (!before.workSnapshot) continue;
        QCOMPARE(after.workSnapshot->resource.workId, before.workSnapshot->resource.workId);
        QCOMPARE(after.workSnapshot->media.category, before.workSnapshot->media.category);
        QCOMPARE(after.workSnapshot->people.size(), before.workSnapshot->people.size());
        if (!before.workSnapshot->people.isEmpty()) {
            QCOMPARE(after.workSnapshot->people.first().personId, before.workSnapshot->people.first().personId);
            QCOMPARE(after.workSnapshot->people.first().name, before.workSnapshot->people.first().name);
            QCOMPARE(after.workSnapshot->people.first().roles, before.workSnapshot->people.first().roles);
            QCOMPARE(after.workSnapshot->people.first().followed, before.workSnapshot->people.first().followed);
        }
    }
    QVERIFY(result.value->events[1].workSnapshot->people.isEmpty());
    QVERIFY(!result.value->events.last().workSnapshot);
    QCOMPARE(result.value->readableWorks.first().people.size(), 1);
    QCOMPARE(result.value->readableWorks.first().people.first().personId, QString("ps_2c4"));
}
void CarePersonSnapshotTests::footprintCountsConfirmedRoles() {
    const auto current = work();
    auto state = personal({current});
    state.events = {view("ev_current", current, false)};
    auto unknown = current; unknown.people.clear(); state.events.append(view("ev_unknown", unknown));
    PersonContext context{true, {{current.resource, {person("ps_2c4", "Alex Vale", {PersonRole::Director, PersonRole::Actor}),
        person("ps_2c4", "Alex Vale", {PersonRole::Actor}), person("ps_8f0", "Alex Vale")}}}, {}};
    const auto attached = attachConfirmedPeople(state, context);
    QVERIFY(attached.status.ok() && attached.value);
    const auto report = buildFootprint({{Now - 3600, Now + 1}, {}}, *attached.value);
    QVERIFY(report.status.ok() && report.value);
    QCOMPARE(report.value->current.views, qint64(2));
    QCOMPARE(report.value->current.people.size(), 3);
    for (const auto& count : report.value->current.people) QCOMPARE(count.count, qint64(1));
    QVERIFY(report.value->containsSyntheticData);
}
void CarePersonSnapshotTests::temporalFollowPrecedence() {
    const auto neutral = work("wk_a", 1, 10); const auto followed = work("wk_b", 1, 11);
    auto state = personal({neutral, followed});
    PersonContext context{true, {{followed.resource, {person()}}}, {"ps_2c4"}};
    const auto attached = attachConfirmedPeople(state, context);
    QVERIFY(attached.status.ok() && attached.value);
    auto result = recommendTemporal({}, *attached.value);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->items.size(), 1);
    QCOMPARE(result.value->items.first().work.resource.workId, QString("wk_b"));
    QVERIFY(result.value->items.first().reason.contains(QStringLiteral("主动关注=0.050000")));
    auto disabled = *attached.value; disabled.settings.personalizationEnabled = false;
    result = recommendTemporal({}, disabled);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->items.size(), 2);
    QCOMPARE(result.value->items.first().work.resource.workId, QString("wk_a"));
    for (const auto& item : result.value->items) QVERIFY(item.reason.contains(QStringLiteral("主动关注=0.000000")));
    auto excluded = *attached.value; excluded.notInterestedWorkIds.insert("wk_b");
    result = recommendTemporal({}, excluded);
    QVERIFY(result.status.ok() && result.value);
    QCOMPARE(result.value->items.size(), 1);
    QCOMPARE(result.value->items.first().work.resource.workId, QString("wk_a"));
}
void CarePersonSnapshotTests::syntheticFixtureIsExplicit() {
    QFile input(QStringLiteral(PERSON_FIXTURE_DIR "/confirmed_people.synthetic.json"));
    QVERIFY(input.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    const auto root = document.object();
    QVERIFY(root.value("synthetic").toBool()); QVERIFY(!root.value("productionLoaderEnabled").toBool(true));
    QVERIFY(root.value("credits").toArray().size() >= 3);
    PersonContext context;
    for (const auto& row : root.value("credits").toArray()) {
        const auto object = row.toObject();
        const auto resource = object.value("resource").toObject();
        const auto source = resource.value("source").toObject();
        ConfirmedWorkPeople credit;
        credit.resource = {resource.value("workId").toString(),
                           {source.value("warehouseId").toInteger(), source.value("itemId").toInteger()}};
        for (const auto& entry : object.value("people").toArray()) {
            const auto data = entry.toObject();
            PersonSummary person{data.value("personId").toString(), data.value("name").toString(), {}, false};
            for (const auto& role : data.value("roles").toArray()) {
                const auto name = role.toString();
                QVERIFY(name == "Actor" || name == "Director" || name == "Musician");
                person.roles.append(name == "Actor" ? PersonRole::Actor : name == "Director" ? PersonRole::Director : PersonRole::Musician);
            }
            credit.people.append(person);
        }
        context.credits.append(credit);
    }
    for (const auto& id : root.value("followedPeople").toArray()) context.followedPeople.insert(id.toString());
    const auto fixtureCatalog = attachConfirmedPeople(CatalogSnapshot{true, {work(), work("wk_4b9", 1, 11)}, {}}, context);
    QVERIFY(fixtureCatalog.status.ok() && fixtureCatalog.value);
    QCOMPARE(fixtureCatalog.value->readableWorks.first().people.size(), 2);
    QCOMPARE(fixtureCatalog.value->readableWorks.first().people.first().roles.size(), 3);
    QVERIFY(fixtureCatalog.value->readableWorks.last().people.isEmpty());
    QVERIFY(!fixtureCatalog.value->followedPeople.contains("ps_6d3"));
    QFile updates(QStringLiteral(PERSON_FIXTURE_DIR "/person_updates.synthetic.json"));
    QVERIFY(updates.open(QIODevice::ReadOnly));
    const auto updatesDoc = QJsonDocument::fromJson(updates.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(updatesDoc.object().value("synthetic").toBool());
    QVERIFY(updatesDoc.object().value("updates").toArray().size() >= 3);
    QList<PersonUpdate> cache;
    for (const auto& row : updatesDoc.object().value("updates").toArray()) {
        const auto object = row.toObject();
        PersonUpdate update;
        update.updateId = object.value("updateId").toString();
        update.personId = object.value("personId").toString();
        update.title = object.value("title").toString();
        update.kind = object.value("kind").toString() == "News" ? UpdateKind::News : UpdateKind::Release;
        update.sourceUrl = QUrl(object.value("sourceUrl").toString());
        if (object.value("publishedAtUtc").isDouble()) update.publishedAtUtc = object.value("publishedAtUtc").toInteger();
        if (object.value("eventAtUtc").isDouble()) update.eventAtUtc = object.value("eventAtUtc").toInteger();
        update.fetchedAtUtc = object.value("fetchedAtUtc").toInteger();
        update.stale = object.value("stale").toBool();
        cache.append(update);
    }
    const auto chosen = personUpdates({"ps_2c4", {Now - 86400 * 2, Now + 86400 * 8}, {}},
                                      *fixtureCatalog.value, cache, Now);
    QVERIFY(chosen.status.ok() && chosen.value);
    QCOMPARE(chosen.value->items.size(), 3);
    for (const auto& update : chosen.value->items) {
        QCOMPARE(update.sourceUrl.scheme(), QString("https"));
        QCOMPARE(update.sourceUrl.host(), QString("example.invalid"));
        QVERIFY(update.fetchedAtUtc <= Now);
        if (update.updateId == "up_9c0") { QVERIFY(!update.publishedAtUtc); QVERIFY(!update.eventAtUtc); QVERIFY(update.stale); }
        if (update.updateId == "up_5b8") { QVERIFY(update.eventAtUtc); QVERIFY(*update.eventAtUtc > Now); }
    }
}

QTEST_GUILESS_MAIN(CarePersonSnapshotTests)
#include "care_person_snapshot_tests.moc"
