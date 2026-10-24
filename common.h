#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0755)
#define REMOVE_DIR(path) rmdir(path)
#endif

#define NAME_LEN 64
#define MSG_LEN 128
#define PATH_LEN 320
#define LINE_LEN 1024
#define TIME_LEN 32
#define MAX_FILES 64
#define MAX_DIFF_LINES 500

#define DATA_DIR "data"
#define REPO_DIR "data/repos"
#define SNAP_DIR "data/snapshots"
#define UNDO_DIR "data/undo"
#define MANIFEST_NAME "MANIFEST.mgh"
#define LOG_SIZE 10
#define MAX_UNDO 20
#define HASH_SIZE 31

typedef struct Repository {
    int id;
    char name[NAME_LEN];
    char owner[NAME_LEN];
    char created[TIME_LEN];
    struct Repository *next;
} Repository;

typedef struct TrackedFile {
    int repoId;
    char name[NAME_LEN];
    struct TrackedFile *next;
} TrackedFile;

typedef struct Commit {
    int id;
    int repoId;
    int parentId;
    int mergeParentId;
    int fileCount;
    char branch[NAME_LEN];
    char author[NAME_LEN];
    char timestamp[TIME_LEN];
    char message[MSG_LEN];
    struct Commit *next;
} Commit;

typedef struct Branch {
    int repoId;
    int headId;
    char name[NAME_LEN];
    struct Branch *next;
} Branch;

typedef struct {
    char name[NAME_LEN];
    unsigned int checksum;
} ManifestEntry;

extern Repository *repoList;
extern TrackedFile *fileList;
extern Commit *commitList;
extern Branch *branchList;
extern int activeRepo;
extern int pendingMerge;
extern char activeBranch[NAME_LEN];
extern char currentUser[NAME_LEN];

void readLine(char *buf, int size);
int readInt(void);
int askYesNo(const char *question);
int isValidName(const char *s);
int isValidFileName(const char *s);
void currentTime(char *out);
void ensureDataDirs(void);
void workingDir(int repoId, char *out);
void workingPath(int repoId, const char *file, char *out);
void snapshotDir(int commitId, char *out);
void snapshotPath(int commitId, const char *file, char *out);
int fileExists(const char *path);
int copyFile(const char *src, const char *dst);
unsigned int fileChecksum(const char *path, int *ok);
int countLines(const char *path);
int loadManifest(int commitId, ManifestEntry *entries, int max);
int saveManifest(int commitId, ManifestEntry *entries, int count);
int findInManifest(ManifestEntry *entries, int count, const char *name);
void loadAll(void);
void saveRepositories(void);
void saveFiles(void);
void saveCommits(void);
void saveBranches(void);
void saveState(void);
void freeAll(void);
void logActivity(const char *format, ...);
void showActivityLog(void);

Repository *findRepoById(int id);
Repository *findRepoByName(const char *name);
int requireActiveRepo(void);
const char *activeRepoName(void);
void createRepository(void);
void listRepositories(void);
void selectRepository(void);
void deleteRepository(void);

TrackedFile *findTrackedFile(int repoId, const char *name);
int trackFile(int repoId, const char *name);
void untrackFile(int repoId, const char *name);
int countTrackedFiles(int repoId);
void printFileWithNumbers(const char *path);
void createFile(void);
void editFile(void);
void viewFile(void);
void listFiles(void);
void deleteFile(void);
void clearEditHistory(void);
void undoEdit(void);
void redoEdit(void);
void showEditHistory(void);

Commit *findCommit(int id);
Commit *findRepoCommit(int repoId, int id);
int createCommit(const char *message, int mergeParentId);
int hasUncommittedChanges(void);
int checkoutCommit(int commitId);
void diffFiles(const char *oldPath, const char *newPath);
void commitChanges(void);
void showStatus(void);
void showWorkingChanges(void);
void showBranchHistory(void);
void showRepositoryLog(void);
void compareCommits(void);
void viewFileAtCommit(void);
void rollbackToCommit(void);
void searchCommits(void);

Branch *findBranch(int repoId, const char *name);
Branch *activeBranchPtr(void);
int addBranch(int repoId, const char *name, int headId);
void createBranch(void);
void listBranches(void);
void switchBranch(void);
void deleteBranch(void);

int findCommonAncestor(int commitA, int commitB);
void previewMerge(void);
void mergeBranch(void);

void showDependencyGraph(void);
void analyzeFileImpact(void);
void analyzeCommitImpact(void);
void detectCircularDependencies(void);
void showRiskReport(void);
void showBuildOrder(void);

void verifyIntegrity(void);
void showRepositoryStats(void);
void showOwnershipReport(void);
void showLineAuthors(void);

#endif
