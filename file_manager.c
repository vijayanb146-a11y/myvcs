#include "common.h"

TrackedFile *findTrackedFile(int repoId, const char *name) {
    TrackedFile *f;
    for (f = fileList; f != NULL; f = f->next)
        if (f->repoId == repoId && strcmp(f->name, name) == 0) return f;
    return NULL;
}

int trackFile(int repoId, const char *name) {
    TrackedFile *f, *tail;
    if (findTrackedFile(repoId, name) != NULL) return 1;
    f = malloc(sizeof(TrackedFile));
    if (f == NULL) return 0;
    f->repoId = repoId;
    strcpy(f->name, name);
    f->next = NULL;
    if (fileList == NULL) fileList = f;
    else {
        for (tail = fileList; tail->next != NULL; tail = tail->next);
        tail->next = f;
    }
    return 1;
}

void untrackFile(int repoId, const char *name) {
    TrackedFile **fp = &fileList, *f;
    while (*fp != NULL) {
        f = *fp;
        if (f->repoId == repoId && strcmp(f->name, name) == 0) {
            *fp = f->next;
            free(f);
            return;
        }
        fp = &f->next;
    }
}

int countTrackedFiles(int repoId) {
    TrackedFile *f;
    int count = 0;
    for (f = fileList; f != NULL; f = f->next)
        if (f->repoId == repoId) count++;
    return count;
}

void printFileWithNumbers(const char *path) {
    FILE *fp = fopen(path, "r");
    char line[LINE_LEN];
    int no = 1;
    if (fp == NULL) {
        printf("(file could not be opened)\n");
        return;
    }
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        printf("%3d | %s\n", no++, line);
    }
    if (no == 1) printf("(empty file)\n");
    fclose(fp);
}

static int readContent(FILE *out) {
    char line[LINE_LEN];
    int count = 0;
    printf("Type the content line by line. Enter a single '.' on its own line to finish.\n");
    while (1) {
        readLine(line, sizeof(line));
        if (strcmp(line, ".") == 0) break;
        if (feof(stdin) && line[0] == '\0') break;
        fprintf(out, "%s\n", line);
        count++;
    }
    return count;
}

static int askFileName(char *name, int mustExist) {
    printf("File name (e.g. main.c): ");
    readLine(name, NAME_LEN);
    if (!isValidFileName(name)) {
        printf("Invalid file name.\n");
        return 0;
    }
    if (mustExist && findTrackedFile(activeRepo, name) == NULL) {
        printf("File '%s' is not in this repository.\n", name);
        return 0;
    }
    return 1;
}

typedef struct EditNode {
    int id;
    int repoId;
    int hadBefore;
    int hasAfter;
    char file[NAME_LEN];
    char action[16];
    struct EditNode *prev;
    struct EditNode *next;
} EditNode;

static EditNode *editHead = NULL;
static EditNode *editTail = NULL;
static EditNode *editCurrent = NULL;
static int editCounter = 0;

static void editStatePath(int id, const char *which, char *out) {
    snprintf(out, PATH_LEN, "%s/edit_%d_%s", UNDO_DIR, id, which);
}

static void freeEditNode(EditNode *n) {
    char path[PATH_LEN];
    editStatePath(n->id, "before", path);
    remove(path);
    editStatePath(n->id, "after", path);
    remove(path);
    free(n);
}

static void dropRedoNodes(void) {
    EditNode *n = editCurrent != NULL ? editCurrent->next : editHead, *next;
    while (n != NULL) {
        next = n->next;
        freeEditNode(n);
        n = next;
    }
    if (editCurrent != NULL) editCurrent->next = NULL;
    else editHead = NULL;
    editTail = editCurrent;
}

void clearEditHistory(void) {
    editCurrent = NULL;
    dropRedoNodes();
}

static EditNode *beginEdit(const char *file, const char *action) {
    EditNode *n = malloc(sizeof(EditNode));
    char work[PATH_LEN], state[PATH_LEN];
    if (n == NULL) return NULL;
    n->id = ++editCounter;
    n->repoId = activeRepo;
    strcpy(n->file, file);
    strcpy(n->action, action);
    n->prev = n->next = NULL;
    workingPath(activeRepo, file, work);
    editStatePath(n->id, "before", state);
    n->hadBefore = fileExists(work) && copyFile(work, state);
    n->hasAfter = 0;
    return n;
}

static void finishEdit(EditNode *n) {
    char work[PATH_LEN], state[PATH_LEN];
    EditNode *oldest;
    int count = 0;
    if (n == NULL) return;
    workingPath(activeRepo, n->file, work);
    editStatePath(n->id, "after", state);
    n->hasAfter = fileExists(work) && copyFile(work, state);
    dropRedoNodes();
    n->prev = editTail;
    if (editTail != NULL) editTail->next = n;
    else editHead = n;
    editTail = n;
    editCurrent = n;
    for (oldest = editHead; oldest != NULL; oldest = oldest->next) count++;
    if (count > MAX_UNDO) {
        oldest = editHead;
        editHead = oldest->next;
        editHead->prev = NULL;
        freeEditNode(oldest);
    }
}

static void applyEditState(EditNode *n, int useAfter) {
    char work[PATH_LEN], state[PATH_LEN];
    int exists = useAfter ? n->hasAfter : n->hadBefore;
    workingPath(n->repoId, n->file, work);
    if (exists) {
        editStatePath(n->id, useAfter ? "after" : "before", state);
        copyFile(state, work);
        trackFile(n->repoId, n->file);
    } else {
        remove(work);
        untrackFile(n->repoId, n->file);
    }
    saveFiles();
}

void undoEdit(void) {
    EditNode *n;
    if (!requireActiveRepo()) return;
    n = editCurrent;
    if (n == NULL || n->repoId != activeRepo) {
        printf("Nothing to undo. (Undo works on edits made since the last commit.)\n");
        return;
    }
    applyEditState(n, 0);
    editCurrent = n->prev;
    printf("Undone: %s %s\n", n->action, n->file);
    logActivity("Undid %s of %s in %s", n->action, n->file, activeRepoName());
}

void redoEdit(void) {
    EditNode *n;
    if (!requireActiveRepo()) return;
    n = editCurrent != NULL ? editCurrent->next : editHead;
    if (n == NULL || n->repoId != activeRepo) {
        printf("Nothing to redo.\n");
        return;
    }
    applyEditState(n, 1);
    editCurrent = n;
    printf("Redone: %s %s\n", n->action, n->file);
    logActivity("Redid %s of %s in %s", n->action, n->file, activeRepoName());
}

void showEditHistory(void) {
    EditNode *n;
    int step = 1;
    if (!requireActiveRepo()) return;
    printf("\nEdit history since the last commit (oldest first):\n");
    if (editHead == NULL) {
        printf("  (no edits yet)\n");
        return;
    }
    printf("  %s start\n", editCurrent == NULL ? "->" : "  ");
    for (n = editHead; n != NULL; n = n->next)
        printf("  %s %2d. %-7s %s\n", n == editCurrent ? "->" : "  ", step++, n->action, n->file);
    printf("'->' marks where you are. Undo moves up the list, Redo moves down.\n");
}

void createFile(void) {
    char name[NAME_LEN], path[PATH_LEN];
    EditNode *edit;
    FILE *fp;
    int lines;

    if (!requireActiveRepo()) return;
    if (countTrackedFiles(activeRepo) >= MAX_FILES) {
        printf("A repository can hold at most %d files.\n", MAX_FILES);
        return;
    }
    if (!askFileName(name, 0)) return;
    if (findTrackedFile(activeRepo, name) != NULL) {
        printf("File '%s' already exists. Use Edit File instead.\n", name);
        return;
    }
    workingPath(activeRepo, name, path);
    edit = beginEdit(name, "create");
    fp = fopen(path, "w");
    if (fp == NULL) {
        printf("Could not create the file.\n");
        free(edit);
        return;
    }
    lines = readContent(fp);
    fclose(fp);
    trackFile(activeRepo, name);
    saveFiles();
    finishEdit(edit);
    logActivity("Created %s in %s", name, activeRepoName());
    printf("Created '%s' with %d line(s). Commit to save this version.\n", name, lines);
}

void editFile(void) {
    char name[NAME_LEN], path[PATH_LEN];
    EditNode *edit;
    FILE *fp;
    int choice, lines;

    if (!requireActiveRepo()) return;
    listFiles();
    if (countTrackedFiles(activeRepo) == 0) return;
    if (!askFileName(name, 1)) return;
    workingPath(activeRepo, name, path);
    printf("\nCurrent content of %s:\n", name);
    printFileWithNumbers(path);
    printf("\n1. Replace the whole content\n2. Append new lines at the end\nChoice: ");
    choice = readInt();
    if (choice != 1 && choice != 2) {
        printf("Invalid choice.\n");
        return;
    }
    edit = beginEdit(name, "edit");
    fp = fopen(path, choice == 1 ? "w" : "a");
    if (fp == NULL) {
        printf("Could not open the file.\n");
        free(edit);
        return;
    }
    lines = readContent(fp);
    fclose(fp);
    finishEdit(edit);
    logActivity("Edited %s in %s", name, activeRepoName());
    printf("'%s' updated (%d line(s) %s). Commit to save this version.\n", name, lines,
           choice == 1 ? "written" : "appended");
}

void viewFile(void) {
    char name[NAME_LEN], path[PATH_LEN];
    if (!requireActiveRepo()) return;
    listFiles();
    if (countTrackedFiles(activeRepo) == 0) return;
    if (!askFileName(name, 1)) return;
    workingPath(activeRepo, name, path);
    printf("\n----- %s -----\n", name);
    printFileWithNumbers(path);
}

void listFiles(void) {
    TrackedFile *f;
    char path[PATH_LEN];
    int count = 0;
    if (!requireActiveRepo()) return;
    printf("\nFiles in '%s':\n", activeRepoName());
    for (f = fileList; f != NULL; f = f->next) {
        if (f->repoId != activeRepo) continue;
        workingPath(activeRepo, f->name, path);
        printf("  %-25s %d line(s)\n", f->name, countLines(path));
        count++;
    }
    if (count == 0) printf("  (no files yet)\n");
}

void deleteFile(void) {
    char name[NAME_LEN], path[PATH_LEN];
    EditNode *edit;
    if (!requireActiveRepo()) return;
    listFiles();
    if (countTrackedFiles(activeRepo) == 0) return;
    if (!askFileName(name, 1)) return;
    if (!askYesNo("Delete this file from the working directory?")) {
        printf("Delete cancelled.\n");
        return;
    }
    workingPath(activeRepo, name, path);
    edit = beginEdit(name, "delete");
    remove(path);
    untrackFile(activeRepo, name);
    saveFiles();
    finishEdit(edit);
    logActivity("Deleted %s in %s", name, activeRepoName());
    printf("'%s' deleted. Commit to record the deletion.\n", name);
}
