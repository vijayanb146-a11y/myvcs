#include "common.h"

Branch *findBranch(int repoId, const char *name) {
    Branch *b;
    for (b = branchList; b != NULL; b = b->next)
        if (b->repoId == repoId && strcmp(b->name, name) == 0) return b;
    return NULL;
}

Branch *activeBranchPtr(void) {
    return findBranch(activeRepo, activeBranch);
}

int addBranch(int repoId, const char *name, int headId) {
    Branch *b, *tail;
    b = malloc(sizeof(Branch));
    if (b == NULL) return 0;
    b->repoId = repoId;
    b->headId = headId;
    strcpy(b->name, name);
    b->next = NULL;
    if (branchList == NULL) branchList = b;
    else {
        for (tail = branchList; tail->next != NULL; tail = tail->next);
        tail->next = b;
    }
    return 1;
}

void createBranch(void) {
    char name[NAME_LEN];
    Branch *current;

    if (!requireActiveRepo()) return;
    current = activeBranchPtr();
    if (current == NULL) return;
    printf("New branch name: ");
    readLine(name, sizeof(name));
    if (!isValidName(name) || strchr(name, ' ') != NULL) {
        printf("Invalid branch name (no spaces or '|').\n");
        return;
    }
    if (findBranch(activeRepo, name) != NULL) {
        printf("Branch '%s' already exists.\n", name);
        return;
    }
    if (!addBranch(activeRepo, name, current->headId)) {
        printf("Out of memory.\n");
        return;
    }
    saveBranches();
    logActivity("Created branch %s in %s", name, activeRepoName());
    printf("Branch '%s' created from '%s' at commit %d. Use Switch Branch to work on it.\n",
           name, activeBranch, current->headId);
}

void listBranches(void) {
    Branch *b;
    Commit *c;
    if (!requireActiveRepo()) return;
    printf("\nBranches of '%s':\n", activeRepoName());
    for (b = branchList; b != NULL; b = b->next) {
        if (b->repoId != activeRepo) continue;
        c = findCommit(b->headId);
        printf("  %s %-15s head: %-4d %s\n", strcmp(b->name, activeBranch) == 0 ? "*" : " ",
               b->name, b->headId, c != NULL ? c->message : "(no commits yet)");
    }
}

void switchBranch(void) {
    char name[NAME_LEN];
    Branch *target;

    if (!requireActiveRepo()) return;
    listBranches();
    printf("Branch to switch to: ");
    readLine(name, sizeof(name));
    target = findBranch(activeRepo, name);
    if (target == NULL) {
        printf("Branch '%s' not found.\n", name);
        return;
    }
    if (strcmp(name, activeBranch) == 0) {
        printf("Already on '%s'.\n", name);
        return;
    }
    if (pendingMerge) {
        printf("Finish the merge in progress first (resolve conflicts and commit).\n");
        return;
    }
    if (hasUncommittedChanges() && !askYesNo("You have uncommitted changes that will be lost. Switch anyway?")) {
        printf("Switch cancelled. Commit your changes first.\n");
        return;
    }
    strcpy(activeBranch, name);
    if (target->headId != 0) checkoutCommit(target->headId);
    saveState();
    logActivity("Switched to branch %s in %s", name, activeRepoName());
    printf("Switched to branch '%s'. Working files now match commit %d.\n", name, target->headId);
}

void deleteBranch(void) {
    char name[NAME_LEN];
    Branch **bp, *b;

    if (!requireActiveRepo()) return;
    listBranches();
    printf("Branch to delete: ");
    readLine(name, sizeof(name));
    if (strcmp(name, "main") == 0) {
        printf("The main branch cannot be deleted.\n");
        return;
    }
    if (strcmp(name, activeBranch) == 0) {
        printf("You cannot delete the branch you are on. Switch first.\n");
        return;
    }
    for (bp = &branchList; *bp != NULL; bp = &(*bp)->next) {
        b = *bp;
        if (b->repoId == activeRepo && strcmp(b->name, name) == 0) {
            *bp = b->next;
            free(b);
            saveBranches();
            logActivity("Deleted branch %s in %s", name, activeRepoName());
            printf("Branch '%s' deleted. Its commits stay in the repository log.\n", name);
            return;
        }
    }
    printf("Branch '%s' not found.\n", name);
}
