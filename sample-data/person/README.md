# Synthetic person adapter examples

`import-demo.synthetic.json` is a separate **fictional operator-import example**, documented in `server/PERSON_IMPORT.md`. Its placeholder resource triple must be replaced with an exact registered source in a dedicated demo database, and importing requires `--allow-synthetic`. The adapter fixtures described below are not operator-import files.

These files are hand-authored, fictional test inputs. They contain **no real biography, confirmed real-world credit, news crawl, or production loader**. The JSON metadata explicitly sets `synthetic: true` and `productionLoaderEnabled: false`. Do not import this directory into a production person store or infer real associations from the invented display names.

`confirmed_people.synthetic.json` demonstrates stable opaque person/work IDs, two separate people named Alex Vale, English whitespace normalization, one person with three roles, duplicate roles, an unknown association, duplicate local media row IDs, multiple actual sources, and a directory entry outside the caller's readable scope. `mediaRowId` and `rawCsvActors` are explanatory fixture fields only; the adapter never reads or matches them.

`person_updates.synthetic.json` supplies structurally valid HTTPS/date examples to exercise the existing pure `personUpdates` cache selection function. `example.invalid` is a reserved example host, not an actual news source. Titles, dates and URLs were invented for validation, and no network request is made. A future release date is explicit; an unknown release date stays `null`, so it cannot become an invented calendar event. The fixed clock is UTC Unix seconds.

The production handoff is the typed `care_algorithms::PersonContext` supplied by a trusted server-owned source after authentication. `attachConfirmedPeople` matches **all three** of `workId`, `warehouseId` and `itemId` against the current authorized resources. It replaces current `people`, validates confirmed identities and roles, merges repeated roles deterministically, derives follow flags from the trusted context, and retains only currently visible followed IDs. It never reads a file, infers identity from a name or CSV ID, opens a database, or makes an external request. Store read failure must be represented by `dataAvailable=false` or an error at the server boundary.

Current resources with no exact confirmed mapping have `people=[]`. An explicit historical `workSnapshot` remains intact, including its original category, credits and empty people list. A historical event without a snapshot may use the algorithm's documented current-work fallback; the adapter itself never creates a historical snapshot.

Each exact work/source mapping accepts at most 256 normalized distinct person IDs (`MaxConfirmedPeoplePerWork`); duplicated role rows do not inflate that count. Exceeding the limit is an error so the server can apply the same bound when storing or reading historical credit snapshots.

Run the `care_person_snapshot` CTest target in a configured Qt build. Tests exercise the typed adapter and the fixture JSON, then call the existing person search, footprint and temporal recommendation functions. Follow bonus is `0.05` with personalization on; personalization off disables it, and not-interested feedback still excludes the work. No fixture is loaded by server production code.
