#include "common.h"

typedef struct {
    int count;
    char names[MAX_FILES][NAME_LEN];
    unsigned char includes[MAX_FILES][MAX_FILES];
} DependencyGraph;

static DependencyGraph graph;

static int nodeIndex(const char *name) {
    int i;
    for (i = 0; i < graph.count; i++)
        if (strcmp(graph.names[i], name) == 0) return i;
    return -1;
}

static const char *baseName(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) slash = back;
    return slash == NULL ? path : slash + 1;
}

static void filePathFor(int commitId, const char *name, char *out) {
    if (commitId == 0) workingPath(activeRepo, name, out);
    else snapshotPath(commitId, name, out);
}

static void scanIncludes(int commitId, int from) {
    char path[PATH_LEN], line[LINE_LEN], target[NAME_LEN];
    char *p, *start, *end;
    FILE *fp;
    int to;
    size_t len;

    filePathFor(commitId, graph.names[from], path);
    fp = fopen(path, "r");
    if (fp == NULL) return;
    while (fgets(line, sizeof(line), fp)) {
        p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '#') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, "include", 7) != 0) continue;
        start = strchr(p, '"');
        if (start == NULL) continue;
        end = strchr(start + 1, '"');
        if (end == NULL) continue;
        len = (size_t)(end - start - 1);
        if (len == 0 || len >= NAME_LEN) continue;
        strncpy(target, start + 1, len);
        target[len] = '\0';
        to = nodeIndex(baseName(target));
        if (to >= 0 && to != from) graph.includes[from][to] = 1;
    }
    fclose(fp);
}

static int buildGraph(int commitId) {
    ManifestEntry entries[MAX_FILES];
    TrackedFile *f;
    int i, count;

    memset(&graph, 0, sizeof(graph));
    if (commitId == 0) {
        for (f = fileList; f != NULL && graph.count < MAX_FILES; f = f->next)
            if (f->repoId == activeRepo) strcpy(graph.names[graph.count++], f->name);
    } else {
        count = loadManifest(commitId, entries, MAX_FILES);
        if (count < 0) return 0;
        for (i = 0; i < count; i++) strcpy(graph.names[graph.count++], entries[i].name);
    }
    for (i = 0; i < graph.count; i++) scanIncludes(commitId, i);
    return 1;
}

static int findImpact(int start, int *level) {
    int queue[MAX_FILES], front = 0, rear = 0, i, u, reached = 0;
    for (i = 0; i < graph.count; i++) level[i] = -1;
    level[start] = 0;
    queue[rear++] = start;
    while (front < rear) {
        u = queue[front++];
        for (i = 0; i < graph.count; i++) {
            if (graph.includes[i][u] && level[i] == -1) {
                level[i] = level[u] + 1;
                queue[rear++] = i;
                reached++;
            }
        }
    }
    return reached;
}

static int isSourceFile(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot != NULL && (strcmp(dot, ".c") == 0 || strcmp(dot, ".cpp") == 0);
}

void showDependencyGraph(void) {
    int i, j, any, edges = 0;
    if (!requireActiveRepo()) return;
    buildGraph(0);
    if (graph.count == 0) {
        printf("No files in this repository yet.\n");
        return;
    }
    printf("\n===== Dependency Graph of '%s' =====\n", activeRepoName());
    for (i = 0; i < graph.count; i++) {
        printf("\n%s\n  includes : ", graph.names[i]);
        any = 0;
        for (j = 0; j < graph.count; j++)
            if (graph.includes[i][j]) {
                printf("%s%s", any ? ", " : "", graph.names[j]);
                any = 1;
                edges++;
            }
        if (!any) printf("-");
        printf("\n  used by  : ");
        any = 0;
        for (j = 0; j < graph.count; j++)
            if (graph.includes[j][i]) {
                printf("%s%s", any ? ", " : "", graph.names[j]);
                any = 1;
            }
        if (!any) printf("-");
        printf("\n");
    }
    printf("\n%d file(s), %d dependency link(s).\n", graph.count, edges);
}

void analyzeFileImpact(void) {
    char name[NAME_LEN];
    int level[MAX_FILES], start, reached, i, d, maxLevel = 0, rebuild = 0;

    if (!requireActiveRepo()) return;
    buildGraph(0);
    if (graph.count == 0) {
        printf("No files in this repository yet.\n");
        return;
    }
    printf("Files: ");
    for (i = 0; i < graph.count; i++) printf("%s%s", i ? ", " : "", graph.names[i]);
    printf("\nWhich file are you planning to change? ");
    readLine(name, sizeof(name));
    start = nodeIndex(name);
    if (start < 0) {
        printf("'%s' is not in this repository.\n", name);
        return;
    }
    reached = findImpact(start, level);
    printf("\n===== Change Impact Report for %s =====\n", name);
    if (reached == 0) {
        printf("No other file depends on %s. The change is isolated (LOW risk).\n", name);
        return;
    }
    for (i = 0; i < graph.count; i++)
        if (level[i] > maxLevel) maxLevel = level[i];
    for (d = 1; d <= maxLevel; d++) {
        printf("%s:\n", d == 1 ? "Directly affected (includes it)" : "Indirectly affected");
        for (i = 0; i < graph.count; i++)
            if (level[i] == d) printf("  [level %d] %s\n", d, graph.names[i]);
    }
    printf("\nSource files to rebuild and retest: ");
    for (i = 0; i < graph.count; i++)
        if (level[i] >= 0 && isSourceFile(graph.names[i])) {
            printf("%s%s", rebuild ? ", " : "", graph.names[i]);
            rebuild++;
        }
    if (!rebuild) printf("-");
    d = reached * 100 / (graph.count - 1);
    printf("\nRisk level: %s (%d of %d other files affected)\n",
           d >= 60 ? "HIGH" : d >= 30 ? "MEDIUM" : "LOW", reached, graph.count - 1);
}

void analyzeCommitImpact(void) {
    ManifestEntry now[MAX_FILES], before[MAX_FILES];
    Commit *c;
    char changed[MAX_FILES][NAME_LEN];
    int id, nNow, nBefore = 0, i, j, k, nChanged = 0, level[MAX_FILES], hit[MAX_FILES], total = 0;

    if (!requireActiveRepo()) return;
    showRepositoryLog();
    printf("Commit ID to analyze: ");
    id = readInt();
    c = findRepoCommit(activeRepo, id);
    if (c == NULL) {
        printf("Commit %d does not belong to this repository.\n", id);
        return;
    }
    nNow = loadManifest(id, now, MAX_FILES);
    if (c->parentId) nBefore = loadManifest(c->parentId, before, MAX_FILES);
    if (nNow < 0 || nBefore < 0) {
        printf("A snapshot is missing. Run the integrity check.\n");
        return;
    }
    for (i = 0; i < nNow; i++) {
        j = findInManifest(before, nBefore, now[i].name);
        if (j < 0 || before[j].checksum != now[i].checksum) strcpy(changed[nChanged++], now[i].name);
    }
    printf("\n===== Impact of commit %d: \"%s\" by %s =====\n", id, c->message, c->author);
    printf("Files changed in this commit:\n");
    if (nChanged == 0) printf("  (no file content changed)\n");
    for (i = 0; i < nChanged; i++) printf("  * %s\n", changed[i]);
    for (i = 0; i < nBefore; i++)
        if (findInManifest(now, nNow, before[i].name) < 0) printf("  * %s (deleted)\n", before[i].name);

    buildGraph(id);
    for (i = 0; i < graph.count; i++) hit[i] = 0;
    for (k = 0; k < nChanged; k++) {
        j = nodeIndex(changed[k]);
        if (j < 0) continue;
        findImpact(j, level);
        for (i = 0; i < graph.count; i++)
            if (level[i] > 0 && !hit[i]) hit[i] = 1;
    }
    for (k = 0; k < nChanged; k++) {
        j = nodeIndex(changed[k]);
        if (j >= 0) hit[j] = 0;
    }
    printf("\nOther files that may break because of these changes:\n");
    for (i = 0; i < graph.count; i++)
        if (hit[i]) {
            printf("  -> %s\n", graph.names[i]);
            total++;
        }
    if (total == 0) printf("  (none, the change is isolated)\n");
    k = graph.count > nChanged ? total * 100 / (graph.count - nChanged) : 0;
    printf("Risk level: %s (%d of %d unchanged files affected)\n", k >= 60 ? "HIGH" : k >= 30 ? "MEDIUM" : "LOW",
           total, graph.count > nChanged ? graph.count - nChanged : 0);
}

static int cycleFound;

static void dfsCycle(int u, int *color, int *stack, int depth) {
    int v, i, k;
    color[u] = 1;
    stack[depth] = u;
    for (v = 0; v < graph.count; v++) {
        if (!graph.includes[u][v]) continue;
        if (color[v] == 1) {
            cycleFound++;
            printf("  Cycle %d: ", cycleFound);
            for (k = 0; k <= depth; k++)
                if (stack[k] == v) break;
            for (i = k; i <= depth; i++) printf("%s -> ", graph.names[stack[i]]);
            printf("%s\n", graph.names[v]);
        } else if (color[v] == 0) {
            dfsCycle(v, color, stack, depth + 1);
        }
    }
    color[u] = 2;
}

void detectCircularDependencies(void) {
    int color[MAX_FILES], stack[MAX_FILES], i;
    if (!requireActiveRepo()) return;
    buildGraph(0);
    for (i = 0; i < graph.count; i++) color[i] = 0;
    cycleFound = 0;
    printf("\nChecking for circular #include chains...\n");
    for (i = 0; i < graph.count; i++)
        if (color[i] == 0) dfsCycle(i, color, stack, 0);
    if (cycleFound == 0) printf("No circular dependencies found.\n");
    else printf("%d circular dependency chain(s) found. Use include guards or restructure these files.\n", cycleFound);
}

static void siftDown(int *order, int *score, int n, int i) {
    int smallest = i, left = 2 * i + 1, right = 2 * i + 2, t;
    if (left < n && score[order[left]] < score[order[smallest]]) smallest = left;
    if (right < n && score[order[right]] < score[order[smallest]]) smallest = right;
    if (smallest != i) {
        t = order[i];
        order[i] = order[smallest];
        order[smallest] = t;
        siftDown(order, score, n, smallest);
    }
}

static void heapSortByScore(int *order, int *score, int n) {
    int i, t;
    for (i = n / 2 - 1; i >= 0; i--) siftDown(order, score, n, i);
    for (i = n - 1; i > 0; i--) {
        t = order[0];
        order[0] = order[i];
        order[i] = t;
        siftDown(order, score, i, 0);
    }
}

void showRiskReport(void) {
    ManifestEntry now[MAX_FILES], before[MAX_FILES];
    Commit *c;
    int changes[MAX_FILES], dependents[MAX_FILES], score[MAX_FILES], order[MAX_FILES], level[MAX_FILES];
    int i, j, k, nNow, nBefore;

    if (!requireActiveRepo()) return;
    buildGraph(0);
    if (graph.count == 0) {
        printf("No files in this repository yet.\n");
        return;
    }
    for (i = 0; i < graph.count; i++) changes[i] = 0;
    for (c = commitList; c != NULL; c = c->next) {
        if (c->repoId != activeRepo) continue;
        nNow = loadManifest(c->id, now, MAX_FILES);
        nBefore = c->parentId ? loadManifest(c->parentId, before, MAX_FILES) : 0;
        if (nNow < 0 || nBefore < 0) continue;
        for (j = 0; j < nNow; j++) {
            k = findInManifest(before, nBefore, now[j].name);
            if (k >= 0 && before[k].checksum == now[j].checksum) continue;
            i = nodeIndex(now[j].name);
            if (i >= 0) changes[i]++;
        }
    }
    for (i = 0; i < graph.count; i++) {
        dependents[i] = findImpact(i, level);
        score[i] = changes[i] * (dependents[i] + 1);
        order[i] = i;
    }
    heapSortByScore(order, score, graph.count);
    printf("\n===== Hotspot Risk Report for '%s' =====\n", activeRepoName());
    printf("Risk score = times changed x (files depending on it + 1)\n\n");
    printf("%-5s %-22s %-9s %-11s %-6s %s\n", "Rank", "File", "Changes", "Dependents", "Score", "Risk");
    printf("----------------------------------------------------------------\n");
    for (i = 0; i < graph.count; i++) {
        k = order[i];
        printf("%-5d %-22s %-9d %-11d %-6d %s\n", i + 1, graph.names[k], changes[k], dependents[k], score[k],
               score[k] >= 6 ? "HIGH" : score[k] >= 3 ? "MEDIUM" : "LOW");
    }
    printf("\nFiles at the top change often and many files depend on them. Review and test them first.\n");
}

void showBuildOrder(void) {
    int pending[MAX_FILES], queue[MAX_FILES], front = 0, rear = 0, i, u, done = 0, step = 1;

    if (!requireActiveRepo()) return;
    buildGraph(0);
    if (graph.count == 0) {
        printf("No files in this repository yet.\n");
        return;
    }
    for (i = 0; i < graph.count; i++) {
        pending[i] = 0;
        for (u = 0; u < graph.count; u++)
            if (graph.includes[i][u]) pending[i]++;
        if (pending[i] == 0) queue[rear++] = i;
    }
    printf("\n===== Build Order for '%s' =====\n", activeRepoName());
    printf("Every file appears after all the files it includes.\n\n");
    while (front < rear) {
        u = queue[front++];
        printf("  %2d. %s\n", step++, graph.names[u]);
        done++;
        for (i = 0; i < graph.count; i++) {
            if (!graph.includes[i][u]) continue;
            if (--pending[i] == 0) queue[rear++] = i;
        }
    }
    if (done < graph.count) {
        printf("\nThese files could not be ordered because of circular includes:\n");
        for (i = 0; i < graph.count; i++)
            if (pending[i] > 0) printf("  %s\n", graph.names[i]);
        printf("Use Detect Circular Dependencies to see the cycle.\n");
    } else printf("\nAll %d file(s) ordered. Review or compile them top to bottom.\n", done);
}
