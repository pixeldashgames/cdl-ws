#include "cdl-utils.h"
char help[] = "Members: Leonardo Amaro Rodriguez and Alfredo Montero Lopez\n"
              "\nFeatures:\nbasics\nmulti-pipe\nspaces\nhistory\nchain\nif\nhelp\n"
              "\nBuilt-in commands:\ncd\nexit\nhelp\n" // Falta escribir mas comandos
              "\nTotal: 6.5";
char help_basics[] = "cd <arg>: Changes directory to arg if possible. Makes use of the chdir(char *) function in C.\n" // LEO
                     "\nexit: Uses the exit(i) function in C to exit, clearing up memory and stdio resources.\n"       // LEO
                     /*FIX*/ "\n>,<,>>: These features are not considered built-in operators so we send it to the execvp directly, letting the operative system handler them\n"
                     "\n|: If you type command1 | command2, executes command1 and its output is saved to a file, which is later read and sent as input to command2\n"
                     "For more understanding of pipe feature seek how works multi-pipe whit \"help multi-pipe\"\n";
char help_multipipe[] = "With this feature we allow to use multiple pipes in a single command. Example: command1 | command2 | command3 ... and so on.\n"
                        "This is implemented sending the output of each command to a file, which es read later by the next command for input purpose.\n"
                        "We create 2 files in tmp folder and we use a counter variable which it raised by 1 for each command send by the user in the line.\n"
                        "With the counter we save the output following the next behavior: file[count%2] = output and input = file[(count+1)%2], allowing to reuse the same files and save some space\n"
                        "This counter only rise when is used on pipes, because we reuse the method who execute the commands\n";
char help_spaces[] = "For this features we implemented a parse function wich convert the commands like this example: command1 | comand2 into |(command1,command2).\n"
                     "In others words, we convert each operator in a prefix way for convenience.\n"
                     "Of course, in this parse_function we split the string by the space character, ignoring multiple-spaces in a row.\n";
char help_history[] = "history: Outputs the last 10 commands used, except those that start with a space.\n"
                      "\nagain <arg>: Replaces the again command with the #arg command in the stored history.\n"; // LEO
char help_chain[] = "true and false features:\n"
                    "As result of our code, each command put its output in a file, so we treat true and false like echo 0 and echo 1 respectively. For us, 0 is true and 1 is false\n"
                    "; && || features:\n"
                    "For these operators we reuse the way in which we execute the multiple pipes but this time we dont change the counter value (for more understanding execute \"help multi-pipe\" command) and declare that this command do not recive inputs\n"
                    "After executing each operator we check if the previous output of the command (0 or 1) and decide whether to execute the next command depending on its behavior\n";
char help_if[] = "For this features we implemented a parse function that converts the if command1 then command2 else command3 end in if(command1,command2,comman3).\n"
                 "Then we parse each command for later execution.\n"
                 "The actual parse function doesn't allow nested if, but it is relatively easy:\n"
                 "For each clause of the statement of an \"if\", for example: from \"if\" to \"then\", we only have to look for the first appearance of the word \"end\", get its position and compare it with the position of the closing clause. "
                 "If the position of the word \"end\" is less than that of the word \"then\", inside that clause there is an if statement, and we only have to parse that nested if recursively.\n";
int indexOf(unsigned long long elem, unsigned long long *array, int cnt)
{
    int i;
    for (i = 0; i < cnt; i++)
    {
        if (array[i] == elem)
            return i;
    }
    return -1;
}

bool directory_exists(const char *path)
{
    DIR *dir = opendir(path);
    if (dir)
    {
        closedir(dir);
        return true;
    }
    else
    {
        return false;
    }
}

char *get_home_subpath(char *sub_path)
{
    const char *homedir = getenv("HOME");
    int homelen = strlen(homedir);
    int sublen = strlen(sub_path);
    char *path = malloc((homelen + sublen + 2) * sizeof(char));

    sprintf(path, "%s/%s", homedir, sub_path);

    return path;
}

int cntdigits(unsigned long long num)
{
    int count = 0;
    do
    {
        num /= 10;
        count++;
    } while (num > 0);

    return count;
}
struct JaggedCharArray splitstr(char *str, char sep)
{
    int strLength = strlen(str);
    int count = 0;
    int i;
    int tokenPointer = 0;

    char **ret = (char **)malloc(strLength * sizeof(char *));

    for (i = 0; i < strLength; i++)
    {
        if (str[i] != sep)
            continue;

        int len = i - tokenPointer;
        if (len == 0)
        {
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

    if (strLength > tokenPointer)
    {
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

char *joinarr(struct JaggedCharArray arr, char sep, int count)
{
    if (count == 0)
        return "";

    int i;
    int retLen = 0;
    for (i = 0; i < count; i++)
    {
        retLen += strlen(arr.arr[i]) + 1;
    }

    int len;
    char *ret = malloc(retLen * sizeof(char));
    char *pret = ret;
    for (i = 0; i < count - 1; i++)
    {
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
int findstr(char *str, char *tok)
{
    size_t len = strlen(str);
    size_t toklen = strlen(tok);
    size_t q = 0; // matched chars so far

    for (int i = 0; i < len; i++)
    {
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

// extracts an integer from str that starts in startpos
int extractint(char *str, int startpos, int *len)
{
    size_t maxlen = strlen(str) - startpos;
    char *num = malloc(maxlen * sizeof(char) + sizeof(char));

    int count = 0;

    while (count < maxlen && (isdigit(str[startpos + count]) || str[startpos + count] == '-'))
    {
        num[count] = str[startpos + count];
        count++;
    }

    num[count] = '\0';

    *len = count;

    return atoi(num);
}

void replacestr(char *source, char *target, int start, int len)
{
    size_t srclen = strlen(source);
    size_t tgtlen = strlen(target);

    size_t extralen = srclen - start - len;

    if (extralen == 0)
    {
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

int readtoend(FILE *f, char *result)
{
    long file_size;
    char *buffer;

    // Determine the size of the file
    fseek(f, 0, SEEK_END);
    file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Allocate memory for the buffer
    buffer = malloc((file_size + 1) * sizeof(char));

    // Read the entire file into the buffer
    fread(buffer, file_size, 1, f);

    // Add a null terminator to the end of the buffer
    buffer[file_size] = '\0';

    return file_size;
}

char *dtryget(struct Dictionary dict, char *var, int *outidx)
{
    for (int i = 0; i < dict.count; i++)
        if (dict.pairs[i].key == var)
        {
            *outidx = i;
            return dict.pairs[i].value;
        }

    *outidx = -1;
    return NULL;
}

int dset(struct Dictionary *dict, char *var, char *value)
{
    int idx;
    dtryget(*dict, var, &idx);

    if (idx >= 0)
    {
        strcpy(dict->pairs[idx].value, value);
        return 0;
    }

    for (int i = 0; i < dict->count; i++)
    {
        if (!dict->pairs[i].hasValue)
        {
            dict->pairs[i].hasValue = true;
            strcpy(dict->pairs[i].key, var);
            strcpy(dict->pairs[i].value, value);
            return 0;
        }
    }

    return 1;
}

int dremove(struct Dictionary *dict, char *var)
{
    int idx;
    dtryget(*dict, var, &idx);

    if (idx >= 0)
    {
        dict->pairs[idx].hasValue = false;
        return 0;
    }

    return 1;
}
char *get_help(char *param)
{
    if (param == NULL)
        return help;

    if (strcmp(param, "basics") == 0)
        return help_basics;
    if (strcmp(param, "multi-pipe") == 0)
        return help_multipipe;
    // if (strcmp(param, "background") == 0)
    //     return help_background;
    if (strcmp(param, "chain") == 0)
        return help_chain;
    // if (strcmp(param, "ctrl+c") == 0)
    //     return help_ctrl;
    if (strcmp(param, "history") == 0)
        return help_history;
    if (strcmp(param, "if") == 0)
        return help_if;
    if (strcmp(param, "multipipe") == 0)
        return help_multipipe;
    if (strcmp(param, "spaces") == 0)
        return help_spaces;
    // if (strcmp(param, "variables") == 0)
    //     return help_variables;

    return help;
}