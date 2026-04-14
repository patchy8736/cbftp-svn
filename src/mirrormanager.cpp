#include "mirrormanager.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <regex>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "core/tickpoke.h"
#include "core/types.h"
#include "crypto.h"
#include "datafilehandler.h"
#include "engine.h"
#include "eventlog.h"
#include "file.h"
#include "filelist.h"
#include "filelistdata.h"
#include "filesystem.h"
#include "globalcontext.h"
#include "path.h"
#include "race.h"
#include "section.h"
#include "sectionmanager.h"
#include "site.h"
#include "skiplist.h"
#include "sitelogic.h"
#include "sitelogicmanager.h"
#include "sitemanager.h"
#include "siterace.h"

namespace {

#define MIRRORMANAGER_TICK_INTERVAL_MS 1000
#define MIRROR_MIN_POLL_INTERVAL 5
#define MIRROR_MAX_POLL_INTERVAL 3600
#define MIRROR_DEFAULT_RPU_WEBHOOK_URL "http://rpu:8090/api/monitor"
#define MIRROR_COMPLETE_MARKER_REGEX "^\\[[^\\]]*\\][^%]*\\bCOMPLETE\\b[^%]*\\[[^\\]]*\\]$"

std::string listToData(const std::list<std::string>& list) {
  return util::join(list, ",");
}

std::list<std::string> dataToList(const std::string& value) {
  std::vector<std::string> split = util::splitVec(value, ",");
  std::list<std::string> out;
  for (const std::string& part : split) {
    std::string trimmed = util::trim(part);
    if (!trimmed.empty()) {
      out.push_back(trimmed);
    }
  }
  return out;
}

bool isSkiplistDeniedError(const std::string& error) {
  std::string lowered = util::toLower(error);
  return lowered.find("skiplist match") != std::string::npos;
}

} // namespace

MirrorSeenRelease::MirrorSeenRelease() :
  firstseenepoch(0),
  lastseenepoch(0),
  triggered(false),
  donefilecreated(false),
  webhooksent(false),
  localreconciled(false)
{
}

MirrorJob::MirrorJob() :
  id(-1),
  enabled(true),
  profile(MirrorProfile::DISTRIBUTE),
  pollintervalseconds(15),
  releasenamepattern("*"),
  minreleaseageseconds(0),
  createdonefile(false),
  sendrpuwebhook(false),
  rpuwebhookurl(MIRROR_DEFAULT_RPU_WEBHOOK_URL),
  seeded(false),
  createdepoch(0),
  updatedepoch(0),
  lastscanepoch(0),
  scanning(false),
  scanpending(0),
  scanseen(0),
  scantriggered(0),
  scanskipped(0)
{
}

MirrorScanResult::MirrorScanResult() :
  seen(0),
  triggered(0),
  skipped(0)
{
}

MirrorManager::MirrorManager() : nextid(1), initialized(false), runtimeenabled(false) {
}

MirrorManager::~MirrorManager() {
  if (initialized) {
    global->getTickPoke()->stopPoke(this, 0);
    global->getSettingsLoaderSaver()->removeSettingsAdder(this);
  }
}

void MirrorManager::init() {
  if (initialized) {
    return;
  }
  initialized = true;
  global->getSettingsLoaderSaver()->addSettingsAdder(this);
  global->getTickPoke()->startPoke(this, "MirrorManager", MIRRORMANAGER_TICK_INTERVAL_MS, 0);
}

std::string MirrorManager::profileToString(MirrorProfile profile) {
  switch (profile) {
    case MirrorProfile::DISTRIBUTE:
      return "DISTRIBUTE";
    case MirrorProfile::RACE:
      return "RACE";
  }
  return "DISTRIBUTE";
}

util::Result MirrorManager::stringToProfile(const std::string& profile, MirrorProfile* out) {
  if (profile == "DISTRIBUTE") {
    *out = MirrorProfile::DISTRIBUTE;
    return util::Result(true);
  }
  if (profile == "RACE") {
    *out = MirrorProfile::RACE;
    return util::Result(true);
  }
  return util::Result(false, "Unknown mirror profile: " + profile);
}

util::Result MirrorManager::addJob(const MirrorJob& inputjob, int* id) {
  MirrorJob job = inputjob;
  job.name = normalizeAndValidateName(job.name);
  job.monitorsite = util::trim(job.monitorsite);
  job.targetsite = util::trim(job.targetsite);
  job.rpuwebhookurl = util::trim(job.rpuwebhookurl);
  if (job.rpuwebhookurl.empty()) {
    job.rpuwebhookurl = MIRROR_DEFAULT_RPU_WEBHOOK_URL;
  }
  job.spreadsites = dedupeList(job.spreadsites);
  job.spreadsites.remove(job.monitorsite);
  job.spreadsites.remove(job.targetsite);
  util::Result validation = validate(job);
  if (!validation.success) {
    return validation;
  }
  job.id = nextid++;
  job.sections = dedupeList(job.sections);
  unsigned long long int now = util::getEpochNow();
  job.createdepoch = now;
  job.updatedepoch = now;
  jobs[job.id] = job;
  if (id) {
    *id = job.id;
  }
  return util::Result(true);
}

util::Result MirrorManager::replaceJob(int id, const MirrorJob& inputjob) {
  std::map<int, MirrorJob>::iterator it = jobs.find(id);
  if (it == jobs.end()) {
    return util::Result(false, "Mirror job not found");
  }
  MirrorJob updated = inputjob;
  updated.name = normalizeAndValidateName(updated.name);
  updated.monitorsite = util::trim(updated.monitorsite);
  updated.targetsite = util::trim(updated.targetsite);
  updated.rpuwebhookurl = util::trim(updated.rpuwebhookurl);
  if (updated.rpuwebhookurl.empty()) {
    updated.rpuwebhookurl = MIRROR_DEFAULT_RPU_WEBHOOK_URL;
  }
  updated.spreadsites = dedupeList(updated.spreadsites);
  updated.spreadsites.remove(updated.monitorsite);
  updated.spreadsites.remove(updated.targetsite);
  util::Result validation = validate(updated, id);
  if (!validation.success) {
    return validation;
  }
  MirrorJob old = it->second;
  updated.id = id;
  updated.sections = dedupeList(updated.sections);
  updated.createdepoch = old.createdepoch;
  updated.updatedepoch = util::getEpochNow();
  updated.lastscanepoch = old.lastscanepoch;
  updated.lasterror = old.lasterror;
  updated.seeded = old.seeded;
  updated.scanning = old.scanning;
  updated.scanpending = old.scanpending;
  updated.scanseen = old.scanseen;
  updated.scantriggered = old.scantriggered;
  updated.scanskipped = old.scanskipped;
  updated.seenreleases = old.seenreleases;
  jobs[id] = updated;
  return util::Result(true);
}

bool MirrorManager::removeJob(int id) {
  for (std::list<OngoingScanRequest>::const_iterator it = ongoingrequests.begin(); it != ongoingrequests.end(); ++it) {
    if (it->jobid == id) {
      return false;
    }
  }
  return jobs.erase(id) > 0;
}

MirrorJob* MirrorManager::getJob(int id) {
  std::map<int, MirrorJob>::iterator it = jobs.find(id);
  if (it == jobs.end()) {
    return nullptr;
  }
  return &it->second;
}

const MirrorJob* MirrorManager::getJob(int id) const {
  std::map<int, MirrorJob>::const_iterator it = jobs.find(id);
  if (it == jobs.end()) {
    return nullptr;
  }
  return &it->second;
}

const std::map<int, MirrorJob>& MirrorManager::getJobs() const {
  return jobs;
}

bool MirrorManager::getRuntimeEnabled() const {
  return runtimeenabled;
}

void MirrorManager::setRuntimeEnabled(bool enabled) {
  runtimeenabled = enabled;
  global->getEventLog()->log("MirrorManager", std::string("Mirror runtime ") + (runtimeenabled ? "enabled" : "disabled"));
}

bool MirrorManager::toggleRuntimeEnabled() {
  runtimeenabled = !runtimeenabled;
  setRuntimeEnabled(runtimeenabled);
  return runtimeenabled;
}

bool MirrorManager::resetSeenRelease(int id, const std::string& section, const std::string& release) {
  MirrorJob* job = getJob(id);
  if (!job) {
    return false;
  }
  std::unordered_map<std::string, std::unordered_map<std::string, MirrorSeenRelease> >::iterator sit = job->seenreleases.find(section);
  if (sit == job->seenreleases.end()) {
    return false;
  }
  std::unordered_map<std::string, MirrorSeenRelease>::iterator rit = sit->second.find(release);
  if (rit == sit->second.end()) {
    return false;
  }
  sit->second.erase(rit);
  if (sit->second.empty()) {
    job->seenreleases.erase(sit);
  }
  reconcilestates.erase(reconcileKey(id, section, release));
  job->updatedepoch = util::getEpochNow();
  global->getEventLog()->log("MirrorManager", "Reset seen release for mirror job " + std::to_string(id) + ": " + section + "/" + release);
  return true;
}

bool MirrorManager::setEnabled(int id, bool enabled) {
  MirrorJob* job = getJob(id);
  if (!job) {
    return false;
  }
  job->enabled = enabled;
  job->updatedepoch = util::getEpochNow();
  return true;
}

MirrorScanResult MirrorManager::scanNow(int id, bool force) {
  MirrorScanResult out;
  MirrorJob* job = getJob(id);
  if (!job) {
    out.result = util::Result(false, "Mirror job not found");
    return out;
  }
  if (job->scanning) {
    out.result = util::Result(false, "Mirror job is already scanning");
    out.seen = job->scanseen;
    out.triggered = job->scantriggered;
    out.skipped = job->scanskipped;
    return out;
  }
  util::Result start = startScan(*job, true, force);
  out.result = start;
  out.seen = job->scanseen;
  out.triggered = job->scantriggered;
  out.skipped = job->scanskipped;
  return out;
}

void MirrorManager::tick(int) {
  if (!runtimeenabled) {
    return;
  }
  unsigned long long int now = util::getEpochNow();
  for (std::map<int, MirrorJob>::iterator it = jobs.begin(); it != jobs.end(); ++it) {
    MirrorJob& job = it->second;
    if (!job.enabled || job.scanning) {
      continue;
    }
    if (job.pollintervalseconds < MIRROR_MIN_POLL_INTERVAL) {
      job.pollintervalseconds = MIRROR_MIN_POLL_INTERVAL;
    }
    if (!job.lastscanepoch || now >= job.lastscanepoch + static_cast<unsigned long long int>(job.pollintervalseconds)) {
      startScan(job, false, false);
    }
    processPostActions(job);
  }
}

void MirrorManager::requestReady(void* service, int requestid) {
  std::list<OngoingScanRequest>::iterator reqit = ongoingrequests.end();
  for (std::list<OngoingScanRequest>::iterator it = ongoingrequests.begin(); it != ongoingrequests.end(); ++it) {
    if (it->sl == service && it->requestid == requestid) {
      reqit = it;
      break;
    }
  }
  if (reqit == ongoingrequests.end()) {
    return;
  }
  std::map<int, MirrorJob>::iterator jobit = jobs.find(reqit->jobid);
  if (jobit == jobs.end()) {
    reqit->sl->finishRequest(reqit->requestid);
    ongoingrequests.erase(reqit);
    return;
  }
  bool isscan = reqit->type == OngoingScanRequest::Type::SCAN;
  if (reqit->type == OngoingScanRequest::Type::SCAN) {
    processRequestResult(jobit->second, *reqit);
  }
  else if (reqit->type == OngoingScanRequest::Type::COMPLETE_CHECK_ROOT) {
    processCompleteCheckRootResult(jobit->second, *reqit);
  }
  else if (reqit->type == OngoingScanRequest::Type::COMPLETE_CHECK_SUBS) {
    processCompleteCheckSubsResult(jobit->second, *reqit);
  }
  else if (reqit->type == OngoingScanRequest::Type::RECONCILE_LIST) {
    processReconcileListResult(jobit->second, *reqit);
  }
  reqit->sl->finishRequest(reqit->requestid);
  ongoingrequests.erase(reqit);
  if (isscan) {
    if (jobit->second.scanpending > 0) {
      jobit->second.scanpending--;
    }
    if (jobit->second.scanpending == 0) {
      completeScan(jobit->second);
    }
  }
}

util::Result MirrorManager::validate(const MirrorJob& in, int ignoreid) const {
  MirrorJob job = in;
  std::string name = normalizeAndValidateName(job.name);
  if (name.empty()) {
    return util::Result(false, "Mirror job name is required");
  }
  for (std::map<int, MirrorJob>::const_iterator it = jobs.begin(); it != jobs.end(); ++it) {
    if (it->first != ignoreid && it->second.name == name) {
      return util::Result(false, "Mirror job name already exists: " + name);
    }
  }
  std::shared_ptr<Site> monitor = global->getSiteManager()->getSite(job.monitorsite);
  if (!monitor) {
    return util::Result(false, "Monitor site not found: " + job.monitorsite);
  }
  std::shared_ptr<Site> target = global->getSiteManager()->getSite(job.targetsite);
  if (!target) {
    return util::Result(false, "Target site not found: " + job.targetsite);
  }
  if (job.monitorsite == job.targetsite) {
    return util::Result(false, "Monitor site and target site must differ");
  }
  if (job.sections.empty()) {
    return util::Result(false, "At least one section is required");
  }
  for (const std::string& section : job.sections) {
    if (!global->getSectionManager()->getSection(section)) {
      return util::Result(false, "Undefined section: " + section);
    }
    if (!monitor->hasSection(section)) {
      return util::Result(false, "Section " + section + " is not defined on monitor site " + job.monitorsite);
    }
    if (!target->hasSection(section)) {
      return util::Result(false, "Section " + section + " is not defined on target site " + job.targetsite);
    }
  }
  std::set<std::string> spreadset;
  for (const std::string& site : job.spreadsites) {
    if (site.empty()) {
      continue;
    }
    std::shared_ptr<Site> spread = global->getSiteManager()->getSite(site);
    if (!spread) {
      return util::Result(false, "Spread site not found: " + site);
    }
    if (site == job.targetsite) {
      return util::Result(false, "Spread sites must not include target site");
    }
    spreadset.insert(site);
    for (const std::string& section : job.sections) {
      if (!spread->hasSection(section)) {
        return util::Result(false, "Section " + section + " is not defined on spread site " + site);
      }
    }
  }
  if (job.pollintervalseconds < MIRROR_MIN_POLL_INTERVAL || job.pollintervalseconds > MIRROR_MAX_POLL_INTERVAL) {
    return util::Result(false, "poll_interval_seconds must be between " + std::to_string(MIRROR_MIN_POLL_INTERVAL) + " and " + std::to_string(MIRROR_MAX_POLL_INTERVAL));
  }
  if (job.minreleaseageseconds < 0) {
    return util::Result(false, "min_release_age_seconds must be >= 0");
  }
  if (job.releasenamepattern.empty()) {
    return util::Result(false, "release_name_pattern must not be empty");
  }
  if (job.sendrpuwebhook) {
    if (job.rpuwebhookurl.empty()) {
      return util::Result(false, "rpu_webhook_url must not be empty when webhook is enabled");
    }
    for (const std::string& section : job.sections) {
      std::unordered_map<std::string, std::string>::const_iterator it = job.sectionlocalpaths.find(section);
      if (it == job.sectionlocalpaths.end() || util::trim(it->second).empty()) {
        return util::Result(false, "Missing section local path mapping for section: " + section);
      }
    }
  }
  return util::Result(true);
}

util::Result MirrorManager::startScan(MirrorJob& job, bool manual, bool force) {
  util::Result valid = validate(job, job.id);
  if (!valid.success) {
    job.lasterror = valid.error;
    return valid;
  }
  if (job.scanning) {
    return util::Result(false, "Mirror job is already scanning");
  }
  if (!runtimeenabled && !force) {
    return util::Result(false, "Mirror runtime is disabled");
  }
  if (!job.enabled && !force) {
    return util::Result(false, "Mirror job is disabled");
  }
  std::shared_ptr<SiteLogic> sl = global->getSiteLogicManager()->getSiteLogic(job.monitorsite);
  if (!sl) {
    job.lasterror = "Monitor site logic not found: " + job.monitorsite;
    return util::Result(false, job.lasterror);
  }
  job.scanning = true;
  job.scanpending = 0;
  job.scanseen = 0;
  job.scantriggered = 0;
  job.scanskipped = 0;
  job.lasterror.clear();
  for (const std::string& section : job.sections) {
    int reqid = sl->requestFileList(this, sl->getSite()->getSectionPath(section));
    ongoingrequests.push_back(OngoingScanRequest(OngoingScanRequest::Type::SCAN, job.id, section, "", "", "", sl.get(), reqid, manual));
    job.scanpending++;
  }
  if (!job.scanpending) {
    job.scanning = false;
    job.lasterror = "No sections configured";
    return util::Result(false, job.lasterror);
  }
  return util::Result(true);
}

void MirrorManager::processRequestResult(MirrorJob& job, const OngoingScanRequest& req) {
  bool status = req.sl->requestStatus(req.requestid);
  if (!status) {
    job.scanskipped++;
    if (job.lasterror.empty()) {
      job.lasterror = "Failed to list section " + req.section + " on " + job.monitorsite;
    }
    return;
  }
  FileListData* data = req.sl->getFileListData(req.requestid);
  if (!data) {
    job.scanskipped++;
    if (job.lasterror.empty()) {
      job.lasterror = "Missing filelist data for section " + req.section;
    }
    return;
  }
  std::shared_ptr<FileList> fl = data->getFileList();
  if (!fl) {
    job.scanskipped++;
    return;
  }
  bool seedonly = !job.seeded;
  unsigned long long int now = util::getEpochNow();
  for (std::list<File*>::const_iterator it = fl->begin(); it != fl->end(); ++it) {
    File* f = *it;
    if (!f->isDirectory()) {
      continue;
    }
    const std::string release = f->getName();
    if (release == "." || release == "..") {
      continue;
    }
    if (util::wildcmp(job.releasenamepattern.c_str(), release.c_str()) == 0) {
      continue;
    }
    job.scanseen++;
    MirrorSeenRelease& seen = job.seenreleases[req.section][release];
    if (!seen.firstseenepoch) {
      seen.firstseenepoch = now;
    }
    seen.lastseenepoch = now;
    if (seen.triggered || seedonly) {
      if (seedonly) {
        seen.triggered = true;
      }
      job.scanskipped++;
      continue;
    }
    if (job.minreleaseageseconds > 0 &&
        now < seen.firstseenepoch + static_cast<unsigned long long int>(job.minreleaseageseconds))
    {
      job.scanskipped++;
      continue;
    }
    if (spreadJobExists(req.section, release)) {
      seen.triggered = true;
      job.scanskipped++;
      continue;
    }
    if (isReleaseSkiplistDenied(job, req.section, release)) {
      seen.triggered = true;
      job.scanskipped++;
      global->getEventLog()->log("MirrorManager", "Release skipped by skiplist: " + req.section + "/" + release);
      continue;
    }
    util::Result started = triggerSpreadJob(job, req.section, release);
    if (started.success) {
      seen.triggered = true;
      job.scantriggered++;
    }
    else {
      if (isSkiplistDeniedError(started.error)) {
        seen.triggered = true;
      }
      if (job.lasterror.empty()) {
        job.lasterror = started.error;
      }
      job.scanskipped++;
    }
  }
}

void MirrorManager::processCompleteCheckRootResult(MirrorJob& job, const OngoingScanRequest& req) {
  if (!req.sl->requestStatus(req.requestid)) {
    return;
  }
  FileListData* data = req.sl->getFileListData(req.requestid);
  if (!data) {
    return;
  }
  std::shared_ptr<FileList> fl = data->getFileList();
  if (!fl) {
    return;
  }
  bool hasrootcomplete = false;
  bool hassubs = false;
  for (std::list<File*>::const_iterator it = fl->begin(); it != fl->end(); ++it) {
    File* f = *it;
    const std::string& name = f->getName();
    if (name == "." || name == "..") {
      continue;
    }
    if (isCompleteMarkerName(name)) {
      hasrootcomplete = true;
    }
    if (f->isDirectory() && isSubsDirName(name)) {
      hassubs = true;
    }
  }
  if (!hasrootcomplete) {
    return;
  }
  if (hassubs) {
    Path p = req.sl->getSite()->getSectionPath(req.section) / req.release / "subs";
    int requestid = req.sl->requestFileList(this, p);
    ongoingrequests.push_back(OngoingScanRequest(OngoingScanRequest::Type::COMPLETE_CHECK_SUBS,
      job.id, req.section, req.release, req.sourcesite, "subs", req.sl, requestid, false));
    return;
  }
  MirrorSeenRelease& seen = job.seenreleases[req.section][req.release];
  seen.completesourcesite = req.sourcesite;
  global->getEventLog()->log("MirrorManager", "Source complete detected for " + req.section + "/" + req.release + " on " + req.sourcesite);
}

void MirrorManager::processCompleteCheckSubsResult(MirrorJob& job, const OngoingScanRequest& req) {
  if (!req.sl->requestStatus(req.requestid)) {
    return;
  }
  FileListData* data = req.sl->getFileListData(req.requestid);
  if (!data) {
    return;
  }
  std::shared_ptr<FileList> fl = data->getFileList();
  if (!fl) {
    return;
  }
  bool hassubscomplete = false;
  for (std::list<File*>::const_iterator it = fl->begin(); it != fl->end(); ++it) {
    File* f = *it;
    const std::string& name = f->getName();
    if (name == "." || name == "..") {
      continue;
    }
    if (isCompleteMarkerName(name)) {
      hassubscomplete = true;
      break;
    }
  }
  if (!hassubscomplete) {
    return;
  }
  MirrorSeenRelease& seen = job.seenreleases[req.section][req.release];
  seen.completesourcesite = req.sourcesite;
  global->getEventLog()->log("MirrorManager", "Source complete detected (subs) for " + req.section + "/" + req.release + " on " + req.sourcesite);
}

void MirrorManager::processReconcileListResult(MirrorJob& job, const OngoingScanRequest& req) {
  ReconcileState* state = getReconcileState(job.id, req.section, req.release);
  if (!state || !state->active || state->sourcesite != req.sourcesite) {
    return;
  }
  if (!req.sl->requestStatus(req.requestid)) {
    state->active = false;
    if (job.lasterror.empty()) {
      job.lasterror = "Failed to list source path during reconcile on " + req.sourcesite;
    }
    return;
  }
  FileListData* data = req.sl->getFileListData(req.requestid);
  if (!data) {
    state->active = false;
    return;
  }
  std::shared_ptr<FileList> fl = data->getFileList();
  if (!fl) {
    state->active = false;
    return;
  }
  for (std::list<File*>::const_iterator it = fl->begin(); it != fl->end(); ++it) {
    File* f = *it;
    const std::string& name = f->getName();
    if (name == "." || name == "..") {
      continue;
    }
    if (shouldSkipRemoteSyncEntry(name)) {
      continue;
    }
    std::string rel = req.subpath.empty() ? name : req.subpath + "/" + name;
    state->remotepaths.insert(rel);
    if (f->isDirectory()) {
      state->pending.push_back(rel);
    }
  }
  if (!state->pending.empty()) {
    std::string nextsub = state->pending.front();
    state->pending.pop_front();
    Path nextpath = req.sl->getSite()->getSectionPath(req.section) / req.release / nextsub;
    int requestid = req.sl->requestFileList(this, nextpath);
    ongoingrequests.push_back(OngoingScanRequest(OngoingScanRequest::Type::RECONCILE_LIST,
      job.id, req.section, req.release, req.sourcesite, nextsub, req.sl, requestid, false));
    return;
  }
  int deletedcount = 0;
  util::Result syncres = syncLocalToRemote(job, req.section, req.release, state->remotepaths, &deletedcount);
  state->active = false;
  if (!syncres.success) {
    if (job.lasterror.empty()) {
      job.lasterror = syncres.error;
    }
    return;
  }
  MirrorSeenRelease& seen = job.seenreleases[req.section][req.release];
  seen.localreconciled = true;
  global->getEventLog()->log("MirrorManager", "Reconciled local release " + req.section + "/" + req.release + " deleted=" + std::to_string(deletedcount));
}

void MirrorManager::processPostActions(MirrorJob& job) {
  if (!runtimeenabled) {
    return;
  }
  if (!job.createdonefile && !job.sendrpuwebhook) {
    return;
  }
  for (std::unordered_map<std::string, std::unordered_map<std::string, MirrorSeenRelease> >::iterator sit = job.seenreleases.begin(); sit != job.seenreleases.end(); ++sit) {
    const std::string& section = sit->first;
    for (std::unordered_map<std::string, MirrorSeenRelease>::iterator rit = sit->second.begin(); rit != sit->second.end(); ++rit) {
      const std::string& release = rit->first;
      MirrorSeenRelease& seen = rit->second;
      if (!seen.triggered) {
        continue;
      }
      std::shared_ptr<Race> race = getRace(section, release);
      if (!race || !race->isDone() || !isTargetComplete(race, job.targetsite)) {
        if (race && race->isDone() && !race->getSiteRace(job.targetsite)) {
          if (job.lasterror.empty()) {
            job.lasterror = "Orphaned race for " + section + "/" + release + " (target site not in race)";
          }
          seen.donefilecreated = true;
          seen.webhooksent = true;
        }
        continue;
      }
      if (seen.completesourcesite.empty()) {
        requestSourceCompleteChecks(job, section, release);
        continue;
      }
      if (!ensureLocalReconciled(job, section, release, seen.completesourcesite)) {
        continue;
      }
      if (job.createdonefile && !seen.donefilecreated) {
        createDoneFile(job, section, release);
        continue;
      }
      if (job.sendrpuwebhook && !seen.webhooksent) {
        if (!isWebhookActive(job.id, section, release)) {
          sendWebhook(job, section, release);
        }
      }
    }
  }
}

bool MirrorManager::requestSourceCompleteChecks(MirrorJob& job, const std::string& section, const std::string& release) {
  std::list<std::string> sourcesites;
  sourcesites.push_back(job.monitorsite);
  for (const std::string& sitename : job.spreadsites) {
    sourcesites.push_back(sitename);
  }
  sourcesites = dedupeList(sourcesites);
  bool requested = false;
  for (const std::string& sitename : sourcesites) {
    if (isCompleteCheckActive(job.id, section, release, sitename)) {
      continue;
    }
    std::shared_ptr<SiteLogic> sl = global->getSiteLogicManager()->getSiteLogic(sitename);
    if (!sl || !sl->getSite()->hasSection(section)) {
      continue;
    }
    Path releasepath = sl->getSite()->getSectionPath(section) / release;
    int requestid = sl->requestFileList(this, releasepath);
    ongoingrequests.push_back(OngoingScanRequest(OngoingScanRequest::Type::COMPLETE_CHECK_ROOT,
      job.id, section, release, sitename, "", sl.get(), requestid, false));
    requested = true;
  }
  return requested;
}

bool MirrorManager::ensureLocalReconciled(MirrorJob& job, const std::string& section, const std::string& release, const std::string& sourcesite) {
  MirrorSeenRelease& seen = job.seenreleases[section][release];
  if (seen.localreconciled) {
    return true;
  }
  ReconcileState* state = getReconcileState(job.id, section, release);
  if (state && state->active) {
    return false;
  }
  std::shared_ptr<SiteLogic> sl = global->getSiteLogicManager()->getSiteLogic(sourcesite);
  if (!sl || !sl->getSite()->hasSection(section)) {
    if (job.lasterror.empty()) {
      job.lasterror = "Source site logic not found for reconcile: " + sourcesite;
    }
    return false;
  }
  std::string key = reconcileKey(job.id, section, release);
  ReconcileState& newstate = reconcilestates[key];
  newstate.section = section;
  newstate.release = release;
  newstate.sourcesite = sourcesite;
  newstate.pending.clear();
  newstate.remotepaths.clear();
  newstate.active = true;
  int requestid = sl->requestFileList(this, sl->getSite()->getSectionPath(section) / release);
  ongoingrequests.push_back(OngoingScanRequest(OngoingScanRequest::Type::RECONCILE_LIST,
    job.id, section, release, sourcesite, "", sl.get(), requestid, false));
  return false;
}

bool MirrorManager::createDoneFile(MirrorJob& job, const std::string& section, const std::string& release) {
  std::unordered_map<std::string, std::string>::const_iterator mapit = job.sectionlocalpaths.find(section);
  if (mapit == job.sectionlocalpaths.end()) {
    if (job.lasterror.empty()) {
      job.lasterror = "Missing section local path mapping for section: " + section;
    }
    return false;
  }
  std::string localsectionpath = util::trim(mapit->second);
  if (localsectionpath.empty()) {
    if (job.lasterror.empty()) {
      job.lasterror = "Empty section local path mapping for section: " + section;
    }
    return false;
  }
  Path releasedir = Path(localsectionpath) / release;
  if (!FileSystem::directoryExists(releasedir)) {
    if (job.lasterror.empty()) {
      job.lasterror = "Release directory not found for done.file creation: " + releasedir.toString();
    }
    return false;
  }
  Path donefilepath = releasedir / "done.file";
  std::unordered_map<std::string, std::unordered_map<std::string, MirrorSeenRelease> >::iterator sit = job.seenreleases.find(section);
  if (sit == job.seenreleases.end()) {
    return false;
  }
  std::unordered_map<std::string, MirrorSeenRelease>::iterator rit = sit->second.find(release);
  if (rit == sit->second.end()) {
    return false;
  }
  if (FileSystem::fileExists(donefilepath)) {
    rit->second.donefilecreated = true;
    return true;
  }
  Core::BinaryData empty;
  util::Result write = FileSystem::writeFile(donefilepath, empty);
  if (!write.success) {
    if (job.lasterror.empty()) {
      job.lasterror = "Failed to create local done.file at " + donefilepath.toString() + ": " + write.error;
    }
    return false;
  }
  rit->second.donefilecreated = true;
  global->getEventLog()->log("MirrorManager", "Created local done.file for " + section + "/" + release + " at " + donefilepath.toString());
  return true;
}

bool MirrorManager::sendWebhook(MirrorJob& job, const std::string& section, const std::string& release) {
  std::unordered_map<std::string, std::string>::const_iterator mapit = job.sectionlocalpaths.find(section);
  if (mapit == job.sectionlocalpaths.end()) {
    if (job.lasterror.empty()) {
      job.lasterror = "Missing section local path mapping for section: " + section;
    }
    return false;
  }
  std::string categorypath = util::trim(mapit->second);
  if (categorypath.empty()) {
    if (job.lasterror.empty()) {
      job.lasterror = "Empty section local path mapping for section: " + section;
    }
    return false;
  }
  std::string fullpath = categorypath;
  if (!fullpath.empty() && fullpath.back() == '/') {
    fullpath.pop_back();
  }
  fullpath += "/" + release;
  std::string payload = std::string("{\"path\":\"") + escapeJson(fullpath) +
      "\",\"category_path\":\"" + escapeJson(categorypath) + "\"}";
  std::vector<std::string> args = {
      "-sS",
      "-o", "/dev/null",
      "-w", "%{http_code}",
      "-X", "POST",
      job.rpuwebhookurl,
      "-H", "Content-Type: application/json",
      "-d", payload
  };
  std::shared_ptr<SubProcess> subprocess = global->getSubProcessManager()->runProcess(this, Path("curl"), args);
  if (!subprocess) {
    if (job.lasterror.empty()) {
      job.lasterror = "Failed to start webhook subprocess";
    }
    return false;
  }
  ongoingwebhooks.push_back(OngoingWebhook(subprocess->pid, job.id, section, release));
  return true;
}

std::shared_ptr<Race> MirrorManager::getRace(const std::string& section, const std::string& release) const {
  for (std::list<std::shared_ptr<Race> >::const_iterator it = global->getEngine()->getRacesBegin(); it != global->getEngine()->getRacesEnd(); ++it) {
    if ((*it)->getSection() == section && (*it)->getName() == release) {
      return *it;
    }
  }
  return std::shared_ptr<Race>();
}

bool MirrorManager::isTargetComplete(const std::shared_ptr<Race>& race, const std::string& targetsite) const {
  if (!race) {
    return false;
  }
  std::shared_ptr<SiteRace> sr = race->getSiteRace(targetsite);
  return !!sr && sr->isDone();
}

bool MirrorManager::isCompleteMarkerName(const std::string& name) {
  static const std::regex completeregex(MIRROR_COMPLETE_MARKER_REGEX, std::regex_constants::ECMAScript | std::regex_constants::icase);
  return std::regex_match(name, completeregex);
}

bool MirrorManager::isSubsDirName(const std::string& name) {
  return util::toLower(name) == "subs";
}

bool MirrorManager::shouldSkipRemoteSyncEntry(const std::string& name) {
  return isCompleteMarkerName(name) || name.find("% Complete") != std::string::npos;
}

bool MirrorManager::shouldSkipLocalSyncEntry(const std::string& relpath, bool isdir) {
  if (relpath.empty() || relpath == ".") {
    return true;
  }
  if (!isdir) {
    Path p(relpath);
    if (p.baseName() == "done.file") {
      return true;
    }
  }
  return false;
}

util::Result MirrorManager::collectLocalEntries(const Path& root, const std::string& relpath,
  std::vector<std::pair<std::string, bool> >* entries)
{
  Path curr = relpath.empty() ? root : (root / relpath);
  DIR* dir = opendir(curr.toString().c_str());
  if (!dir) {
    return util::Result(false, "Failed opening local directory " + curr.toString() + ": " + std::string(strerror(errno)));
  }
  struct dirent* ent;
  while ((ent = readdir(dir)) != nullptr) {
    std::string name = ent->d_name;
    if (name == "." || name == "..") {
      continue;
    }
    std::string childrel = relpath.empty() ? name : relpath + "/" + name;
    Path childfull = root / childrel;
    struct stat st;
    if (lstat(childfull.toString().c_str(), &st) != 0) {
      closedir(dir);
      return util::Result(false, "Failed stat local path " + childfull.toString() + ": " + std::string(strerror(errno)));
    }
    bool isdir = S_ISDIR(st.st_mode);
    entries->push_back(std::make_pair(childrel, isdir));
    if (isdir) {
      util::Result nested = collectLocalEntries(root, childrel, entries);
      if (!nested.success) {
        closedir(dir);
        return nested;
      }
    }
  }
  closedir(dir);
  return util::Result(true);
}

bool MirrorManager::hasLocalPayload(const Path& root, bool* out) {
  if (!FileSystem::directoryExists(root)) {
    *out = false;
    return true;
  }
  std::vector<std::pair<std::string, bool> > entries;
  util::Result res = collectLocalEntries(root, "", &entries);
  if (!res.success) {
    return false;
  }
  for (const std::pair<std::string, bool>& entry : entries) {
    if (!entry.second && !shouldSkipLocalSyncEntry(entry.first, false)) {
      *out = true;
      return true;
    }
  }
  *out = false;
  return true;
}

util::Result MirrorManager::removeLocalPath(const Path& root, const std::string& relpath, bool isdir) {
  Path full = root / relpath;
  int ret = isdir ? rmdir(full.toString().c_str()) : unlink(full.toString().c_str());
  if (ret != 0) {
    return util::Result(false, "Failed deleting local path " + full.toString() + ": " + std::string(strerror(errno)));
  }
  return util::Result(true);
}

util::Result MirrorManager::syncLocalToRemote(MirrorJob& job, const std::string& section, const std::string& release,
  const std::unordered_set<std::string>& remotepaths, int* deletedcount)
{
  std::unordered_map<std::string, std::string>::const_iterator mapit = job.sectionlocalpaths.find(section);
  if (mapit == job.sectionlocalpaths.end()) {
    return util::Result(false, "Missing section local path mapping for section: " + section);
  }
  Path localroot = Path(util::trim(mapit->second)) / release;
  if (!FileSystem::directoryExists(localroot)) {
    return util::Result(false, "Release directory not found for reconcile: " + localroot.toString());
  }
  bool haspayload = false;
  if (!hasLocalPayload(localroot, &haspayload)) {
    return util::Result(false, "Failed checking local payload before reconcile");
  }
  if (haspayload && remotepaths.empty()) {
    return util::Result(false, "Refusing reconcile delete: source listing is empty while local payload exists");
  }
  std::vector<std::pair<std::string, bool> > localentries;
  util::Result gather = collectLocalEntries(localroot, "", &localentries);
  if (!gather.success) {
    return gather;
  }
  std::sort(localentries.begin(), localentries.end(), [](const std::pair<std::string, bool>& a, const std::pair<std::string, bool>& b) {
    if (a.first.length() != b.first.length()) {
      return a.first.length() > b.first.length();
    }
    return a.first > b.first;
  });
  int deleted = 0;
  for (const std::pair<std::string, bool>& entry : localentries) {
    if (shouldSkipLocalSyncEntry(entry.first, entry.second)) {
      continue;
    }
    if (remotepaths.find(entry.first) == remotepaths.end()) {
      util::Result delres = removeLocalPath(localroot, entry.first, entry.second);
      if (!delres.success) {
        return delres;
      }
      ++deleted;
    }
  }
  if (deletedcount) {
    *deletedcount = deleted;
  }
  return util::Result(true);
}

std::string MirrorManager::reconcileKey(int jobid, const std::string& section, const std::string& release) {
  return std::to_string(jobid) + "|" + section + "|" + release;
}

MirrorManager::ReconcileState* MirrorManager::getReconcileState(int jobid, const std::string& section, const std::string& release) {
  std::string key = reconcileKey(jobid, section, release);
  std::unordered_map<std::string, ReconcileState>::iterator it = reconcilestates.find(key);
  if (it == reconcilestates.end()) {
    return nullptr;
  }
  return &it->second;
}

bool MirrorManager::isCompleteCheckActive(int jobid, const std::string& section, const std::string& release, const std::string& sourcesite) const {
  for (const OngoingScanRequest& req : ongoingrequests) {
    if ((req.type == OngoingScanRequest::Type::COMPLETE_CHECK_ROOT || req.type == OngoingScanRequest::Type::COMPLETE_CHECK_SUBS) &&
        req.jobid == jobid && req.section == section && req.release == release && req.sourcesite == sourcesite)
    {
      return true;
    }
  }
  return false;
}

bool MirrorManager::isWebhookActive(int jobid, const std::string& section, const std::string& release) const {
  for (const OngoingWebhook& req : ongoingwebhooks) {
    if (req.jobid == jobid && req.section == section && req.release == release) {
      return true;
    }
  }
  return false;
}

void MirrorManager::completeScan(MirrorJob& job) {
  job.scanning = false;
  if (!job.seeded) {
    job.seeded = true;
    global->getEventLog()->log("MirrorManager", "Baseline seeded for mirror job: " + job.name);
  }
  job.lastscanepoch = util::getEpochNow();
  job.updatedepoch = job.lastscanepoch;
  std::string logline = "Mirror job scan completed: " + job.name +
      " seen=" + std::to_string(job.scanseen) +
      " triggered=" + std::to_string(job.scantriggered) +
      " skipped=" + std::to_string(job.scanskipped);
  if (!job.lasterror.empty()) {
    logline += " error=" + job.lasterror;
  }
  global->getEventLog()->log("MirrorManager", logline);
}

bool MirrorManager::spreadJobExists(const std::string& section, const std::string& release) const {
  for (std::list<std::shared_ptr<Race> >::const_iterator it = global->getEngine()->getRacesBegin(); it != global->getEngine()->getRacesEnd(); ++it) {
    if ((*it)->getSection() == section && (*it)->getName() == release) {
      return true;
    }
  }
  return false;
}

util::Result MirrorManager::triggerSpreadJob(MirrorJob& job, const std::string& section, const std::string& release) {
  std::list<std::string> sites;
  sites.push_back(job.targetsite);
  sites.push_back(job.monitorsite);
  for (const std::string& spread : job.spreadsites) {
    sites.push_back(spread);
  }
  sites = dedupeList(sites);

  std::list<std::string> dlonlysites;
  dlonlysites.push_back(job.monitorsite);
  for (const std::string& spread : job.spreadsites) {
    dlonlysites.push_back(spread);
  }
  dlonlysites = dedupeList(dlonlysites);
  dlonlysites.remove(job.targetsite);

  JobStartResult result;
  if (job.profile == MirrorProfile::RACE) {
    result = global->getEngine()->newRace(release, section, sites, false, dlonlysites);
  }
  else {
    result = global->getEngine()->newDistribute(release, section, sites, false, dlonlysites);
  }
  if (!result) {
    return util::Result(false, result.error);
  }
  std::shared_ptr<Race> createdrace = global->getEngine()->getRace(result.id);
  if (!createdrace || !createdrace->getSiteRace(job.targetsite)) {
    if (createdrace && !createdrace->isDone()) {
      global->getEngine()->abortRace(createdrace);
    }
    return util::Result(false, "Target site excluded from race (skiplist or site config)");
  }
  global->getEventLog()->log("MirrorManager", "Triggered mirror spread job: " + section + "/" + release + " via " + job.name);
  return util::Result(true);
}

bool MirrorManager::isReleaseSkiplistDenied(const MirrorJob& job, const std::string& section, const std::string& release) const {
  Section* sectionptr = global->getSectionManager()->getSection(section);
  if (!sectionptr) {
    return false;
  }
  SkipListMatch sectionmatch = sectionptr->getSkipList().check(release, true, false);
  if (sectionmatch.action == SKIPLIST_DENY) {
    return true;
  }
  std::shared_ptr<Site> target = global->getSiteManager()->getSite(job.targetsite);
  if (target) {
    std::string fullpath = (target->getSectionPath(section) / release).toString();
    SkipListMatch targetmatch = target->getSkipList().check(fullpath, true, false, &sectionptr->getSkipList());
    if (targetmatch.action == SKIPLIST_DENY) {
      return true;
    }
  }
  return false;
}

std::string MirrorManager::normalizeAndValidateName(const std::string& in) const {
  return util::trim(in);
}

std::string MirrorManager::encodeString(const std::string& text) {
  Core::BinaryData indata(text.begin(), text.end());
  Core::BinaryData outdata;
  Crypto::base64Encode(indata, outdata);
  return std::string(outdata.begin(), outdata.end());
}

std::string MirrorManager::decodeString(const std::string& b64) {
  Core::BinaryData indata(b64.begin(), b64.end());
  Core::BinaryData outdata;
  Crypto::base64Decode(indata, outdata);
  return std::string(outdata.begin(), outdata.end());
}

std::list<std::string> MirrorManager::dedupeList(const std::list<std::string>& in) {
  std::set<std::string> seen;
  std::list<std::string> out;
  for (const std::string& item : in) {
    std::string trimmed = util::trim(item);
    if (trimmed.empty()) {
      continue;
    }
    if (seen.insert(trimmed).second) {
      out.push_back(trimmed);
    }
  }
  return out;
}

void MirrorManager::processStdOut(int pid, const std::string& text) {
  for (OngoingWebhook& webhook : ongoingwebhooks) {
    if (webhook.pid == pid) {
      webhook.stdoutdata += text;
      return;
    }
  }
}

void MirrorManager::processStdErr(int pid, const std::string& text) {
  for (OngoingWebhook& webhook : ongoingwebhooks) {
    if (webhook.pid == pid) {
      webhook.stderrdata += text;
      return;
    }
  }
}

void MirrorManager::processExited(int pid, int status) {
  std::list<OngoingWebhook>::iterator it = ongoingwebhooks.end();
  for (std::list<OngoingWebhook>::iterator wit = ongoingwebhooks.begin(); wit != ongoingwebhooks.end(); ++wit) {
    if (wit->pid == pid) {
      it = wit;
      break;
    }
  }
  if (it == ongoingwebhooks.end()) {
    return;
  }
  std::map<int, MirrorJob>::iterator jobit = jobs.find(it->jobid);
  if (jobit != jobs.end()) {
    std::unordered_map<std::string, std::unordered_map<std::string, MirrorSeenRelease> >::iterator sit = jobit->second.seenreleases.find(it->section);
    if (sit != jobit->second.seenreleases.end()) {
      std::unordered_map<std::string, MirrorSeenRelease>::iterator rit = sit->second.find(it->release);
      if (rit != sit->second.end()) {
        std::string out = util::trim(it->stdoutdata);
        bool okstatus = out.length() >= 3 && out[0] == '2';
        if (status == 0 && okstatus) {
          rit->second.webhooksent = true;
          global->getEventLog()->log("MirrorManager", "Sent RPU webhook for " + it->section + "/" + it->release);
        }
        else if (jobit->second.lasterror.empty()) {
          jobit->second.lasterror = "RPU webhook failed for " + it->section + "/" + it->release +
              " status=" + std::to_string(status) +
              " http=" + out +
              " stderr=" + util::trim(it->stderrdata);
        }
      }
    }
  }
  ongoingwebhooks.erase(it);
}

std::unordered_map<std::string, std::string> MirrorManager::parseSectionLocalPaths(const std::string& data) {
  std::unordered_map<std::string, std::string> out;
  std::vector<std::string> entries = util::splitVec(data, ",");
  for (const std::string& entry : entries) {
    std::string trimmed = util::trim(entry);
    if (trimmed.empty()) {
      continue;
    }
    size_t eq = trimmed.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string section = util::trim(trimmed.substr(0, eq));
    std::string path = util::trim(trimmed.substr(eq + 1));
    if (!section.empty() && !path.empty()) {
      out[section] = path;
    }
  }
  return out;
}

std::string MirrorManager::sectionLocalPathsToString(const std::unordered_map<std::string, std::string>& data) {
  std::list<std::string> entries;
  for (const std::pair<const std::string, std::string>& item : data) {
    entries.push_back(item.first + "=" + item.second);
  }
  entries.sort();
  return util::join(entries, ",");
}

std::string MirrorManager::escapeJson(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (char ch : text) {
    if (ch == '\\') {
      out += "\\\\";
    }
    else if (ch == '"') {
      out += "\\\"";
    }
    else {
      out += ch;
    }
  }
  return out;
}

void MirrorManager::loadSettings(std::shared_ptr<DataFileHandler> dfh) {
  jobs.clear();
  reconcilestates.clear();
  ongoingrequests.clear();
  ongoingwebhooks.clear();
  nextid = 1;
  runtimeenabled = false;
  std::vector<std::string> lines;
  dfh->getDataFor("MirrorManager", &lines);
  for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
    const std::string& line = *it;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    if (key == "nextid") {
      nextid = std::stoi(value);
      continue;
    }
    size_t tok = key.find('$');
    if (tok == std::string::npos) {
      continue;
    }
    int id = std::stoi(key.substr(0, tok));
    std::string setting = key.substr(tok + 1);
    MirrorJob& job = jobs[id];
    job.id = id;
    if (setting == "name") {
      job.name = value;
    }
    else if (setting == "enabled") {
      job.enabled = value == "true";
    }
    else if (setting == "monitor_site") {
      job.monitorsite = value;
    }
    else if (setting == "target_site") {
      job.targetsite = value;
    }
    else if (setting == "spread_sites") {
      job.spreadsites = dataToList(value);
    }
    else if (setting == "sections") {
      job.sections = dataToList(value);
    }
    else if (setting == "profile") {
      MirrorProfile profile;
      if (stringToProfile(value, &profile).success) {
        job.profile = profile;
      }
    }
    else if (setting == "poll_interval_seconds") {
      job.pollintervalseconds = std::stoi(value);
    }
    else if (setting == "release_name_pattern") {
      job.releasenamepattern = value;
    }
    else if (setting == "min_release_age_seconds") {
      job.minreleaseageseconds = std::stoi(value);
    }
    else if (setting == "create_done_file") {
      job.createdonefile = value == "true";
    }
    else if (setting == "send_rpu_webhook") {
      job.sendrpuwebhook = value == "true";
    }
    else if (setting == "rpu_webhook_url") {
      job.rpuwebhookurl = value;
    }
    else if (setting == "section_local_paths") {
      job.sectionlocalpaths = parseSectionLocalPaths(value);
    }
    else if (setting == "seeded") {
      job.seeded = value == "true";
    }
    else if (setting == "created_epoch") {
      job.createdepoch = std::stoull(value);
    }
    else if (setting == "updated_epoch") {
      job.updatedepoch = std::stoull(value);
    }
    else if (setting == "last_scan_epoch") {
      job.lastscanepoch = std::stoull(value);
    }
    else if (setting == "last_error_b64") {
      job.lasterror = decodeString(value);
    }
  }
  for (std::map<int, MirrorJob>::iterator it = jobs.begin(); it != jobs.end(); ++it) {
    MirrorJob& job = it->second;
    if (job.rpuwebhookurl.empty()) {
      job.rpuwebhookurl = MIRROR_DEFAULT_RPU_WEBHOOK_URL;
    }
    std::unordered_map<std::string, std::string> normalized;
    for (const std::pair<const std::string, std::string>& mapitem : job.sectionlocalpaths) {
      std::string section = util::trim(mapitem.first);
      std::string localpath = util::trim(mapitem.second);
      if (!section.empty() && !localpath.empty()) {
        normalized[section] = localpath;
      }
    }
    job.sectionlocalpaths = normalized;
  }

  lines.clear();
  dfh->getDataFor("MirrorSeen", &lines);
  for (std::vector<std::string>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
    const std::string& line = *it;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    size_t tok = key.find('$');
    if (tok == std::string::npos) {
      continue;
    }
    int id = std::stoi(key.substr(0, tok));
    std::string setting = key.substr(tok + 1);
    if (setting != "entry") {
      continue;
    }
    std::vector<std::string> parts = util::splitVec(value, "$");
    if (parts.size() < 5) {
      continue;
    }
    std::map<int, MirrorJob>::iterator jobit = jobs.find(id);
    if (jobit == jobs.end()) {
      continue;
    }
    std::string section = decodeString(parts[0]);
    std::string release = decodeString(parts[1]);
    MirrorSeenRelease seen;
    seen.firstseenepoch = std::stoull(parts[2]);
    seen.lastseenepoch = std::stoull(parts[3]);
    seen.triggered = parts[4] == "1";
    if (parts.size() >= 6) {
      seen.donefilecreated = parts[5] == "1";
    }
    if (parts.size() >= 7) {
      seen.webhooksent = parts[6] == "1";
    }
    if (parts.size() >= 8) {
      seen.completesourcesite = decodeString(parts[7]);
    }
    if (parts.size() >= 9) {
      seen.localreconciled = parts[8] == "1";
    }
    jobit->second.seenreleases[section][release] = seen;
  }
}

void MirrorManager::saveSettings(std::shared_ptr<DataFileHandler> dfh) {
  dfh->addOutputLine("MirrorManager", "nextid=" + std::to_string(nextid));
  for (std::map<int, MirrorJob>::const_iterator it = jobs.begin(); it != jobs.end(); ++it) {
    const MirrorJob& job = it->second;
    std::string prefix = std::to_string(job.id) + "$";
    dfh->addOutputLine("MirrorManager", prefix + "name=" + job.name);
    dfh->addOutputLine("MirrorManager", prefix + "enabled=" + std::string(job.enabled ? "true" : "false"));
    dfh->addOutputLine("MirrorManager", prefix + "monitor_site=" + job.monitorsite);
    dfh->addOutputLine("MirrorManager", prefix + "target_site=" + job.targetsite);
    dfh->addOutputLine("MirrorManager", prefix + "spread_sites=" + listToData(job.spreadsites));
    dfh->addOutputLine("MirrorManager", prefix + "sections=" + listToData(job.sections));
    dfh->addOutputLine("MirrorManager", prefix + "profile=" + profileToString(job.profile));
    dfh->addOutputLine("MirrorManager", prefix + "poll_interval_seconds=" + std::to_string(job.pollintervalseconds));
    dfh->addOutputLine("MirrorManager", prefix + "release_name_pattern=" + job.releasenamepattern);
    dfh->addOutputLine("MirrorManager", prefix + "min_release_age_seconds=" + std::to_string(job.minreleaseageseconds));
    dfh->addOutputLine("MirrorManager", prefix + "create_done_file=" + std::string(job.createdonefile ? "true" : "false"));
    dfh->addOutputLine("MirrorManager", prefix + "send_rpu_webhook=" + std::string(job.sendrpuwebhook ? "true" : "false"));
    dfh->addOutputLine("MirrorManager", prefix + "rpu_webhook_url=" + job.rpuwebhookurl);
    dfh->addOutputLine("MirrorManager", prefix + "section_local_paths=" + sectionLocalPathsToString(job.sectionlocalpaths));
    dfh->addOutputLine("MirrorManager", prefix + "seeded=" + std::string(job.seeded ? "true" : "false"));
    dfh->addOutputLine("MirrorManager", prefix + "created_epoch=" + std::to_string(job.createdepoch));
    dfh->addOutputLine("MirrorManager", prefix + "updated_epoch=" + std::to_string(job.updatedepoch));
    dfh->addOutputLine("MirrorManager", prefix + "last_scan_epoch=" + std::to_string(job.lastscanepoch));
    dfh->addOutputLine("MirrorManager", prefix + "last_error_b64=" + encodeString(job.lasterror));
    for (std::unordered_map<std::string, std::unordered_map<std::string, MirrorSeenRelease> >::const_iterator sit = job.seenreleases.begin(); sit != job.seenreleases.end(); ++sit) {
      for (std::unordered_map<std::string, MirrorSeenRelease>::const_iterator rit = sit->second.begin(); rit != sit->second.end(); ++rit) {
        const MirrorSeenRelease& seen = rit->second;
        std::string line = encodeString(sit->first) + "$" +
            encodeString(rit->first) + "$" +
            std::to_string(seen.firstseenepoch) + "$" +
            std::to_string(seen.lastseenepoch) + "$" +
            (seen.triggered ? "1" : "0") + "$" +
            (seen.donefilecreated ? "1" : "0") + "$" +
            (seen.webhooksent ? "1" : "0") + "$" +
            encodeString(seen.completesourcesite) + "$" +
            (seen.localreconciled ? "1" : "0");
        dfh->addOutputLine("MirrorSeen", std::to_string(job.id) + "$entry=" + line);
      }
    }
  }
}
