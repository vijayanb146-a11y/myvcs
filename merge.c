#include "common.h"

static int maxCommitId(void) {
    Commit *c;
    int max = 0;
    for (c = commitList; c != NULL; c = c->next)
        if (c->id > max) max = c->id;
    return max;
}

int findCommonAncestor(int commitA, int commitB) {
    int size, *queue, front = 0, rear = 0, id, result = 0;
    char *inA, *seen;
    Commit *c;

    if (commitA == 0 || commitB == 0) return 0;
    size = maxCommitId() + 1;
    inA = calloc(size, 1);
    seen = calloc(size, 1);
    queue = malloc(sizeof(int) * size * 2);
    if (inA == NULL || seen == NULL || queue == NULL) {
        free(inA);
        free(seen);
        free(queue);
        return 0;
    }

    queue[rear++] = commitA;
    inA[commitA] = 1;
    while (front < rear) {
        c = findCommit(queue[front++]);
        if (c == NULL) continue;
        if (c->parentId && !inA[c->parentId]) {
            inA[c->parentId] = 1;
            queue[rear++] = c->parentId;
        }
        if (c->mergeParentId && !inA[c->mergeParentId]) {
            inA[c->mergeParentId] = 1;
            queue[rear++] = c->mergeParentId;
        }
    }

    front = rear = 0;
    queue[rear++] = commitB;
    seen[commitB] = 1;
    while (front < rear) {
        id = queue[front++];
        if (inA[id]) {
            result = id;
            break;
        }
        c = findCommit(id);
        if (c == NULL) continue;
        if (c->parentId && !seen[c->parentId]) {
            seen[c->parentId] = 1;
            queue[rear++] = c->parentId;
        }
        if (c->mergeParentId && !seen[c->mergeParentId]) {
            seen[c->mergeParentId] = 1;
            queue[rear++] = c->mergeParentId;
        }
    }
    free(inA);
    free(seen);
    free(queue);
    return result;
}

static int sameVersion(int presentX, unsigned int sumX, int presentY, unsigned int sumY) {
    if (presentX != presentY) return 0;
    if (!presentX) return 1;
    return sumX == sumY;
}

static void appendFile(FILE *out, int commitId, const char *name) {
    char path[PATH_LEN], line[LINE_LEN];
    FILE *in;
    snapshotPath(commitId, name, path);
    in = fopen(path, "r");
    if (in == NULL) return;
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0';
        fprintf(out, "%s\n", line);
    }
    fclose(in);
}

static void addName(char names[][NAME_LEN], int *count, const char *name) {
    int i;
    for (i = 0; i < *count; i++)
        if (strcmp(names[i], name) == 0) return;
    if (*count < MAX_FILES * 3) strcpy(names[(*count)++], name);
}

static int runMerge(const char *sourceName, int dryRun) {
    ManifestEntry base[MAX_FILES], ours[MAX_FILES], theirs[MAX_FILES];
    char names[MAX_FILES * 3][NAME_LEN], src[PATH_LEN], dst[PATH_LEN], message[MSG_LEN];
    Branch *target = activeBranchPtr(), *source = findBranch(activeRepo, sourceName);
    int baseId, nb = 0, no, nt, total = 0, i, ib, io, it, conflicts = 0, taken = 0, newId;
    FILE *out;

    if (target == NULL || source == NULL) {
        printf("Branch not found.\n");
        return -1;
    }
    if (source->headId == 0) {
        printf("Branch '%s' has no commits. Nothing to merge.\n", sourceName);
        return 0;
    }
    baseId = findCommonAncestor(target->headId, source->headId);
    if (baseId == source->headId) {
        printf("'%s' is already up to date with '%s'.\n", activeBranch, sourceName);
        return 0;
    }
    if (target->headId == 0 || baseId == target->headId) {
        if (dryRun) {
            printf("Fast-forward possible: '%s' will move to commit %d. No conflicts.\n", activeBranch, source->headId);
            return 0;
        }
        target->headId = source->headId;
        checkoutCommit(source->headId);
        saveBranches();
        logActivity("Fast-forward merged %s into %s", sourceName, activeBranch);
        printf("Fast-forward merge: '%s' now points to commit %d.\n", activeBranch, source->headId);
        return 0;
    }

    if (baseId) nb = loadManifest(baseId, base, MAX_FILES);
    no = loadManifest(target->headId, ours, MAX_FILES);
    nt = loadManifest(source->headId, theirs, MAX_FILES);
    if (nb < 0 || no < 0 || nt < 0) {
        printf("A snapshot is missing. Run the integrity check.\n");
        return -1;
    }
    for (i = 0; i < nb; i++) addName(names, &total, base[i].name);
    for (i = 0; i < no; i++) addName(names, &total, ours[i].name);
    for (i = 0; i < nt; i++) addName(names, &total, theirs[i].name);

    printf("\nCommon ancestor: commit %d | %s: commit %d | %s: commit %d\n", baseId,
           activeBranch, target->headId, sourceName, source->headId);
    for (i = 0; i < total; i++) {
        ib = findInManifest(base, nb, names[i]);
        io = findInManifest(ours, no, names[i]);
        it = findInManifest(theirs, nt, names[i]);
        if (sameVersion(io >= 0, io >= 0 ? ours[io].checksum : 0, it >= 0, it >= 0 ? theirs[it].checksum : 0))
            continue;
        if (sameVersion(it >= 0, it >= 0 ? theirs[it].checksum : 0, ib >= 0, ib >= 0 ? base[ib].checksum : 0)) {
            printf("  keep    : %s (changed only in %s)\n", names[i], activeBranch);
            continue;
        }
        if (sameVersion(io >= 0, io >= 0 ? ours[io].checksum : 0, ib >= 0, ib >= 0 ? base[ib].checksum : 0)) {
            printf("  take    : %s (%s in %s)\n", names[i], it >= 0 ? "changed" : "deleted", sourceName);
            taken++;
            if (dryRun) continue;
            workingPath(activeRepo, names[i], dst);
            if (it >= 0) {
                snapshotPath(source->headId, names[i], src);
                copyFile(src, dst);
                trackFile(activeRepo, names[i]);
            } else {
                remove(dst);
                untrackFile(activeRepo, names[i]);
            }
            continue;
        }
        printf("  CONFLICT: %s (changed differently in both branches)\n", names[i]);
        conflicts++;
        if (dryRun) continue;
        workingPath(activeRepo, names[i], dst);
        out = fopen(dst, "w");
        if (out == NULL) continue;
        fprintf(out, "<<<<<<< %s\n", activeBranch);
        if (io >= 0) appendFile(out, target->headId, names[i]);
        fprintf(out, "=======\n");
        if (it >= 0) appendFile(out, source->headId, names[i]);
        fprintf(out, ">>>>>>> %s\n", sourceName);
        fclose(out);
        trackFile(activeRepo, names[i]);
    }

    if (dryRun) {
        if (conflicts) printf("\nPreview: %d conflict(s) will need manual fixing.\n", conflicts);
        else printf("\nPreview: merge is clean. %d file(s) will come from '%s'.\n", taken, sourceName);
        return conflicts;
    }
    saveFiles();
    if (conflicts) {
        pendingMerge = source->headId;
        saveState();
        logActivity("Merge of %s into %s stopped with %d conflict(s)", sourceName, activeBranch, conflicts);
        printf("\n%d conflict(s). Open the files above with Edit File, keep the right lines,\n", conflicts);
        printf("remove the <<<<<<< ======= >>>>>>> markers, then use Commit Changes to finish.\n");
        return conflicts;
    }
    snprintf(message, sizeof(message), "Merge branch '%s' into '%s'", sourceName, activeBranch);
    newId = createCommit(message, source->headId);
    if (newId) printf("\nMerge successful. Created merge commit %d.\n", newId);
    return 0;
}

static int askSourceBranch(char *name) {
    listBranches();
    printf("Branch to merge into '%s': ", activeBranch);
    readLine(name, NAME_LEN);
    if (findBranch(activeRepo, name) == NULL) {
        printf("Branch '%s' not found.\n", name);
        return 0;
    }
    if (strcmp(name, activeBranch) == 0) {
        printf("Choose a different branch from the one you are on.\n");
        return 0;
    }
    return 1;
}

void previewMerge(void) {
    char name[NAME_LEN];
    if (!requireActiveRepo()) return;
    if (!askSourceBranch(name)) return;
    runMerge(name, 1);
}

void mergeBranch(void) {
    char name[NAME_LEN];
    if (!requireActiveRepo()) return;
    if (pendingMerge) {
        printf("A merge is already in progress. Resolve the conflicts and commit first.\n");
        return;
    }
    if (hasUncommittedChanges()) {
        printf("Commit your changes before merging.\n");
        return;
    }
    if (!askSourceBranch(name)) return;
    clearEditHistory();
    runMerge(name, 0);
}
