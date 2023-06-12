#include "cdl-utils.h"

bool directory_exists(const char *path) {
    DIR *dir = opendir(path);
    if (dir) {
        closedir(dir);
        return true;
    } else {
        return false;
    }
}

char *get_home_subpath(char *sub_path) {
    const char *homedir = getenv("HOME");
    int homelen = strlen(homedir);
    int sublen = strlen(sub_path);
    char *path = malloc((homelen + sublen + 2) * sizeof(char));

    sprintf(path, "%s/%s", homedir, sub_path);

    return path;
}

struct JaggedCharArray splitnstr(char *str, char sep, int len, bool allowempty) {
    char *s = calloc(len + 1, sizeof(char));
    strncpy(s, str, len);

    struct JaggedCharArray ret = splitstr(s, sep, allowempty);

    free(s);

    return ret;
}

struct JaggedCharArray splitstr(char *str, char sep, bool allowempty) {
    int strLength = strlen(str);
    int count = 0;
    int i;
    int tokenPointer = 0;

    char **ret = (char **) malloc(strLength * sizeof(char *));

    for (i = 0; i < strLength; i++) {
        if (str[i] != sep)
            continue;

        int len = i - tokenPointer;
        if (len == 0) {
            if (allowempty) {
                ret[count] = "";
                count++;
            }

            tokenPointer = i + 1;
            continue;
        }

        size_t size = len * sizeof(char);
        ret[count] = malloc(size + sizeof(char));

        memcpy(ret[count], str + tokenPointer, size);
        ret[count][len] = '\0';

        count++;
        tokenPointer = i + 1;
    }

    if (strLength > tokenPointer) {
        int len = strLength - tokenPointer;
        size_t size = len * sizeof(char);

        ret[count] = malloc(size + sizeof(char));

        memcpy(ret[count], str + tokenPointer, size);
        ret[count][len] = '\0';
        count++;
    }

    size_t size = count * sizeof(char *);
    char **result = malloc(size);
    memcpy(result, ret, size);

    free(ret);

    struct JaggedCharArray jaggedArr = {result, count};
    return jaggedArr;
}

char *joinarr(struct JaggedCharArray arr, char sep, int count) {
    if (count == 0)
        return "";

    int i;
    int retLen = 0;
    for (i = 0; i < count; i++) {
        retLen += strlen(arr.arr[i]) + 1;
    }

    int len;
    char *ret = malloc(retLen * sizeof(char));
    char *pret = ret;
    for (i = 0; i < count - 1; i++) {
        strcpy(pret, arr.arr[i]);
        len = strlen(pret);
        pret[len] = sep;
        pret += len + 1;
    }
    strcpy(pret, arr.arr[count - 1]);
    len = strlen(pret);
    pret[len] = '\0';

    return ret;
}

// returns the index of the first time tok is matched in str, left to right.
int findstr(char *str, char *tok) {
    size_t len = strlen(str);
    size_t toklen = strlen(tok);
    size_t q = 0; // matched chars so far

    for (int i = 0; i < len; i++) {
        if (q > 0 && str[i] != tok[q])
            q = 0; // this works since in the use cases for this function tokens are space separated and
        // don't start with a space, so there is no need for a prefix function.
        if (str[i] == tok[q])
            q++;
        if (q == toklen)
            return i - q + 1;
    }

    return -1;
}

int findc(char *str, char t) {
    if (str == NULL)
        return -1;

    int n = strlen(str);
    for (int i = 0; i < n; i++)
        if (str[i] == t)
            return i;

    return -1;
}

// extracts an integer from str that starts in startpos
int extractint(char *str, int startpos, int *len) {
    size_t maxlen = strlen(str) - startpos;
    char *num = malloc(maxlen * sizeof(char) + sizeof(char));

    int count = 0;

    while (count < maxlen && (isdigit(str[startpos + count]) || str[startpos + count] == '-')) {
        num[count] = str[startpos + count];
        count++;
    }

    num[count] = '\0';

    *len = count;

    return atoi(num);
}

void replacestr(char *source, char *target, int start, int len) {
    size_t srclen = strlen(source);
    size_t tgtlen = strlen(target);

    size_t extralen = srclen - start - len;

    if (extralen == 0) {
        memcpy(source + start, target, tgtlen * sizeof(char));
        source[start + tgtlen] = '\0';

        return;
    }

    char *buffer = malloc(extralen * sizeof(char) + sizeof(char));
    strcpy(buffer, source + start + len);

    strcpy(source + start, target);
    strcpy(source + start + tgtlen, buffer);

    free(buffer);
}

char *readtoend(FILE *f) {
    long file_size;
    char *buffer;

    // Determine the size of the file
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Allocate memory for the buffer
    buffer = calloc(file_size + 1, sizeof(char));

    // Read the entire file into the buffer
    fread(buffer, file_size, 1, f);

    // Add a null terminator to the end of the buffer
    buffer[file_size] = '\0';

    return buffer;
}

char *dtryget(struct Dictionary dict, char *var, int *outidx) {
    for (int i = 0; i < dict.count; i++)
        if (dict.pairs[i].hasValue && strcmp(dict.pairs[i].key, var) == 0) {
            *outidx = i;
            return dict.pairs[i].value;
        }

    *outidx = -1;
    return NULL;
}

int dset(struct Dictionary *dict, char *var, char *value) {
    int idx;
    dtryget(*dict, var, &idx);

    if (idx >= 0) {
        strcpy(dict->pairs[idx].value, value);
        return 0;
    }

    for (int i = 0; i < dict->count; i++) {
        if (!dict->pairs[i].hasValue) {
            dict->pairs[i].hasValue = true;
            dict->pairs[i].key = var;
            dict->pairs[i].value = value;
            return 0;
        }
    }

    return 1;
}

int dremove(struct Dictionary *dict, char *var) {
    int idx;
    dtryget(*dict, var, &idx);

    if (idx >= 0) {
        dict->pairs[idx].hasValue = false;
        return 0;
    }

    return 1;
}