#include "common.h"

Repository *repoList = NULL;
TrackedFile *fileList = NULL;
Commit *commitList = NULL;
Branch *branchList = NULL;
int activeRepo = 0;
int pendingMerge = 0;
char activeBranch[NAME_LEN] = "main";
char currentUser[NAME_LEN] = "guest";

static char activityLog[LOG_SIZE][LINE_LEN];
static int logFront = 0, logRear = -1, logCount = 0;

static void enqueueActivity(const char *entry) {
    if (logCount == LOG_SIZE) {
        logFront = (logFront + 1) % LOG_SIZE;
        logCount--;
    }
    logRear = (logRear + 1) % LOG_SIZE;
    strncpy(activityLog[logRear], entry, LINE_LEN - 1);
    activityLog[logRear][LINE_LEN - 1] = '\0';
    logCount++;
}

static void saveActivity(void) {
    FILE *fp = fopen(DATA_DIR "/activity.dat", "w");
    int i;
    if (fp == NULL) return;
    for (i = 0; i < logCount; i++)
        fprintf(fp, "%s\n", activityLog[(logFront + i) % LOG_SIZE]);
    fclose(fp);
}

static void loadActivity(void) {
    FILE *fp = fopen(DATA_DIR "/activity.dat", "r");
    char line[LINE_LEN];
    if (fp == NULL) return;
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '\0') enqueueActivity(line);
    }
    fclose(fp);
}

void logActivity(const char *format, ...) {
    char message[MSG_LEN * 2], entry[LINE_LEN], when[TIME_LEN];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    currentTime(when);
    snprintf(entry, sizeof(entry), "%s | %-10s | %s", when, currentUser, message);
    enqueueActivity(entry);
    saveActivity();
}

void showActivityLog(void) {
    int i;
    printf("\n===== Recent Activity (newest first, last %d actions kept) =====\n", LOG_SIZE);
    if (logCount == 0) {
        printf("No activity yet.\n");
        return;
    }
    for (i = logCount - 1; i >= 0; i--)
        printf("  %s\n", activityLog[(logFront + i) % LOG_SIZE]);
}

void readLine(char *buf, int size) {
    int c;
    if (fgets(buf, size, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    if (strchr(buf, '\n') == NULL)
        while ((c = getchar()) != '\n' && c != EOF);
    buf[strcspn(buf, "\r\n")] = '\0';
}

int readInt(void) {
    char buf[64], *end;
    long value;
    readLine(buf, sizeof(buf));
    if (buf[0] == '\0') return -1;
    value = strtol(buf, &end, 10);
    if (*end != '\0') return -1;
    return (int)value;
}

int askYesNo(const char *question) {
    char buf[16];
    printf("%s (y/n): ", question);
    readLine(buf, sizeof(buf));
    return buf[0] == 'y' || buf[0] == 'Y';
}

int isValidName(const char *s) {
    size_t len = strlen(s);
    if (len == 0 || len >= NAME_LEN) return 0;
    if (strchr(s, '|') != NULL) return 0;
    if (s[0] == ' ' || s[len - 1] == ' ') return 0;
    return 1;
}

int isValidFileName(const char *s) {
    if (!isValidName(s)) return 0;
    if (strpbrk(s, "/\\:*?\"<>") != NULL) return 0;
    if (strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return 0;
    if (strcmp(s, MANIFEST_NAME) == 0) return 0;
    return 1;
}

void currentTime(char *out) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(out, TIME_LEN, "%Y-%m-%d %H:%M", t);
}

void ensureDataDirs(void) {
    MAKE_DIR(DATA_DIR);
    MAKE_DIR(REPO_DIR);
    MAKE_DIR(SNAP_DIR);
    MAKE_DIR(UNDO_DIR);
}

void workingDir(int repoId, char *out) {
    snprintf(out, PATH_LEN, "%s/repo_%d", REPO_DIR, repoId);
}

void workingPath(int repoId, const char *file, char *out) {
    snprintf(out, PATH_LEN, "%s/repo_%d/%s", REPO_DIR, repoId, file);
}

void snapshotDir(int commitId, char *out) {
    snprintf(out, PATH_LEN, "%s/commit_%d", SNAP_DIR, commitId);
}

void snapshotPath(int commitId, const char *file, char *out) {
    snprintf(out, PATH_LEN, "%s/commit_%d/%s", SNAP_DIR, commitId, file);
}

int fileExists(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) return 0;
    fclose(fp);
    return 1;
}

int copyFile(const char *src, const char *dst) {
    FILE *in, *out;
    char buffer[4096];
    size_t n;
    int ok = 1;
    in = fopen(src, "rb");
    if (in == NULL) return 0;
    out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, n, out) != n) {
            ok = 0;
            break;
        }
    }
    if (ferror(in)) ok = 0;
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    return ok;
}

unsigned int fileChecksum(const char *path, int *ok) {
    FILE *fp = fopen(path, "rb");
    unsigned int hash = 2166136261u;
    int c;
    if (fp == NULL) {
        if (ok) *ok = 0;
        return 0;
    }
    while ((c = fgetc(fp)) != EOF) {
        hash ^= (unsigned char)c;
        hash *= 16777619u;
    }
    fclose(fp);
    if (ok) *ok = 1;
    return hash;
}

int countLines(const char *path) {
    FILE *fp = fopen(path, "r");
    char line[LINE_LEN];
    int count = 0;
    if (fp == NULL) return 0;
    while (fgets(line, sizeof(line), fp)) count++;
    fclose(fp);
    return count;
}

int loadManifest(int commitId, ManifestEntry *entries, int max) {
    char path[PATH_LEN], line[LINE_LEN];
    FILE *fp;
    int count = 0;
    if (commitId == 0) return 0;
    snapshotPath(commitId, MANIFEST_NAME, path);
    fp = fopen(path, "r");
    if (fp == NULL) return -1;
    while (count < max && fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%63[^|]|%u", entries[count].name, &entries[count].checksum) == 2)
            count++;
    }
    fclose(fp);
    return count;
}

int saveManifest(int commitId, ManifestEntry *entries, int count) {
    char path[PATH_LEN];
    FILE *fp;
    int i;
    snapshotPath(commitId, MANIFEST_NAME, path);
    fp = fopen(path, "w");
    if (fp == NULL) return 0;
    for (i = 0; i < count; i++)
        fprintf(fp, "%s|%u\n", entries[i].name, entries[i].checksum);
    fclose(fp);
    return 1;
}

int findInManifest(ManifestEntry *entries, int count, const char *name) {
    int i;
    for (i = 0; i < count; i++)
        if (strcmp(entries[i].name, name) == 0) return i;
    return -1;
}

static void loadRepositories(void) {
    FILE *fp = fopen(DATA_DIR "/repositories.dat", "r");
    char line[LINE_LEN];
    Repository *tail = NULL, *r;
    if (fp == NULL) return;
    while (fgets(line, sizeof(line), fp)) {
        r = malloc(sizeof(Repository));
        if (r == NULL) break;
        if (sscanf(line, "%d|%63[^|]|%63[^|]|%31[^\r\n]", &r->id, r->name, r->owner, r->created) != 4) {
            free(r);
            continue;
        }
        r->next = NULL;
        if (tail == NULL) repoList = r;
        else tail->next = r;
        tail = r;
    }
    fclose(fp);
}

static void loadFiles(void) {
    FILE *fp = fopen(DATA_DIR "/files.dat", "r");
    char line[LINE_LEN];
    TrackedFile *tail = NULL, *f;
    if (fp == NULL) return;
    while (fgets(line, sizeof(line), fp)) {
        f = malloc(sizeof(TrackedFile));
        if (f == NULL) break;
        if (sscanf(line, "%d|%63[^\r\n]", &f->repoId, f->name) != 2) {
            free(f);
            continue;
        }
        f->next = NULL;
        if (tail == NULL) fileList = f;
        else tail->next = f;
        tail = f;
    }
    fclose(fp);
}

static void loadCommits(void) {
    FILE *fp = fopen(DATA_DIR "/commits.dat", "r");
    char line[LINE_LEN];
    Commit *tail = NULL, *c;
    if (fp == NULL) return;
    while (fgets(line, sizeof(line), fp)) {
        c = malloc(sizeof(Commit));
        if (c == NULL) break;
        if (sscanf(line, "%d|%d|%d|%d|%d|%63[^|]|%63[^|]|%31[^|]|%127[^\r\n]",
                   &c->id, &c->repoId, &c->parentId, &c->mergeParentId, &c->fileCount,
                   c->branch, c->author, c->timestamp, c->message) != 9) {
            free(c);
            continue;
        }
        c->next = NULL;
        if (tail == NULL) commitList = c;
        else tail->next = c;
        tail = c;
    }
    fclose(fp);
}

static void loadBranches(void) {
    FILE *fp = fopen(DATA_DIR "/branches.dat", "r");
    char line[LINE_LEN];
    Branch *tail = NULL, *b;
    if (fp == NULL) return;
    while (fgets(line, sizeof(line), fp)) {
        b = malloc(sizeof(Branch));
        if (b == NULL) break;
        if (sscanf(line, "%d|%63[^|]|%d", &b->repoId, b->name, &b->headId) != 3) {
            free(b);
            continue;
        }
        b->next = NULL;
        if (tail == NULL) branchList = b;
        else tail->next = b;
        tail = b;
    }
    fclose(fp);
}

static void loadState(void) {
    FILE *fp = fopen(DATA_DIR "/state.dat", "r");
    char line[LINE_LEN], branch[NAME_LEN], user[NAME_LEN];
    int repo, merge;
    if (fp == NULL) return;
    if (fgets(line, sizeof(line), fp) &&
        sscanf(line, "%d|%63[^|]|%63[^|]|%d", &repo, branch, user, &merge) == 4) {
        strcpy(currentUser, user);
        if (findRepoById(repo) != NULL && findBranch(repo, branch) != NULL) {
            activeRepo = repo;
            strcpy(activeBranch, branch);
            pendingMerge = merge;
        }
    }
    fclose(fp);
}

void loadAll(void) {
    ensureDataDirs();
    loadRepositories();
    loadFiles();
    loadCommits();
    loadBranches();
    loadState();
    loadActivity();
}

void saveRepositories(void) {
    FILE *fp = fopen(DATA_DIR "/repositories.dat", "w");
    Repository *r;
    if (fp == NULL) {
        printf("Error: could not save repositories.\n");
        return;
    }
    for (r = repoList; r != NULL; r = r->next)
        fprintf(fp, "%d|%s|%s|%s\n", r->id, r->name, r->owner, r->created);
    fclose(fp);
}

void saveFiles(void) {
    FILE *fp = fopen(DATA_DIR "/files.dat", "w");
    TrackedFile *f;
    if (fp == NULL) {
        printf("Error: could not save the file list.\n");
        return;
    }
    for (f = fileList; f != NULL; f = f->next)
        fprintf(fp, "%d|%s\n", f->repoId, f->name);
    fclose(fp);
}

void saveCommits(void) {
    FILE *fp = fopen(DATA_DIR "/commits.dat", "w");
    Commit *c;
    if (fp == NULL) {
        printf("Error: could not save commits.\n");
        return;
    }
    for (c = commitList; c != NULL; c = c->next)
        fprintf(fp, "%d|%d|%d|%d|%d|%s|%s|%s|%s\n", c->id, c->repoId, c->parentId, c->mergeParentId,
                c->fileCount, c->branch, c->author, c->timestamp, c->message);
    fclose(fp);
}

void saveBranches(void) {
    FILE *fp = fopen(DATA_DIR "/branches.dat", "w");
    Branch *b;
    if (fp == NULL) {
        printf("Error: could not save branches.\n");
        return;
    }
    for (b = branchList; b != NULL; b = b->next)
        fprintf(fp, "%d|%s|%d\n", b->repoId, b->name, b->headId);
    fclose(fp);
}

void saveState(void) {
    FILE *fp = fopen(DATA_DIR "/state.dat", "w");
    if (fp == NULL) return;
    fprintf(fp, "%d|%s|%s|%d\n", activeRepo, activeBranch, currentUser, pendingMerge);
    fclose(fp);
}

void freeAll(void) {
    Repository *r;
    TrackedFile *f;
    Commit *c;
    Branch *b;
    while (repoList != NULL) {
        r = repoList;
        repoList = r->next;
        free(r);
    }
    while (fileList != NULL) {
        f = fileList;
        fileList = f->next;
        free(f);
    }
    while (commitList != NULL) {
        c = commitList;
        commitList = c->next;
        free(c);
    }
    while (branchList != NULL) {
        b = branchList;
        branchList = b->next;
        free(b);
    }
}
