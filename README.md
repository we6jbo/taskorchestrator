# Task Orchestrator v0.3

Task Orchestrator now publishes one atomic JSON event file per registered task under `~/.taskorchestrator/events/` so another local program can read the tasks, add them to a calendar, and provide execution controls without depending on Task Orchestrator's internal task store.

Registering CasePath with:

`taskorchestrator register --id casepath --name "CasePath" --exec /home/we6jbo/.local/bin/casepath --priority 75 --duration 45 --cadence daily`

creates or updates:

`~/.taskorchestrator/events/casepath.json`

Each event includes ID, name, executable, arguments, priority, duration, schedule constraints, state, next eligible time, provenance, and two actions:

`taskorchestrator run --id casepath`

`taskorchestrator run-terminal --id casepath`

The second action is intended for a calendar/dashboard Execute button. The Qt Task Orchestrator GUI also runs selected tasks through a terminal window.

Event writes use `QSaveFile`, so a reader should not see a partially written JSON file. Removing a registration removes the corresponding event file. Existing registrations are refreshed into the events directory whenever Task Orchestrator starts.

Use `taskorchestrator events` to print the events directory.

TG/AKA provenance remains enabled with `AKA_TE324543` and the bundled `tg_context_snapshot.json`.
