
#include <string.h>

#include "parse_opts.h"

void parse_opt(char *opt, char **name_ptr, char **value_ptr) {
    char *equals = strchr(opt, '=');
    if(equals) {
        *equals = '\0';
        *name_ptr = opt;
        *value_ptr = equals + 1;
    } else {
        *name_ptr = opt;
        *value_ptr = NULL;
    }
}
