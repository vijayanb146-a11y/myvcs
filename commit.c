#include "common.h"

Commit *findCommit(int id) {
    Commit *c;
    for (c = commitList; c != NULL; c = c->next)
        if (c->id == id) return c;
    return NULL;
}

Commit *findRepoCommit(int repoId, int id) {
    Commit *c = findCommit(id);
    if (c == NULL || c->repoId != repoId) return NULL;
    return c;
}

static int nextCommitId(void) {
    Commit *c;
    int id = 1;
    for (c = commitList; c != NULL; c = c->next)
        if (c->id >= id) id = c->id + 1;
    return id;
}

int createCommit(const char *message, int mergeParentId) {
    ManifestEntry entries[MAX_FILES];
    TrackedFile *f, *next;
    Commit *c, *tail;
    Branch *b = activeBranchPtr();
    char dir[PATH_LEN], src[PATH_LEN], dst[PATH_LEN];
    int id, count = 0, ok;

    if (b == NULL) {
        printf("Active branch not found.\n");
        return 0;
    }
    id = nextCommitId();
    snapshotDir(id, dir);
    MAKE_DIR(dir);

    for (f = fileList; f != NULL; f = next) {
        next = f->next;
        if (f->repoId != activeRepo) continue;
        workingPath(activeRepo, f->name, src);
        if (!fileExists(src)) {
            printf("Warning: '%s' is missing from the working directory and was removed.\n", f->name);
            untrackFile(activeRepo, f->name);
            continue;
        }
        snapshotPath(id, f->name, dst);
        if (!copyFile(src, dst)) {
            printf("Error: could not snapshot '%s'. Commit aborted.\n", f->name);
            return 0;
        }
        strcpy(entries[count].name, f->name);
        entries[count].checksum = fileChecksum(dst, &ok);
        count++;
    }
    if (!saveManifest(id, entries, count)) {
        printf("Error: could not write the commit manifest.\n");
        return 0;
    }

    c = malloc(sizeof(Commit));
    if (c == NULL) {
        printf("Out of memory.\n");
        return 0;
    }
    c->id = id;
    c->repoId = activeRepo;
    c->parentId = b->headId;
    c->mergeParentId = mergeParentId;
    c->fileCount = count;
    strcpy(c->branch, activeBranch);
    strcpy(c->author, currentUser);
    currentTime(c->timestamp);
    strncpy(c->message, message, MSG_LEN - 1);
    c->message[MSG_LEN - 1] = '\0';
    c->next = NULL;
    if (commitList == NULL) commitList = c;
    else {
        for (tail = commitList; tail->next != NULL; tail = tail->next);
        tail->next = c;
    }
    b->headId = id;
    saveCommits();
    saveBranches();
    saveFiles();
    clearEditHistory();
    logActivity("Commit %d \"%s\" on %s/%s", id, c->message, activeRepoName(), activeBranch);
    return id;
}

int hasUncommittedChanges(void) {
    ManifestEntry entries[MAX_FILES];
    TrackedFile *f;
    Branch *b = activeBranchPtr();
    char path[PATH_LEN];
    int count, idx, tracked = 0, ok;

    if (b == NULL) return 0;
    if (b->headId == 0) return countTrackedFiles(activeRepo) > 0;
    count = loadManifest(b->headId, entries, MAX_FILES);
    if (count < 0) return 1;
    for (f = fileList; f != NULL; f = f->next) {
        if (f->repoId != activeRepo) continue;
        tracked++;
        idx = findInManifest(entries, count, f->name);
        if (idx < 0) return 1;
        workingPath(activeRepo, f->name, path);
        if (fileChecksum(path, &ok) != entries[idx].checksum || !ok) return 1;
    }
    return tracked != count;
}

int checkoutCommit(int commitId) {
    ManifestEntry entries[MAX_FILES];
    TrackedFile *f, *next;
    char src[PATH_LEN], dst[PATH_LEN];
    int count, i;

    count = loadManifest(commitId, entries, MAX_FILES);
    if (count < 0) {
        printf("Snapshot of commit %d is missing.\n", commitId);
        return 0;
    }
    clearEditHistory();
    for (f = fileList; f != NULL; f = next) {
        next = f->next;
        if (f->repoId == activeRepo && findInManifest(entries, count, f->name) < 0) {
            workingPath(activeRepo, f->name, dst);
            remove(dst);
            untrackFile(activeRepo, f->name);
        }
    }
    for (i = 0; i < count; i++) {
        snapshotPath(commitId, entries[i].name, src);
        workingPath(activeRepo, entries[i].name, dst);
        if (!copyFile(src, dst)) {
            printf("Error: could not restore '%s'.\n", entries[i].name);
            continue;
        }
        trackFile(activeRepo, entries[i].name);
    }
    saveFiles();
    return 1;
}

static int readAllLines(const char *path, char ***out) {
    FILE *fp;
    char line[LINE_LEN], **lines;
    int count = 0;
    *out = NULL;
    if (path == NULL) return 0;
    fp = fopen(path, "r");
    if (fp == NULL) return 0;
    lines = malloc(sizeof(char *) * MAX_DIFF_LINES);
    if (lines == NULL) {
        fclose(fp);
        return 0;
    }
    while (count < MAX_DIFF_LINES && fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        lines[count] = malloc(strlen(line) + 1);
        if (lines[count] == NULL) break;
        strcpy(lines[count], line);
        count++;
    }
    fclose(fp);
    *out = lines;
    return count;
}

static void freeLines(char **lines, int count) {
    int i;
    for (i = 0; i < count; i++) free(lines[i]);
    free(lines);
}

void diffFiles(const char *oldPath, const char *newPath) {
    char **a, **b;
    int n, m, i, j, added = 0, removed = 0;
    int *dp;

    n = readAllLines(oldPath, &a);
    m = readAllLines(newPath, &b);
    dp = calloc((size_t)(n + 1) * (m + 1), sizeof(int));
    if (dp == NULL) {
        printf("Not enough memory to compare.\n");
        if (a) freeLines(a, n);
        if (b) freeLines(b, m);
        return;
    }
    for (i = n - 1; i >= 0; i--)
        for (j = m - 1; j >= 0; j--) {
            if (strcmp(a[i], b[j]) == 0) dp[i * (m + 1) + j] = dp[(i + 1) * (m + 1) + j + 1] + 1;
            else if (dp[(i + 1) * (m + 1) + j] >= dp[i * (m + 1) + j + 1]) dp[i * (m + 1) + j] = dp[(i + 1) * (m + 1) + j];
            else dp[i * (m + 1) + j] = dp[i * (m + 1) + j + 1];
        }
    i = 0;
    j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && strcmp(a[i], b[j]) == 0) {
            printf("    %s\n", a[i]);
            i++;
            j++;
        } else if (j >= m || (i < n && dp[(i + 1) * (m + 1) + j] >= dp[i * (m + 1) + j + 1])) {
            printf("  - %s\n", a[i]);
            i++;
            removed++;
        } else {
            printf("  + %s\n", b[j]);
            j++;
            added++;
        }
    }
    printf("  (%d line(s) added, %d line(s) removed)\n", added, removed);
    free(dp);
    if (a) freeLines(a, n);
    if (b) freeLines(b, m);
}

void commitChanges(void) {
    char message[MSG_LEN];
    Branch *b;
    int id, mergeParent;

    if (!requireActiveRepo()) return;
    b = activeBranchPtr();
    if (countTrackedFiles(activeRepo) == 0 && (b == NULL || b->headId == 0)) {
        printf("Nothing to commit. Create a file first.\n");
        return;
    }
    if (!hasUncommittedChanges() && pendingMerge == 0) {
        printf("Nothing changed since the last commit.\n");
        return;
    }
    printf("Commit message: ");
    readLine(message, sizeof(message));
    if (message[0] == '\0' || strchr(message, '|') != NULL) {
        printf("Invalid commit message (it must not be empty or contain '|').\n");
        return;
    }
    mergeParent = pendingMerge;
    id = createCommit(message, mergeParent);
    if (id == 0) return;
    pendingMerge = 0;
    saveState();
    printf("[%s %d] %s (%d file(s) saved by %s)\n", activeBranch, id, message,
           findCommit(id)->fileCount, currentUser);
    if (mergeParent) printf("Merge completed with commit %d.\n", id);
}

void showStatus(void) {
    ManifestEntry entries[MAX_FILES];
    TrackedFile *f;
    Branch *b;
    char path[PATH_LEN];
    int count = 0, idx, i, changes = 0, ok;

    if (!requireActiveRepo()) return;
    b = activeBranchPtr();
    if (b == NULL) return;
    printf("\nOn branch %s | Last commit: %d\n", activeBranch, b->headId);
    if (pendingMerge) printf("Merge in progress with commit %d. Resolve conflicts and commit.\n", pendingMerge);
    if (b->headId) count = loadManifest(b->headId, entries, MAX_FILES);
    if (count < 0) count = 0;

    for (f = fileList; f != NULL; f = f->next) {
        if (f->repoId != activeRepo) continue;
        idx = findInManifest(entries, count, f->name);
        workingPath(activeRepo, f->name, path);
        if (idx < 0) {
            printf("  new file : %s\n", f->name);
            changes++;
        } else if (fileChecksum(path, &ok) != entries[idx].checksum) {
            printf("  modified : %s\n", f->name);
            changes++;
        } else printf("  unchanged: %s\n", f->name);
    }
    for (i = 0; i < count; i++) {
        if (findTrackedFile(activeRepo, entries[i].name) == NULL) {
            printf("  deleted  : %s\n", entries[i].name);
            changes++;
        }
    }
    if (changes == 0) printf("Working directory clean. Nothing to commit.\n");
    else printf("%d change(s) not committed yet.\n", changes);
}

void showWorkingChanges(void) {
    ManifestEntry entries[MAX_FILES];
    TrackedFile *f;
    Branch *b;
    char oldPath[PATH_LEN], newPath[PATH_LEN];
    int count = 0, idx, i, shown = 0, ok;

    if (!requireActiveRepo()) return;
    b = activeBranchPtr();
    if (b == NULL) return;
    if (b->headId) count = loadManifest(b->headId, entries, MAX_FILES);
    if (count < 0) count = 0;
    for (f = fileList; f != NULL; f = f->next) {
        if (f->repoId != activeRepo) continue;
        idx = findInManifest(entries, count, f->name);
        workingPath(activeRepo, f->name, newPath);
        if (idx >= 0 && fileChecksum(newPath, &ok) == entries[idx].checksum) continue;
        printf("\n=== %s (%s) ===\n", f->name, idx < 0 ? "new file" : "modified");
        if (idx >= 0) snapshotPath(b->headId, f->name, oldPath);
        diffFiles(idx >= 0 ? oldPath : NULL, newPath);
        shown++;
    }
    for (i = 0; i < count; i++) {
        if (findTrackedFile(activeRepo, entries[i].name) != NULL) continue;
        printf("\n=== %s (deleted) ===\n", entries[i].name);
        snapshotPath(b->headId, entries[i].name, oldPath);
        diffFiles(oldPath, NULL);
        shown++;
    }
    if (shown == 0) printf("No changes since the last commit.\n");
}

static void printCommit(Commit *c, int isHead) {
    printf("commit %d%s\n", c->id, isHead ? "  (HEAD)" : "");
    if (c->mergeParentId) printf("Merge  : %d + %d\n", c->parentId, c->mergeParentId);
    else printf("Parent : %d\n", c->parentId);
    printf("Author : %s | Date: %s | Branch: %s | Files: %d\n", c->author, c->timestamp, c->branch, c->fileCount);
    printf("    %s\n\n", c->message);
}

void showBranchHistory(void) {
    Branch *b;
    Commit *c;
    int id, first = 1;

    if (!requireActiveRepo()) return;
    b = activeBranchPtr();
    if (b == NULL || b->headId == 0) {
        printf("No commits on branch '%s' yet.\n", activeBranch);
        return;
    }
    printf("\n----- History of branch '%s' (newest first) -----\n\n", activeBranch);
    id = b->headId;
    while (id != 0) {
        c = findCommit(id);
        if (c == NULL) break;
        printCommit(c, first);
        first = 0;
        id = c->parentId;
    }
}

void showRepositoryLog(void) {
    Commit *c;
    Branch *b;
    int found = 0, isHead;

    if (!requireActiveRepo()) return;
    printf("\n%-6s %-8s %-12s %-12s %-17s %s\n", "Commit", "Parent", "Branch", "Author", "Date", "Message");
    printf("-------------------------------------------------------------------------------\n");
    for (c = commitList; c != NULL; c = c->next) {
        if (c->repoId != activeRepo) continue;
        isHead = 0;
        for (b = branchList; b != NULL; b = b->next)
            if (b->repoId == activeRepo && b->headId == c->id) isHead = 1;
        printf("%-6d %-8d %-12s %-12s %-17s %s%s\n", c->id, c->parentId, c->branch, c->author,
               c->timestamp, c->message, isHead ? "  [branch head]" : "");
        found = 1;
    }
    if (!found) printf("No commits in this repository yet.\n");
}

static int askCommitId(const char *prompt) {
    int id;
    printf("%s", prompt);
    id = readInt();
    if (findRepoCommit(activeRepo, id) == NULL) {
        printf("Commit %d does not belong to this repository.\n", id);
        return 0;
    }
    return id;
}

void compareCommits(void) {
    ManifestEntry a[MAX_FILES], b[MAX_FILES];
    char pathA[PATH_LEN], pathB[PATH_LEN];
    int idA, idB, countA, countB, i, j, changed = 0;

    if (!requireActiveRepo()) return;
    showRepositoryLog();
    if ((idA = askCommitId("Older commit ID: ")) == 0) return;
    if ((idB = askCommitId("Newer commit ID: ")) == 0) return;
    countA = loadManifest(idA, a, MAX_FILES);
    countB = loadManifest(idB, b, MAX_FILES);
    if (countA < 0 || countB < 0) {
        printf("A snapshot is missing. Run the integrity check.\n");
        return;
    }
    printf("\n===== Changes from commit %d to commit %d =====\n", idA, idB);
    for (i = 0; i < countA; i++) {
        j = findInManifest(b, countB, a[i].name);
        snapshotPath(idA, a[i].name, pathA);
        if (j < 0) {
            printf("\n--- %s (deleted) ---\n", a[i].name);
            diffFiles(pathA, NULL);
            changed++;
        } else if (a[i].checksum != b[j].checksum) {
            printf("\n--- %s (modified) ---\n", a[i].name);
            snapshotPath(idB, b[j].name, pathB);
            diffFiles(pathA, pathB);
            changed++;
        }
    }
    for (j = 0; j < countB; j++) {
        if (findInManifest(a, countA, b[j].name) >= 0) continue;
        printf("\n--- %s (added) ---\n", b[j].name);
        snapshotPath(idB, b[j].name, pathB);
        diffFiles(NULL, pathB);
        changed++;
    }
    if (changed == 0) printf("Both commits contain identical files.\n");
    else printf("\n%d file(s) differ.\n", changed);
}

void viewFileAtCommit(void) {
    ManifestEntry entries[MAX_FILES];
    char name[NAME_LEN], path[PATH_LEN];
    int id, count, i;

    if (!requireActiveRepo()) return;
    showRepositoryLog();
    if ((id = askCommitId("Commit ID to look into: ")) == 0) return;
    count = loadManifest(id, entries, MAX_FILES);
    if (count <= 0) {
        printf("Commit %d has no files.\n", id);
        return;
    }
    printf("Files in commit %d:\n", id);
    for (i = 0; i < count; i++) printf("  %s\n", entries[i].name);
    printf("File name: ");
    readLine(name, sizeof(name));
    if (findInManifest(entries, count, name) < 0) {
        printf("'%s' is not in commit %d.\n", name, id);
        return;
    }
    snapshotPath(id, name, path);
    printf("\n----- %s as it was in commit %d (read only) -----\n", name, id);
    printFileWithNumbers(path);
}

void rollbackToCommit(void) {
    char message[MSG_LEN];
    int id, newId;

    if (!requireActiveRepo()) return;
    showBranchHistory();
    if ((id = askCommitId("Commit ID to restore: ")) == 0) return;
    if (hasUncommittedChanges() && !askYesNo("Uncommitted changes will be lost. Continue?")) {
        printf("Rollback cancelled.\n");
        return;
    }
    if (!checkoutCommit(id)) return;
    if (!hasUncommittedChanges()) {
        printf("The working files already match commit %d.\n", id);
        return;
    }
    snprintf(message, sizeof(message), "Rollback to commit %d", id);
    newId = createCommit(message, 0);
    if (newId) printf("Files restored to commit %d and saved as new commit %d.\n", id, newId);
}

typedef struct WordNode {
    char word[NAME_LEN];
    int commits[64];
    int count;
    struct WordNode *next;
} WordNode;

static unsigned int hashWord(const char *word) {
    unsigned int key = 0;
    while (*word) key = key * 31 + (unsigned char)*word++;
    return key % HASH_SIZE;
}

static void addWord(WordNode **table, const char *word, int commitId) {
    unsigned int index = hashWord(word);
    WordNode *node;
    for (node = table[index]; node != NULL; node = node->next) {
        if (strcmp(node->word, word) == 0) {
            if (node->commits[node->count - 1] != commitId && node->count < 64)
                node->commits[node->count++] = commitId;
            return;
        }
    }
    node = malloc(sizeof(WordNode));
    if (node == NULL) return;
    strcpy(node->word, word);
    node->commits[0] = commitId;
    node->count = 1;
    node->next = table[index];
    table[index] = node;
}

static void toLowerWord(char *s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
}

void searchCommits(void) {
    WordNode *table[HASH_SIZE], *node, *next;
    Commit *c;
    char word[NAME_LEN], key[NAME_LEN];
    const char *p;
    int i, len, words = 0, used = 0, longest = 0, chain;
    unsigned int index;

    if (!requireActiveRepo()) return;
    for (i = 0; i < HASH_SIZE; i++) table[i] = NULL;
    for (c = commitList; c != NULL; c = c->next) {
        if (c->repoId != activeRepo) continue;
        p = c->message;
        while (*p) {
            while (*p && !((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) p++;
            len = 0;
            while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9'))) {
                if (len < NAME_LEN - 1) word[len++] = *p;
                p++;
            }
            word[len] = '\0';
            if (len == 0) continue;
            toLowerWord(word);
            addWord(table, word, c->id);
        }
    }
    for (i = 0; i < HASH_SIZE; i++) {
        chain = 0;
        for (node = table[i]; node != NULL; node = node->next) chain++;
        words += chain;
        if (chain) used++;
        if (chain > longest) longest = chain;
    }
    if (words == 0) {
        printf("No commits in this repository yet.\n");
        return;
    }
    printf("Indexed %d distinct word(s) from commit messages into a hash table of %d buckets.\n", words, HASH_SIZE);
    printf("Keyword to search: ");
    readLine(key, sizeof(key));
    toLowerWord(key);
    index = hashWord(key);
    printf("Bucket %u (key mod %d), searching its chain...\n", index, HASH_SIZE);
    for (node = table[index]; node != NULL; node = node->next)
        if (strcmp(node->word, key) == 0) break;
    if (node == NULL) printf("No commit message contains '%s'.\n", key);
    else {
        printf("\n'%s' appears in %d commit(s):\n", key, node->count);
        for (i = node->count - 1; i >= 0; i--) {
            c = findCommit(node->commits[i]);
            if (c != NULL)
                printf("  commit %-4d %-12s %-10s %s  \"%s\"\n", c->id, c->branch, c->author, c->timestamp, c->message);
        }
    }
    printf("(%d of %d buckets used, longest chain %d)\n", used, HASH_SIZE, longest);
    for (i = 0; i < HASH_SIZE; i++)
        for (node = table[i]; node != NULL; node = next) {
            next = node->next;
            free(node);
        }
}
