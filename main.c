#include "common.h"

static void printHeader(const char *title) {
    printf("\n==================================================\n");
    printf("  MINI GITHUB  |  %s\n", title);
    printf("  User: %s | Repo: %s | Branch: %s\n", currentUser, activeRepoName(),
           activeRepo ? activeBranch : "-");
    printf("==================================================\n");
}

static int menuChoice(void) {
    printf("Choice: ");
    return readInt();
}

static void repositoryMenu(void) {
    int choice;
    while (!feof(stdin)) {
        printHeader("Repository Management");
        printf("1. Create Repository\n2. List Repositories\n3. Select Repository\n4. Delete Repository\n0. Back\n");
        choice = menuChoice();
        switch (choice) {
        case 1: createRepository(); break;
        case 2: listRepositories(); break;
        case 3: selectRepository(); break;
        case 4: deleteRepository(); break;
        case 0: return;
        default: printf("Invalid choice.\n");
        }
    }
}

static void fileMenu(void) {
    int choice;
    while (!feof(stdin)) {
        printHeader("File Management");
        printf("1. Create File\n2. Edit File\n3. View File\n4. List Files\n5. Delete File\n");
        printf("6. Undo Last Change\n7. Redo\n8. Edit History\n0. Back\n");
        choice = menuChoice();
        switch (choice) {
        case 1: createFile(); break;
        case 2: editFile(); break;
        case 3: viewFile(); break;
        case 4: listFiles(); break;
        case 5: deleteFile(); break;
        case 6: undoEdit(); break;
        case 7: redoEdit(); break;
        case 8: showEditHistory(); break;
        case 0: return;
        default: printf("Invalid choice.\n");
        }
    }
}

static void commitMenu(void) {
    int choice;
    while (!feof(stdin)) {
        printHeader("Commits and History");
        printf("1. Status (what changed)\n2. View Changes Since Last Commit\n3. Commit Changes\n");
        printf("4. Branch History\n5. Full Repository Log\n6. Compare Two Commits\n");
        printf("7. View a File From an Old Commit\n8. Rollback to a Commit\n9. Search Commits by Keyword\n0. Back\n");
        choice = menuChoice();
        switch (choice) {
        case 1: showStatus(); break;
        case 2: showWorkingChanges(); break;
        case 3: commitChanges(); break;
        case 4: showBranchHistory(); break;
        case 5: showRepositoryLog(); break;
        case 6: compareCommits(); break;
        case 7: viewFileAtCommit(); break;
        case 8: rollbackToCommit(); break;
        case 9: searchCommits(); break;
        case 0: return;
        default: printf("Invalid choice.\n");
        }
    }
}

static void branchMenu(void) {
    int choice;
    while (!feof(stdin)) {
        printHeader("Branching and Merging");
        printf("1. Create Branch\n2. List Branches\n3. Switch Branch\n4. Preview Merge (conflict check)\n");
        printf("5. Merge Branch\n6. Delete Branch\n0. Back\n");
        choice = menuChoice();
        switch (choice) {
        case 1: createBranch(); break;
        case 2: listBranches(); break;
        case 3: switchBranch(); break;
        case 4: previewMerge(); break;
        case 5: mergeBranch(); break;
        case 6: deleteBranch(); break;
        case 0: return;
        default: printf("Invalid choice.\n");
        }
    }
}

static void impactMenu(void) {
    int choice;
    while (!feof(stdin)) {
        printHeader("Change Impact Analyzer");
        printf("1. Show Dependency Graph\n2. Impact of Changing a File\n3. Impact of a Commit\n");
        printf("4. Detect Circular Dependencies\n5. Hotspot Risk Report\n6. Build Order\n0. Back\n");
        choice = menuChoice();
        switch (choice) {
        case 1: showDependencyGraph(); break;
        case 2: analyzeFileImpact(); break;
        case 3: analyzeCommitImpact(); break;
        case 4: detectCircularDependencies(); break;
        case 5: showRiskReport(); break;
        case 6: showBuildOrder(); break;
        case 0: return;
        default: printf("Invalid choice.\n");
        }
    }
}

static void integrityMenu(void) {
    int choice;
    while (!feof(stdin)) {
        printHeader("Integrity and Statistics");
        printf("1. Verify Repository Integrity\n2. Repository Statistics\n0. Back\n");
        choice = menuChoice();
        switch (choice) {
        case 1: verifyIntegrity(); break;
        case 2: showRepositoryStats(); break;
        case 0: return;
        default: printf("Invalid choice.\n");
        }
    }
}

static void teamMenu(void) {
    int choice;
    while (!feof(stdin)) {
        printHeader("Team Insights: Code Ownership and Bus Factor");
        printf("1. Code Ownership and Bus Factor Report\n2. Who Wrote Each Line of a File\n3. Recent Activity Feed\n0. Back\n");
        choice = menuChoice();
        switch (choice) {
        case 1: showOwnershipReport(); break;
        case 2: showLineAuthors(); break;
        case 3: showActivityLog(); break;
        case 0: return;
        default: printf("Invalid choice.\n");
        }
    }
}

static void changeUser(void) {
    char name[NAME_LEN];
    printf("Your name (used as commit author): ");
    readLine(name, sizeof(name));
    if (!isValidName(name)) {
        printf("Invalid name.\n");
        return;
    }
    strcpy(currentUser, name);
    saveState();
    printf("Now working as '%s'.\n", currentUser);
}

int main(void) {
    int choice;
    loadAll();
    printf("\nWelcome to Mini GitHub, a version control system written in C.\n");
    while (!feof(stdin)) {
        printHeader("Main Menu");
        printf("1. Repository Management\n2. File Management\n3. Commits and History\n");
        printf("4. Branching and Merging\n5. Change Impact Analyzer\n6. Integrity and Statistics\n");
        printf("7. Team Insights (Bus Factor)\n8. Change User\n0. Exit\n");
        choice = menuChoice();
        switch (choice) {
        case 1: repositoryMenu(); break;
        case 2: fileMenu(); break;
        case 3: commitMenu(); break;
        case 4: branchMenu(); break;
        case 5: impactMenu(); break;
        case 6: integrityMenu(); break;
        case 7: teamMenu(); break;
        case 8: changeUser(); break;
        case 0:
            clearEditHistory();
            saveState();
            freeAll();
            printf("All data saved. Goodbye!\n");
            return 0;
        default: printf("Invalid choice.\n");
        }
    }
    clearEditHistory();
    saveState();
    freeAll();
    return 0;
}
