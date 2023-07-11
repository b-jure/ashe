#include "an_lexer.h"
#include "an_parser.h"
#include "an_string.h"
#include "an_utils.h"
#include "an_vec.h"

/// Shell command
struct an_command_t {
    an_vec_t *args; /* Command name and arguments */
    an_vec_t *env;  /* Program environmental variables */
};

typedef struct {
    an_command_t command1;
    an_command_t command2;
} an_tuple_t;

typedef union {
    an_command_t command;
    an_tuple_t pipe;
} an_pipe_t;

typedef enum {
    SINGLETON, /* pipe with single command */
    TUPLE,     /* normal pipe with left and right command */
} an_pipetype_t;

struct an_pipeline_t {
    an_pipe_t pipe;     /* pipe command/s */
    an_pipetype_t type; /* type of pipeline */
    bool is_and;        /* Pipeline is connected with '&&' */
};

struct an_conditional_t {
    an_vec_t *pipelines; /* collection of pipelines */
    bool is_background;  /* proccess is run in foreground otherwise background */
};

/// Wrapper struct for clarity
struct an_commandline_t {
    an_conditional_t conditional;
};


an_conditional_t an_conditional_new(an_lexer_t *lexer)
{
    an_conditional_t conditional = {0};
    an_token_t token = an_lexer_next(lexer);

    if(token.type == EOL_TOKEN) {
        return conditional;
    }

    return conditional;
}
