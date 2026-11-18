# cbftp-svn

FTP site automation tool: spreads releases across racing FTP sites and tracks their progress.

## Language

### Spread jobs

**Spread job**:
A release being pushed to a set of sites as one coordinated unit.
_Avoid_: spread race, release job

**Spread profile**:
The strategy a spread job uses: race (win per-site races), distribute (all sites get everything, no race urgency), prepare (stage sites, start racing later).
_Avoid_: mode, type

**Section**:
A category of releases on the sites, with its own skip list rules.
_Avoid_: category, channel

### Racing

**Race**:
The per-site competition on an affiliate site to be first to complete a release against other racers.
_Avoid_: contest

**Win (a race)**:
Completing the release on a site before any other racer completes it there.
_Avoid_: place, rank

**Local completion**:
All files of the spread job present on one site.
_Avoid_: site done

**Global completion**:
All racing sites at local completion for the spread job.
_Avoid_: job done

**File win**:
A release file on a site whose listing owner is our user — first racer to upload it keeps it.
_Avoid_: file won, owned file

**File loss**:
A release file on a site owned by another racer that appeared during the race window.
_Avoid_: missed file

**Owned percentage**:
Share of the release's files already present on a given site (any racer's uploads count).
_Avoid_: progress, completeness

### Transfers

**FXP**:
Server-to-server transfer between two sites; data flows directly between the servers, never through the client.
_Avoid_: site-to-site transfer

**In-memory transfer**:
Site-to-site transfer routed through the client's RAM instead of FXP; files never touch local disk.
_Avoid_: memory cache, RAM transfer, memxfer
