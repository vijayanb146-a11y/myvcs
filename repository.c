#include "common.h"

Repository *findRepoById(int id) {
    Repository *r;
    for (r = repoList; r != NULL; r = r->next)
        if (r->id == id) return r;
    return NULL;
}

Repository *findRepoByName(const char *name) {
    Repository *r;
    for (r = repoList; r != NULL; r = r->next)
        if (strcmp(r->name, name) == 0) return r;
    return NULL;
}

int requireActiveRepo(void) {
    if (activeRepo == 0 || findRepoById(activeRepo) == NULL) {
        printf("Select a repository first (Repository Management -> Select Repository).\n");
        return 0;
    }
    return 1;
}

const char *activeRepoName(void) {
    Repository *r = findRepoById(activeRepo);
    return r == NULL ? "none" : r->name;
}

static int countRepoCommits(int repoId) {
    Commit *c;
    int count = 0;
    for (c = commitList; c != NULL; c = c->next)
        if (c->repoId == repoId) count++;
    return count;
}

void createRepository(void) {
    char name[NAME_LEN], dir[PATH_LEN];
    Repository *r, *tail;
    int id = 1;

    printf("Repository name: ");
    readLine(name, sizeof(name));
    if (!isValidName(name)) {
        printf("Invalid name. Use 1 to 63 characters without '|'.\n");
        return;
    }
    if (findRepoByName(name) != NULL) {
        printf("A repository named '%s' already exists.\n", name);
        return;
    }
    for (r = repoList; r != NULL; r = r->next)
        if (r->id >= id) id = r->id + 1;

    r = malloc(sizeof(Repository));
    if (r == NULL) {
        printf("Out of memory.\n");
        return;
    }
    r->id = id;
    strcpy(r->name, name);
    strcpy(r->owner, currentUser);
    currentTime(r->created);
    r->next = NULL;
    if (repoList == NULL) repoList = r;
    else {
        for (tail = repoList; tail->next != NULL; tail = tail->next);
        tail->next = r;
    }

    workingDir(id, dir);
    MAKE_DIR(dir);
    addBranch(id, "main", 0);
    saveRepositories();
    saveBranches();

    clearEditHistory();
    activeRepo = id;
    strcpy(activeBranch, "main");
    pendingMerge = 0;
    saveState();
    logActivity("Created repository %s", name);
    printf("Repository '%s' created with ID %d and selected (branch: main).\n", name, id);
}

void listRepositories(void) {
    Repository *r;
    if (repoList == NULL) {
        printf("No repositories yet. Create one first.\n");
        return;
    }
    printf("\n%-4s %-22s %-12s %-17s %-6s %-7s\n", "ID", "Name", "Owner", "Created", "Files", "Commits");
    printf("-------------------------------------------------------------------------\n");
    for (r = repoList; r != NULL; r = r->next)
        printf("%-4d %-22s %-12s %-17s %-6d %-7d%s\n", r->id, r->name, r->owner, r->created,
               countTrackedFiles(r->id), countRepoCommits(r->id), r->id == activeRepo ? "  <- active" : "");
}

void selectRepository(void) {
    Repository *r;
    Branch *b;
    int id;

    listRepositories();
    if (repoList == NULL) return;
    printf("Enter repository ID: ");
    id = readInt();
    r = findRepoById(id);
    if (r == NULL) {
        printf("Repository not found.\n");
        return;
    }
    if (activeRepo != id) clearEditHistory();
    activeRepo = id;
    pendingMerge = 0;
    b = findBranch(id, "main");
    if (b == NULL)
        for (b = branchList; b != NULL && b->repoId != id; b = b->next);
    strcpy(activeBranch, b != NULL ? b->name : "main");
    saveState();
    printf("Active repository: %s | Branch: %s\n", r->name, activeBranch);
}

static void removeSnapshot(int commitId) {
    ManifestEntry entries[MAX_FILES];
    char path[PATH_LEN];
    int count, i;
    count = loadManifest(commitId, entries, MAX_FILES);
    for (i = 0; i < count; i++) {
        snapshotPath(commitId, entries[i].name, path);
        remove(path);
    }
    snapshotPath(commitId, MANIFEST_NAME, path);
    remove(path);
    snapshotDir(commitId, path);
    REMOVE_DIR(path);
}

void deleteRepository(void) {
    char confirm[NAME_LEN], path[PATH_LEN];
    Repository *r, **rp;
    TrackedFile **fp, *f;
    Commit **cp, *c;
    Branch **bp, *b;
    int id;

    listRepositories();
    if (repoList == NULL) return;
    printf("Enter repository ID to delete: ");
    id = readInt();
    r = findRepoById(id);
    if (r == NULL) {
        printf("Repository not found.\n");
        return;
    }
    printf("This permanently deletes '%s', its files, commits and branches.\n", r->name);
    printf("Type the repository name to confirm: ");
    readLine(confirm, sizeof(confirm));
    if (strcmp(confirm, r->name) != 0) {
        printf("Name did not match. Delete cancelled.\n");
        return;
    }

    fp = &fileList;
    while (*fp != NULL) {
        f = *fp;
        if (f->repoId == id) {
            workingPath(id, f->name, path);
            remove(path);
            *fp = f->next;
            free(f);
        } else fp = &f->next;
    }
    workingDir(id, path);
    REMOVE_DIR(path);

    cp = &commitList;
    while (*cp != NULL) {
        c = *cp;
        if (c->repoId == id) {
            removeSnapshot(c->id);
            *cp = c->next;
            free(c);
        } else cp = &c->next;
    }

    bp = &branchList;
    while (*bp != NULL) {
        b = *bp;
        if (b->repoId == id) {
            *bp = b->next;
            free(b);
        } else bp = &b->next;
    }

    for (rp = &repoList; *rp != NULL; rp = &(*rp)->next) {
        if (*rp == r) {
            *rp = r->next;
            break;
        }
    }
    printf("Repository '%s' deleted.\n", r->name);
    logActivity("Deleted repository %s", r->name);
    free(r);

    if (activeRepo == id) {
        clearEditHistory();
        activeRepo = 0;
        strcpy(activeBranch, "main");
        pendingMerge = 0;
    }
    saveRepositories();
    saveFiles();
    saveCommits();
    saveBranches();
    saveState();
}
