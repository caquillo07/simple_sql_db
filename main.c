#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define sizeOfAttribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute );

typedef struct {
    char *buffer;
    size_t buffer_length;
    size_t input_length;
} InputBuffer;

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL,
} ExecuteResult;

typedef enum {
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED_COMMAND,
} MetaCommandResult;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_NEGATIVE_ID,
    PREPARE_UNRECOGNIZED_STATEMENT,
    PREPARE_SYNTAX_ERROR,
    PREPARE_STRING_TOO_LONG,
} PrepareResult;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT,
} StatementType;

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1];
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

const uint32_t ID_SIZE = sizeOfAttribute(Row, id);
const uint32_t USERNAME_SIZE = sizeOfAttribute(Row, username);
const uint32_t EMAIL_SIZE = sizeOfAttribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

#define TABLE_MAX_PAGES 100
const uint32_t PAGE_SIZE = 4096;
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * TABLE_MAX_PAGES;

typedef struct {
    int fileDescriptor;
    uint32_t fileLength;
    void* pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
    Pager* pager;
    uint32_t numRows;
} Table;

typedef struct {
    StatementType type;
    Row rowToInsert; // only used by insert statement
} Statement;

void serializeRow(Row *source, void *destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void deserializeRow(void *source, Row *destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

void printRow(Row* row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

void* getPage(Pager* pager, uint32_t pageNum) {
    if (pageNum > TABLE_MAX_PAGES) {
        printf("Tried to fetch page number out of bounds. %d > %d\n", pageNum, TABLE_MAX_PAGES);
    }

    if (pager->pages[pageNum] == NULL) {
        // Cache miss, allocate memory and load from file.
        void* page = malloc(PAGE_SIZE);
        uint32_t numPages = pager->fileLength / PAGE_SIZE;
        
        // we might save a partial page at the end of the file
        // todo(hector) - do this math to make sure its real
        if (pager->fileLength % PAGE_SIZE) {
            numPages += 1;
        }

        if (pageNum <= numPages) {
            lseek(pager->fileDescriptor, pageNum * PAGE_SIZE, SEEK_SET);
            ssize_t bytesRead = read(pager->fileDescriptor, page, PAGE_SIZE);
            if (bytesRead == -1) {
                printf("Error reading file: %d\n", errno);
                exit(EXIT_FAILURE);
            }
        }

        pager->pages[pageNum] = page;
    }

    return pager->pages[pageNum];
}

// returns a pointer to the row requested. This is used to know where to
// read/write in memory for a particular row.
void *rowSlot(Table *table, uint32_t rowNum) {
    uint32_t pageNum = rowNum / ROWS_PER_PAGE;
    void *page = getPage(table->pager, pageNum);
    uint32_t rowOffset = rowNum % ROWS_PER_PAGE;
    uint32_t byteOffset = rowOffset * ROW_SIZE;
    return page + byteOffset;
}

InputBuffer *newInputBuffer() {
    InputBuffer *inputBuffer = (InputBuffer *) malloc(sizeof(InputBuffer));
    *inputBuffer = (InputBuffer) {
            .buffer = NULL,
            .buffer_length= 0,
            .input_length = 0,
    };
    return inputBuffer;
}

Pager* pagerOpen(const char* filename) {
    int fd = open(
        filename,
        O_RDWR |      // Read/Write mode
            O_CREAT,  // Create file if it does not exist
        S_IWUSR |     // User write permission
            S_IRUSR   // User read permission
    );

    if (fd == -1) {
        printf("Unable to open file\n");
    }

    off_t fileLength = lseek(fd, 0, SEEK_END);
    
    Pager* pager = malloc(sizeof(Pager));
    pager->fileDescriptor = fd;
    pager->fileLength = fileLength;

    for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
        pager->pages[i] = NULL;
    }
    return pager;
}

Table *dbOpen(const char* filename) {
    Pager* pager = pagerOpen(filename);
    uint32_t numRows = pager->fileLength / ROW_SIZE;

    Table *table = malloc(sizeof(Table));
    table->pager = pager;
    table->numRows = numRows;
    return table;
}

void freeTable(Table *table) {

    // here tables->pages[i] will return NULL and thus exit the for loop
    // once we run out of stuff to free.
    //
    // Does this hold once the table is full?
    for (int i = 0; table->pages[i]; ++i) {
        free(table->pages[i]);
    }
    free(table);
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

MetaCommandResult doMetaCommand(InputBuffer *inputBuffer, Table* table) {
    if (strcmp(inputBuffer->buffer, ".exit") == 0) {
        closeInputBuffer(inputBuffer);
        freeTable(table);
        exit(EXIT_SUCCESS);
    }
    return META_COMMAND_UNRECOGNIZED_COMMAND;
}

PrepareResult prepareInsert(InputBuffer* input_buffer, Statement* statement) {
    statement->type = STATEMENT_INSERT;

    char* keyword = strtok(input_buffer->buffer, " ");
    char* id_string = strtok(NULL, " ");
    char* username = strtok(NULL, " ");
    char* email = strtok(NULL, " ");

    if (id_string == NULL || username == NULL || email == NULL) {
        return PREPARE_SYNTAX_ERROR;
    }

    int id = atoi(id_string);
    if (id < 0) {
        return PREPARE_NEGATIVE_ID;
    }
    if (strlen(username) > COLUMN_USERNAME_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }

    if (strlen(email) > COLUMN_EMAIL_SIZE) {
        return PREPARE_STRING_TOO_LONG;
    }

    statement->rowToInsert.id = id;
    strcpy(statement->rowToInsert.username, username);
    strcpy(statement->rowToInsert.email, email);

    return PREPARE_SUCCESS;
}

PrepareResult prepareStatement(InputBuffer *inputBuffer, Statement *statement) {
    if (strncmp(inputBuffer->buffer, "insert", 6) == 0) {
        return prepareInsert(inputBuffer, statement);
    }
    if (strcmp(inputBuffer->buffer, "select") == 0) {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    return PREPARE_UNRECOGNIZED_STATEMENT;
}

ExecuteResult executeInsert(Statement *statement, Table *table) {
    if (table->numRows >= TABLE_MAX_ROWS) {
        return EXECUTE_TABLE_FULL;
    }
    Row *rowToInsert = &(statement->rowToInsert);
    serializeRow(rowToInsert, rowSlot(table, table->numRows));
    table->numRows++;
    return EXECUTE_SUCCESS;
}

ExecuteResult executeSelect(Statement *statement, Table *table) {
    Row row;
    for (uint32_t i = 0; i < table->numRows; i++) {
        deserializeRow(rowSlot(table, i), &row);
        printRow(&row);
    }
    return EXECUTE_SUCCESS;
}

ExecuteResult executeStatement(Statement *statement, Table *table) {
    switch (statement->type) {
        case (STATEMENT_INSERT):
            return executeInsert(statement, table);
        case STATEMENT_SELECT:
            return executeSelect(statement, table);
    }
}

int main(int argc, char *argv[]) {
    Table *table = dbOpen();
    InputBuffer *inputBuffer = newInputBuffer();
    while (true) {
        printPrompt();
        readInput(inputBuffer);

        if (inputBuffer->buffer[0] == '.') {
            switch (doMetaCommand(inputBuffer, table)) {
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
            case PREPARE_SYNTAX_ERROR:
                printf("Syntax error. Could not parse statement.\n");
                continue;
            case (PREPARE_UNRECOGNIZED_STATEMENT):
                printf("Unrecognized keyword at start of '%s'.\n", inputBuffer->buffer);
                continue;
            case (PREPARE_STRING_TOO_LONG):
                printf("String is too long.\n");
                continue;
            case (PREPARE_NEGATIVE_ID):
                printf("ID must be positive.\n");
                continue;
        }
        switch (executeStatement(&statement, table)) {
            case EXECUTE_SUCCESS:
                printf("Executed.\n");
                break;
            case EXECUTE_TABLE_FULL:
                printf("Error: Table is full.\n");
                break;
        }
    }
}

