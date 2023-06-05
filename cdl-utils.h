#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "cdl-text-utils.h"
#include <signal.h>

struct JaggedCharArray
{
    char **arr;
    int count;
};

struct KeyValuePair
{
    char *key;
    char *value;
    bool hasValue;
};

struct Dictionary
{
    struct KeyValuePair *pairs;
    int count;
};

char *get_home_subpath(char *sub_path);
bool directory_exists(const char *path);
char *dtryget(struct Dictionary dict, char *var, int *outidx);
int dset(struct Dictionary *dict, char *var, char *value);
int dremove(struct Dictionary *dict, char *var);
struct JaggedCharArray splitnstr(char *str, char sep, int len, bool allowempty);
struct JaggedCharArray splitstr(char *str, char sep, bool allowempty);
char *joinarr(struct JaggedCharArray arr, char cep, int count);
int findstr(char *str, char *tok);
int extractint(char *str, int startpos, int *len);
// replaces source[start..len] with target
void replacestr(char *source, char *target, int start, int len);
int readtoend(FILE *f, char *result);
int findc(char *str, char t);