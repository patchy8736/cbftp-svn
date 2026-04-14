#pragma once

#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/eventreceiver.h"
#include "requestcallback.h"
#include "settingsloadersaver.h"
#include "subprocessmanager.h"
#include "util.h"

class DataFileHandler;
class SiteLogic;
class Race;

enum class MirrorProfile {
  DISTRIBUTE,
  RACE
};

struct MirrorSeenRelease {
  MirrorSeenRelease();
  unsigned long long int firstseenepoch;
  unsigned long long int lastseenepoch;
  bool triggered;
  bool donefilecreated;
  bool webhooksent;
  std::string completesourcesite;
  bool localreconciled;
};

struct MirrorJob {
  MirrorJob();
  int id;
  std::string name;
  bool enabled;
  std::string monitorsite;
  std::string targetsite;
  std::list<std::string> spreadsites;
  std::list<std::string> sections;
  MirrorProfile profile;
  int pollintervalseconds;
  std::string releasenamepattern;
  int minreleaseageseconds;
  bool createdonefile;
  bool sendrpuwebhook;
  std::string rpuwebhookurl;
  std::unordered_map<std::string, std::string> sectionlocalpaths;
  bool seeded;
  unsigned long long int createdepoch;
  unsigned long long int updatedepoch;
  unsigned long long int lastscanepoch;
  std::string lasterror;
  bool scanning;
  int scanpending;
  int scanseen;
  int scantriggered;
  int scanskipped;
  std::unordered_map<std::string, std::unordered_map<std::string, MirrorSeenRelease> > seenreleases;
};

struct MirrorScanResult {
  MirrorScanResult();
  util::Result result;
  int seen;
  int triggered;
  int skipped;
  std::list<std::pair<std::string, std::string> > triggeredreleases;
};

class MirrorManager : private Core::EventReceiver, private RequestCallback, private SubProcessCallback, public SettingsAdder {
public:
  MirrorManager();
  ~MirrorManager();
  void init();
  util::Result addJob(const MirrorJob& job, int* id = nullptr);
  util::Result replaceJob(int id, const MirrorJob& job);
  bool removeJob(int id);
  MirrorJob* getJob(int id);
  const MirrorJob* getJob(int id) const;
  const std::map<int, MirrorJob>& getJobs() const;
  bool resetSeenRelease(int id, const std::string& section, const std::string& release);
  bool getRuntimeEnabled() const;
  void setRuntimeEnabled(bool enabled);
  bool toggleRuntimeEnabled();
  bool setEnabled(int id, bool enabled);
  MirrorScanResult scanNow(int id, bool force = false);
  static std::string profileToString(MirrorProfile profile);
  static util::Result stringToProfile(const std::string& profile, MirrorProfile* out);
  static std::unordered_map<std::string, std::string> parseSectionLocalPaths(const std::string& data);
  static std::string sectionLocalPathsToString(const std::unordered_map<std::string, std::string>& data);
  void tick(int message) override;
  void requestReady(void* service, int requestid) override;
  void processExited(int pid, int status) override;
  void processStdOut(int pid, const std::string& text) override;
  void processStdErr(int pid, const std::string& text) override;
  void loadSettings(std::shared_ptr<DataFileHandler> dfh) override;
  void saveSettings(std::shared_ptr<DataFileHandler> dfh) override;
private:
  struct OngoingScanRequest {
    enum class Type {
      SCAN,
      COMPLETE_CHECK_ROOT,
      COMPLETE_CHECK_SUBS,
      RECONCILE_LIST
    };
    Type type;
    int jobid;
    std::string section;
    std::string release;
    std::string sourcesite;
    std::string subpath;
    SiteLogic* sl;
    int requestid;
    bool manual;
    OngoingScanRequest(Type type, int jobid, const std::string& section, const std::string& release,
      const std::string& sourcesite, const std::string& subpath, SiteLogic* sl, int requestid, bool manual)
      : type(type), jobid(jobid), section(section), release(release), sourcesite(sourcesite), subpath(subpath), sl(sl), requestid(requestid), manual(manual) {}
  };
  struct ReconcileState {
    std::string section;
    std::string release;
    std::string sourcesite;
    std::list<std::string> pending;
    std::unordered_set<std::string> remotepaths;
    bool active;
    ReconcileState() : active(false) {}
  };
  struct OngoingWebhook {
    int pid;
    int jobid;
    std::string section;
    std::string release;
    std::string stdoutdata;
    std::string stderrdata;
    OngoingWebhook(int pid, int jobid, const std::string& section, const std::string& release)
      : pid(pid), jobid(jobid), section(section), release(release) {}
  };
  util::Result validate(const MirrorJob& job, int ignoreid = -1) const;
  util::Result startScan(MirrorJob& job, bool manual, bool force);
  void processCompleteCheckRootResult(MirrorJob& job, const OngoingScanRequest& req);
  void processCompleteCheckSubsResult(MirrorJob& job, const OngoingScanRequest& req);
  void processReconcileListResult(MirrorJob& job, const OngoingScanRequest& req);
  void processRequestResult(MirrorJob& job, const OngoingScanRequest& req);
  void completeScan(MirrorJob& job);
  void processPostActions(MirrorJob& job);
  bool requestSourceCompleteChecks(MirrorJob& job, const std::string& section, const std::string& release);
  bool ensureLocalReconciled(MirrorJob& job, const std::string& section, const std::string& release, const std::string& sourcesite);
  util::Result syncLocalToRemote(MirrorJob& job, const std::string& section, const std::string& release,
    const std::unordered_set<std::string>& remotepaths, int* deletedcount);
  static bool isCompleteMarkerName(const std::string& name);
  static bool isSubsDirName(const std::string& name);
  static bool shouldSkipRemoteSyncEntry(const std::string& name);
  static bool shouldSkipLocalSyncEntry(const std::string& relpath, bool isdir);
  static bool hasLocalPayload(const Path& root, bool* out);
  static util::Result collectLocalEntries(const Path& root, const std::string& relpath,
    std::vector<std::pair<std::string, bool> >* entries);
  static util::Result removeLocalPath(const Path& root, const std::string& relpath, bool isdir);
  static std::string reconcileKey(int jobid, const std::string& section, const std::string& release);
  ReconcileState* getReconcileState(int jobid, const std::string& section, const std::string& release);
  bool isCompleteCheckActive(int jobid, const std::string& section, const std::string& release, const std::string& sourcesite) const;
  bool createDoneFile(MirrorJob& job, const std::string& section, const std::string& release);
  bool sendWebhook(MirrorJob& job, const std::string& section, const std::string& release);
  std::shared_ptr<Race> getRace(const std::string& section, const std::string& release) const;
  bool isTargetComplete(const std::shared_ptr<Race>& race, const std::string& targetsite) const;
  bool isWebhookActive(int jobid, const std::string& section, const std::string& release) const;
  static std::string escapeJson(const std::string& text);
  bool spreadJobExists(const std::string& section, const std::string& release) const;
  util::Result triggerSpreadJob(MirrorJob& job, const std::string& section, const std::string& release);
  bool isReleaseSkiplistDenied(const MirrorJob& job, const std::string& section, const std::string& release) const;
  std::string normalizeAndValidateName(const std::string& name) const;
  static std::string encodeString(const std::string& text);
  static std::string decodeString(const std::string& b64);
  static std::list<std::string> dedupeList(const std::list<std::string>& in);

  std::map<int, MirrorJob> jobs;
  std::unordered_map<std::string, ReconcileState> reconcilestates;
  std::list<OngoingScanRequest> ongoingrequests;
  std::list<OngoingWebhook> ongoingwebhooks;
  int nextid;
  bool initialized;
  bool runtimeenabled;
};
