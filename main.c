#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    char *buffer;
    ssize_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND,
} MetaCommandResult;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_UNRECOGNIZED_STATEMENT,
} PrepareResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT,
} StatementType;

typedef struct {
    StatementType type;
} Statement;

InputBuffer *newInputBuffer() {
    InputBuffer *inputBuffer = (InputBuffer *) malloc(sizeof(InputBuffer));
    *inputBuffer = (InputBuffer) {
        .buffer = NULL,
        .buffer_length= 0,
        .input_length = 0,
    };
    return inputBuffer;
}

void printPrompt() {
    printf("db > ");
}

void readInput(InputBuffer *inputBuffer) {
    ssize_t bytesRead = getline(&(inputBuffer->buffer), &inputBuffer->buffer_length, stdin);
    if (bytesRead <= 0) {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    // ignore trailing newline
    inputBuffer->input_length = bytesRead - 1;
    inputBuffer->buffer[bytesRead - 1] = 0;
}

void closeInputBuffer(InputBuffer *inputBuffer) {
    free(inputBuffer->buffer);
    free(inputBuffer);
}

MetaCommandResult doMetaCommand(InputBuffer *inputBuffer) {
    if (strcmp(inputBuffer->buffer, ".exit") == 0) {
        exit(EXIT_SUCCESS);
    }
    return META_COMMAND_UNRECOGNIZED_COMMAND;
}

PrepareResult prepareStatement(InputBuffer *inputBuffer, Statement *statement) {
    if (strncmp(inputBuffer->buffer, "insert", 6) == 0) {
        statement->type = STATEMENT_INSERT;
        return PREPARE_SUCCESS;
    }
    if (strcmp(inputBuffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

void executeStatement(Statement *statement) {
    switch (statement->type) {
        case (STATEMENT_INSERT):
            printf("This is where we would do an insert.\n");
            break;
        case STATEMENT_SELECT:
            printf("This is where we would do a select.\n");
            break;
    }
}

int main(int argc, char *argv[]) {
    InputBuffer *inputBuffer = newInputBuffer();
    while (true) {
        printPrompt();
        readInput(inputBuffer);

        if (inputBuffer->buffer[0] == '.') {
            switch (doMetaCommand(inputBuffer)) {
                case (META_COMMAND_SUCCESS):
                    continue;
                case (META_COMMAND_UNRECOGNIZED_COMMAND):
                    printf("Unrecognized command '%s'.\n", inputBuffer->buffer);
                    continue;
                default:
                    printf("Unrecognized meta command");
            }
        }

        Statement statement;
        switch (prepareStatement(inputBuffer, &statement)) {
            case (PREPARE_SUCCESS):
                break;
            case (PREPARE_UNRECOGNIZED_STATEMENT):
                printf("Unrecognized keyword at start of '%s'.\n", inputBuffer->buffer);
                continue;
        }
        executeStatement(&statement);
        printf("Executed.\n");
    }
    return 0;
}

