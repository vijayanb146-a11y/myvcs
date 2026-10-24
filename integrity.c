#include "common.h"

void verifyIntegrity(void) {
    ManifestEntry entries[MAX_FILES];
    TrackedFile *f;
    Commit *c;
    Branch *b;
    char path[PATH_LEN];
    int count, i, ok, problems = 0, commits = 0, files = 0;
    unsigned int sum;

    if (!requireActiveRepo()) return;
    printf("\n===== Integrity Check for '%s' =====\n", activeRepoName());
    for (c = commitList; c != NULL; c = c->next) {
        if (c->repoId != activeRepo) continue;
        commits++;
        if (c->parentId && findRepoCommit(activeRepo, c->parentId) == NULL) {
            printf("  BROKEN LINK : commit %d points to missing parent %d\n", c->id, c->parentId);
            problems++;
        }
        if (c->mergeParentId && findRepoCommit(activeRepo, c->mergeParentId) == NULL) {
            printf("  BROKEN LINK : commit %d points to missing merge parent %d\n", c->id, c->mergeParentId);
            problems++;
        }
        count = loadManifest(c->id, entries, MAX_FILES);
        if (count < 0) {
            printf("  MISSING     : snapshot of commit %d\n", c->id);
            problems++;
            continue;
        }
        for (i = 0; i < count; i++) {
            files++;
            snapshotPath(c->id, entries[i].name, path);
            sum = fileChecksum(path, &ok);
            if (!ok) {
                printf("  MISSING     : %s in commit %d\n", entries[i].name, c->id);
                problems++;
            } else if (sum != entries[i].checksum) {
                printf("  CORRUPTED   : %s in commit %d (checksum mismatch)\n", entries[i].name, c->id);
                problems++;
            }
        }
    }
    for (b = branchList; b != NULL; b = b->next) {
        if (b->repoId != activeRepo || b->headId == 0) continue;
        if (findRepoCommit(activeRepo, b->headId) == NULL) {
            printf("  BROKEN LINK : branch '%s' points to missing commit %d\n", b->name, b->headId);
            problems++;
        }
    }
    for (f = fileList; f != NULL; f = f->next) {
        if (f->repoId != activeRepo) continue;
        workingPath(activeRepo, f->name, path);
        if (!fileExists(path)) {
            printf("  MISSING     : working file %s\n", f->name);
            problems++;
        }
    }
    printf("\nChecked %d commit(s) and %d snapshot file(s).\n", commits, files);
    if (problems == 0) printf("Result: repository is healthy. All snapshots match their checksums.\n");
    else printf("Result: %d problem(s) found.\n", problems);
}

void showRepositoryStats(void) {
    char authors[64][NAME_LEN], path[PATH_LEN];
    int authorCommits[64], nAuthors = 0, i, top = 0;
    int commits = 0, merges = 0, branches = 0, lines = 0, files = 0;
    Commit *c;
    Branch *b;
    TrackedFile *f;
    Repository *r;

    if (!requireActiveRepo()) return;
    r = findRepoById(activeRepo);
    for (c = commitList; c != NULL; c = c->next) {
        if (c->repoId != activeRepo) continue;
        commits++;
        if (c->mergeParentId) merges++;
        for (i = 0; i < nAuthors; i++)
            if (strcmp(authors[i], c->author) == 0) break;
        if (i == nAuthors && nAuthors < 64) {
            strcpy(authors[nAuthors], c->author);
            authorCommits[nAuthors++] = 0;
        }
        if (i < 64) authorCommits[i]++;
    }
    for (b = branchList; b != NULL; b = b->next)
        if (b->repoId == activeRepo) branches++;
    for (f = fileList; f != NULL; f = f->next) {
        if (f->repoId != activeRepo) continue;
        workingPath(activeRepo, f->name, path);
        lines += countLines(path);
        files++;
    }
    printf("\n===== Statistics for '%s' =====\n", r->name);
    printf("Owner        : %s\n", r->owner);
    printf("Created      : %s\n", r->created);
    printf("Files        : %d (%d lines of code)\n", files, lines);
    printf("Commits      : %d (%d merge commit(s))\n", commits, merges);
    printf("Branches     : %d\n", branches);
    printf("Contributors : %d\n", nAuthors);
    for (i = 0; i < nAuthors; i++) {
        printf("  %-15s %d commit(s)\n", authors[i], authorCommits[i]);
        if (authorCommits[i] > authorCommits[top]) top = i;
    }
    if (nAuthors > 0) printf("Top contributor: %s\n", authors[top]);
}


#define MAX_AUTHORS 16

typedef struct {
    char name[NAME_LEN];
    int count;
    char **lines;
    int *author;
} FileBlame;

static char blameAuthors[MAX_AUTHORS][NAME_LEN];
static int blameAuthorCount;

static int authorIndex(const char *name) {
    int i;
    for (i = 0; i < blameAuthorCount; i++)
        if (strcmp(blameAuthors[i], name) == 0) return i;
    if (blameAuthorCount == MAX_AUTHORS) return MAX_AUTHORS - 1;
    strcpy(blameAuthors[blameAuthorCount], name);
    return blameAuthorCount++;
}

static int loadSnapshotLines(int commitId, const char *name, char ***out) {
    char path[PATH_LEN], line[LINE_LEN], **lines;
    FILE *fp;
    int count = 0;
    *out = NULL;
    snapshotPath(commitId, name, path);
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

static void freeBlame(FileBlame *fb) {
    int i;
    for (i = 0; i < fb->count; i++) free(fb->lines[i]);
    free(fb->lines);
    free(fb->author);
    fb->lines = NULL;
    fb->author = NULL;
    fb->count = 0;
}

static void updateBlame(FileBlame *fb, char **newLines, int m, int commitAuthor,
                        char **mergedLines, int mergedCount, int mergedAuthor) {
    int n = fb->count, i, j, k, *dp, *newAuthor, *matched;
    newAuthor = malloc(sizeof(int) * (m > 0 ? m : 1));
    dp = calloc((size_t)(n + 1) * (m + 1), sizeof(int));
    if (newAuthor == NULL || dp == NULL) {
        free(newAuthor);
        free(dp);
        for (j = 0; j < m; j++) free(newLines[j]);
        free(newLines);
        return;
    }
    for (i = n - 1; i >= 0; i--)
        for (j = m - 1; j >= 0; j--) {
            if (strcmp(fb->lines[i], newLines[j]) == 0) dp[i * (m + 1) + j] = dp[(i + 1) * (m + 1) + j + 1] + 1;
            else if (dp[(i + 1) * (m + 1) + j] >= dp[i * (m + 1) + j + 1]) dp[i * (m + 1) + j] = dp[(i + 1) * (m + 1) + j];
            else dp[i * (m + 1) + j] = dp[i * (m + 1) + j + 1];
        }
    matched = calloc(m > 0 ? m : 1, sizeof(int));
    for (j = 0; j < m; j++) newAuthor[j] = commitAuthor;
    i = 0;
    j = 0;
    while (i < n && j < m) {
        if (strcmp(fb->lines[i], newLines[j]) == 0) {
            newAuthor[j] = fb->author[i];
            if (matched) matched[j] = 1;
            i++;
            j++;
        } else if (dp[(i + 1) * (m + 1) + j] >= dp[i * (m + 1) + j + 1]) i++;
        else j++;
    }
    if (matched != NULL && mergedLines != NULL)
        for (j = 0; j < m; j++) {
            if (matched[j]) continue;
            for (k = 0; k < mergedCount; k++)
                if (strcmp(newLines[j], mergedLines[k]) == 0) {
                    newAuthor[j] = mergedAuthor;
                    break;
                }
        }
    free(matched);
    free(dp);
    freeBlame(fb);
    fb->lines = newLines;
    fb->author = newAuthor;
    fb->count = m;
}

static int buildBlame(FileBlame *files) {
    ManifestEntry entries[MAX_FILES];
    Branch *b = activeBranchPtr();
    Commit *c;
    int *chain, length = 0, id, k, i, j, count, fileCount = 0, author, m, mergedCount, mergedAuthor;
    char **lines, **mergedLines;
    Commit *mp;

    blameAuthorCount = 0;
    if (b == NULL || b->headId == 0) return -1;
    for (id = b->headId; id != 0 && (c = findCommit(id)) != NULL; id = c->parentId) length++;
    chain = malloc(sizeof(int) * (length > 0 ? length : 1));
    if (chain == NULL) return -1;
    k = length;
    for (id = b->headId; id != 0 && k > 0 && (c = findCommit(id)) != NULL; id = c->parentId) chain[--k] = id;

    for (k = 0; k < length; k++) {
        c = findCommit(chain[k]);
        count = loadManifest(chain[k], entries, MAX_FILES);
        if (c == NULL || count < 0) continue;
        author = authorIndex(c->author);
        for (i = 0; i < fileCount; i++) {
            if (findInManifest(entries, count, files[i].name) < 0) {
                freeBlame(&files[i]);
                files[i] = files[fileCount - 1];
                fileCount--;
                i--;
            }
        }
        for (j = 0; j < count; j++) {
            for (i = 0; i < fileCount; i++)
                if (strcmp(files[i].name, entries[j].name) == 0) break;
            if (i == fileCount) {
                if (fileCount == MAX_FILES) continue;
                strcpy(files[i].name, entries[j].name);
                files[i].count = 0;
                files[i].lines = NULL;
                files[i].author = NULL;
                fileCount++;
            }
            m = loadSnapshotLines(chain[k], entries[j].name, &lines);
            if (lines == NULL) {
                lines = malloc(sizeof(char *));
                if (lines == NULL) continue;
                m = 0;
            }
            mergedLines = NULL;
            mergedCount = 0;
            mergedAuthor = author;
            mp = c->mergeParentId ? findCommit(c->mergeParentId) : NULL;
            if (mp != NULL) {
                mergedCount = loadSnapshotLines(mp->id, entries[j].name, &mergedLines);
                mergedAuthor = authorIndex(mp->author);
            }
            updateBlame(&files[i], lines, m, author, mergedLines, mergedCount, mergedAuthor);
            if (mergedLines != NULL) {
                for (mergedCount--; mergedCount >= 0; mergedCount--) free(mergedLines[mergedCount]);
                free(mergedLines);
            }
        }
    }
    free(chain);
    return fileCount;
}

void showOwnershipReport(void) {
    FileBlame files[MAX_FILES];
    int fileCount, i, a, top, perFile[MAX_AUTHORS], total[MAX_AUTHORS], order[MAX_AUTHORS];
    int allLines = 0, covered, busFactor, t, risky = 0, j;

    if (!requireActiveRepo()) return;
    fileCount = buildBlame(files);
    if (fileCount < 0) {
        printf("No commits on branch '%s' yet. Commit some files first.\n", activeBranch);
        return;
    }
    for (a = 0; a < MAX_AUTHORS; a++) total[a] = 0;

    printf("\n===== Code Ownership on branch '%s' =====\n", activeBranch);
    printf("Each line is credited to the person who wrote it, found by replaying every commit.\n\n");
    printf("%-20s %-6s %-14s %-7s %s\n", "File", "Lines", "Main owner", "Share", "Knowledge risk");
    printf("--------------------------------------------------------------------------\n");
    for (i = 0; i < fileCount; i++) {
        for (a = 0; a < blameAuthorCount; a++) perFile[a] = 0;
        for (j = 0; j < files[i].count; j++) {
            perFile[files[i].author[j]]++;
            total[files[i].author[j]]++;
        }
        allLines += files[i].count;
        top = 0;
        for (a = 1; a < blameAuthorCount; a++)
            if (perFile[a] > perFile[top]) top = a;
        if (files[i].count == 0) {
            printf("%-20s %-6d %-14s %-7s %s\n", files[i].name, 0, "-", "-", "empty file");
            continue;
        }
        t = perFile[top] * 100 / files[i].count;
        printf("%-20s %-6d %-14s %5d%%  %s\n", files[i].name, files[i].count, blameAuthors[top], t,
               t >= 80 ? "HIGH (only one person knows this file)" : t >= 60 ? "MEDIUM" : "LOW (shared knowledge)");
        if (t >= 80) risky++;
    }

    printf("\nLines written per person (current code only):\n");
    for (a = 0; a < blameAuthorCount; a++) order[a] = a;
    for (a = 0; a < blameAuthorCount - 1; a++)
        for (j = a + 1; j < blameAuthorCount; j++)
            if (total[order[j]] > total[order[a]]) {
                t = order[a];
                order[a] = order[j];
                order[j] = t;
            }
    for (a = 0; a < blameAuthorCount; a++) {
        t = allLines ? total[order[a]] * 100 / allLines : 0;
        printf("  %-15s %4d line(s)  %3d%%  ", blameAuthors[order[a]], total[order[a]], t);
        for (j = 0; j < t / 5; j++) printf("#");
        printf("\n");
    }

    covered = 0;
    busFactor = 0;
    for (a = 0; a < blameAuthorCount && covered * 2 <= allLines; a++) {
        covered += total[order[a]];
        busFactor++;
    }
    printf("\nBUS FACTOR: %d\n", busFactor);
    if (allLines == 0) printf("There is no code yet.\n");
    else if (busFactor <= 1)
        printf("If %s is unavailable, more than half of the code has no one else who wrote it. Share the work!\n",
               blameAuthors[order[0]]);
    else printf("At least %d people would need to be unavailable before half of the code loses its authors.\n", busFactor);
    printf("Files known by only one person: %d of %d\n", risky, fileCount);
    for (i = 0; i < fileCount; i++) freeBlame(&files[i]);
}

void showLineAuthors(void) {
    FileBlame files[MAX_FILES];
    char name[NAME_LEN];
    int fileCount, i, j;

    if (!requireActiveRepo()) return;
    fileCount = buildBlame(files);
    if (fileCount < 0) {
        printf("No commits on branch '%s' yet. Commit some files first.\n", activeBranch);
        return;
    }
    printf("Files in the last commit of '%s': ", activeBranch);
    for (i = 0; i < fileCount; i++) printf("%s%s", i ? ", " : "", files[i].name);
    printf("\nFile name: ");
    readLine(name, sizeof(name));
    for (i = 0; i < fileCount; i++)
        if (strcmp(files[i].name, name) == 0) break;
    if (i == fileCount) printf("'%s' is not in the last commit of this branch.\n", name);
    else {
        printf("\n----- Who wrote each line of %s -----\n", name);
        for (j = 0; j < files[i].count; j++)
            printf("%3d | %-12s | %s\n", j + 1, blameAuthors[files[i].author[j]], files[i].lines[j]);
        if (files[i].count == 0) printf("(empty file)\n");
    }
    for (i = 0; i < fileCount; i++) freeBlame(&files[i]);
}
