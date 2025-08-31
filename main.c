#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define TABLE_MAX_PAGES 100
#define size_of_attribute(Struct, Attribute) sizeof(((Struct *)0)->Attribute)

typedef enum { EXECUTE_SUCCESS, EXECUTE_TABLE_FULL } ExecuteResult;
typedef enum { STATEMENT_INSERT, STATMENT_SELECT } StatementType;

typedef enum {
  META_COMMAND_SUCCESS,
  META_COMMAND_UNRECOGNISED
} MetaCommandResult;

typedef enum {
  PREPARE_SUCCESS,
  PREPARE_UNRECOGNISED_STATEMENT,
  PREPARE_SYNTAX_ERROR,
  PREPARE_STRING_TOO_LONG,
} PrepareResult;

typedef enum { NODE_INTERNAL, NODE_LEAF } NodeType;

typedef struct {
  uint32_t id;
  char username[COLUMN_USERNAME_SIZE];
  char email[COLUMN_EMAIL_SIZE];
} Row;

typedef struct {
  StatementType type;
  Row row_to_insert;
} Statement;

typedef struct {
  char *buffer;
  size_t buffer_length;
  size_t input_length;
} InputBuffer;

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const uint32_t PAGE_SIZE = 4096;

/*
 * Shared Node Header Layout
 */
const uint32_t NODE_TYPE_SIZE = sizeof(uint8_t);
const uint32_t NODE_TYPE_OFFSET = 0;
const uint32_t IS_ROOT_SIZE = sizeof(uint8_t);
const uint32_t IS_ROOT_OFFSET = NODE_TYPE_SIZE;
const uint32_t PARENT_POINTER_SIZE = sizeof(uint32_t);
const uint8_t COMMON_NODE_HEADER_SIZE =
    NODE_TYPE_SIZE + IS_ROOT_SIZE + PARENT_POINTER_SIZE;

/*
 * Leaf Header Layout
 */
const uint32_t LEAF_NODE_NUM_CELL_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_NUM_CELL_OFFSET = COMMON_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_HEADER_SIZE =
    COMMON_NODE_HEADER_SIZE + LEAF_NODE_NUM_CELL_SIZE;

/*
 * Leaf Node Layout
 */
const uint32_t LEAF_NODE_KEY_SIZE = sizeof(uint32_t);
const uint32_t LEAF_NODE_KEY_OFFSET = 0;
const uint32_t LEAF_NODE_VALUE_SIZE = ROW_SIZE;
const uint32_t LEAF_NODE_VALUE_OFFSET =
    LEAF_NODE_KEY_OFFSET + LEAF_NODE_KEY_SIZE;
const uint32_t LEAF_NODE_CELL_SIZE = LEAF_NODE_KEY_SIZE + LEAF_NODE_VALUE_SIZE;
const uint32_t LEAF_NODE_SPACE_FOR_CELLS = PAGE_SIZE - LEAF_NODE_HEADER_SIZE;
const uint32_t LEAF_NODE_MAX_CELLS =
    LEAF_NODE_SPACE_FOR_CELLS / LEAF_NODE_CELL_SIZE;

typedef struct {
  int file_descriptor;
  uint32_t file_length;
  uint32_t num_pages;
  void *pages[TABLE_MAX_PAGES];
} Pager;

typedef struct {
  Pager *pager;
  uint32_t root_page_num;
} Table;

typedef struct {
  Table *table;
  uint32_t page_num;
  uint32_t cell_num;
  bool end_of_table;
} Cursor;

void print_constants() {
  printf("ROW_SIZE: %d\n", ROW_SIZE);
  printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
  printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
  printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
  printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
  printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

void print_row(Row *row) {
  printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

void serialize_row(Row *source, void *destination) {
  memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
  memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
  memcpy(destination + EMAIL_OFFSET, (source->email), EMAIL_SIZE);
}

void deserialize_row(void *source, Row *destination) {
  memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
  memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
  memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

uint32_t *leaf_node_num_cells(void *node) {
  return node + LEAF_NODE_NUM_CELL_OFFSET;
}

void *leaf_node_cell(void *node, uint32_t cell_num) {
  return node + LEAF_NODE_HEADER_SIZE + cell_num * LEAF_NODE_CELL_SIZE;
}

uint32_t *leaf_node_key(void *node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num);
}

void *leaf_node_value(void *node, uint32_t cell_num) {
  return leaf_node_cell(node, cell_num) + LEAF_NODE_KEY_SIZE;
}

void initialize_leaf_node(void *node) { *leaf_node_num_cells(node) = 0; }

Pager *pager_open(const char *filename) {
  int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);

  if (fd == -1) {
    printf("Unable to open file \n");
    exit(EXIT_FAILURE);
  }

  off_t file_length = lseek(fd, 0, SEEK_END);

  Pager *pager = malloc(sizeof(Pager));
  pager->file_descriptor = fd;
  pager->file_length = file_length;
  pager->num_pages = file_length / PAGE_SIZE;

  if (file_length < pager->num_pages) {
    printf("Db file is not whole, corrupted db file");
    exit(EXIT_FAILURE);
  }

  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    pager->pages[i] = NULL;
  }

  return pager;
}

void *get_page(Pager *pager, uint32_t page_num) {
  if (page_num > TABLE_MAX_PAGES) {
    printf("%d\n", page_num);
    printf("Page out of bound\n");
    exit(EXIT_FAILURE);
  }

  if (pager->pages[page_num] == NULL) {
    void *page = malloc(sizeof(PAGE_SIZE));
    uint32_t num_pages = pager->file_length / PAGE_SIZE;

    // save a partial page if at end of file
    if (pager->file_length % PAGE_SIZE) {
      num_pages += 1;
    }

    if (page_num < num_pages) {
      lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
      size_t bytes_read = read(pager->file_descriptor, page, PAGE_SIZE);

      if (bytes_read == -1) {
        printf("Error reading file: %d\n", errno);
        exit(EXIT_FAILURE);
      }
    }

    pager->pages[page_num] = page;

    if (pager->num_pages < page_num) {
      pager->num_pages = page_num + 1;
    }
  }

  return pager->pages[page_num];
}

void print_contants() {
  printf("ROW SIZE: %d\n", ROW_SIZE);
  printf("COMMON_NODE_HEADER_SIZE: %d\n", COMMON_NODE_HEADER_SIZE);
  printf("LEAF_NODE_HEADER_SIZE: %d\n", LEAF_NODE_HEADER_SIZE);
  printf("LEAF_NODE_CELL_SIZE: %d\n", LEAF_NODE_CELL_SIZE);
  printf("LEAF_NODE_SPACE_FOR_CELLS: %d\n", LEAF_NODE_SPACE_FOR_CELLS);
  printf("LEAF_NODE_MAX_CELLS: %d\n", LEAF_NODE_MAX_CELLS);
}

void print_leaf_node(void *node) {
  uint32_t num_cells = *leaf_node_num_cells(node);
  printf("leaf (size %d)\n", num_cells);
  for (uint32_t i = 0; i < num_cells; i++) {
    uint32_t key = *leaf_node_key(node, i);
    printf("  - %d : %d\n", i, key);
  }
}

void leaf_node_insert(Cursor *cursor, uint32_t key, Row *row) {
  void *node = get_page(cursor->table->pager, cursor->page_num);

  uint32_t num_cells = *leaf_node_num_cells(node);
  if (num_cells >= LEAF_NODE_MAX_CELLS) {
    for (uint32_t i = num_cells; i < cursor->cell_num; i--) {
      memcpy(leaf_node_num_cells(node), leaf_node_num_cells(node),
             LEAF_NODE_CELL_SIZE);
    }
  }

  *(leaf_node_num_cells(node)) += 1;
  *(leaf_node_key(node, cursor->cell_num)) = key;
  serialize_row(row, leaf_node_value(node, cursor->cell_num));
}

Table *db_open(char *filename) {
  Pager *pager = pager_open(filename);
  uint32_t num_rows = pager->file_length / ROW_SIZE;

  Table *table = malloc(sizeof(Table));
  table->pager = pager;
  table->root_page_num = 0;

  if (pager->num_pages == 0) {
    void *root_node = get_page(pager, table->root_page_num);
    initialize_leaf_node(root_node);
  }

  // TODO: STOPPED HERRE
  return table;
}

// void *row_slot(Table *table, uint32_t row_num) {
//   uint32_t page_num = row_num / ROWS_PER_PAGE;
//   void *page = get_page(table->pager, page_num);
//
//   uint32_t row_offset = row_num % ROWS_PER_PAGE;
//   uint32_t byte_offset = row_offset * ROW_SIZE;
//   return page + byte_offset;
// }

void pager_flush(Pager *pager, uint32_t page_num) {
  if (pager->pages[page_num] == NULL) {
    printf("Error flushing db\n");
    exit(EXIT_FAILURE);
  }

  off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);

  if (offset == -1) {
    printf("Error seeking during flush\n");
    exit(EXIT_FAILURE);
  }

  ssize_t bytes_written =
      write(pager->file_descriptor, pager->pages[page_num], PAGE_SIZE);

  if (bytes_written == -1) {
    printf("Error during db write \n");
    exit(EXIT_FAILURE);
  }
}

// void free_table(Table *table) {
//   for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
//     free(table->pages[i]);
//   }
//
//   free(table);
// }

void close_input_buffer(InputBuffer *input_buffer) {
  free(input_buffer->buffer);
  free(input_buffer);
};

void db_close(Table *table) {
  Pager *pager = table->pager;
  uint32_t num_full_pages = table->pager->num_pages / PAGE_SIZE;

  for (uint32_t i = 0; i < pager->num_pages; i++) {
    if (pager->pages[i] == NULL) {
      continue;
    }

    pager_flush(pager, i);
    free(pager->pages[i]);
    pager->pages[i] = NULL;
  }

  int result = close(pager->file_descriptor);

  if (result == -1) {
    printf("Error closing db file.\n");
    exit(EXIT_FAILURE);
  }

  for (uint32_t i = 0; i < TABLE_MAX_PAGES; i++) {
    void *page = pager->pages[i];
    if (page) {
      free(page);
      pager->pages[i] = NULL;
    }
  }

  free(pager);
  free(table);
}

MetaCommandResult do_meta_command(InputBuffer *input_buffer, Table *table) {
  if (strcmp(input_buffer->buffer, ".exit") == 0) {
    db_close(table);
    exit(EXIT_SUCCESS);
  } else if (strcmp(input_buffer->buffer, ".help") == 0) {
    printf("None Yet!\n");
    return META_COMMAND_SUCCESS;
  } else if (strcmp(input_buffer->buffer, ".btree") == 0) {
    printf("Tree\n");
    print_leaf_node(get_page(table->pager, 0));
    return META_COMMAND_SUCCESS;
  } else if (strcmp(input_buffer->buffer, ".constants") == 0) {
    printf("Constants\n");
    print_constants();
    return META_COMMAND_SUCCESS;
  } else {
    return META_COMMAND_UNRECOGNISED;
  }
}

InputBuffer *new_input_buffer() {
  InputBuffer *input_buffer = malloc(sizeof(InputBuffer));
  input_buffer->buffer = NULL;
  input_buffer->buffer_length = 0;
  input_buffer->input_length = 0;

  return input_buffer;
};

PrepareResult prepare_statement(InputBuffer *input_buffer,
                                Statement *statement) {
  if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
    statement->type = STATEMENT_INSERT;

    int args_assigned = sscanf(
        input_buffer->buffer, "insert %d %s %s", &(statement->row_to_insert.id),
        statement->row_to_insert.username, statement->row_to_insert.email);

    if (args_assigned < 3) {
      return PREPARE_SYNTAX_ERROR;
    }
    return PREPARE_SUCCESS;
  }

  if (strncmp(input_buffer->buffer, "select", 6) == 0) {
    statement->type = STATMENT_SELECT;
    return PREPARE_SUCCESS;
  }

  return PREPARE_UNRECOGNISED_STATEMENT;
}

PrepareResult prepare_insert(InputBuffer *input_buffer, Statement *statement) {
  statement->type = STATEMENT_INSERT;

  char *keyword = strtok(input_buffer->buffer, " ");
  char *id_string = strtok(NULL, " ");
  char *username = strtok(NULL, " ");
  char *email = strtok(NULL, " ");

  if (id_string == NULL || username == NULL || email == NULL) {
    return PREPARE_SYNTAX_ERROR;
  }

  int id = atoi(id_string);
  if (strlen(username) > COLUMN_USERNAME_SIZE) {
    return PREPARE_STRING_TOO_LONG;
  }

  if (strlen(email) > COLUMN_EMAIL_SIZE) {
    return PREPARE_STRING_TOO_LONG;
  }

  statement->row_to_insert.id = id;
  strcpy(statement->row_to_insert.username, username);
  strcpy(statement->row_to_insert.email, email);

  return PREPARE_SUCCESS;
}

void print_start_screen() {
  printf("   ____   ____   _      \n");
  printf("  / ___| / __ \\ | |     \n");
  printf(" | (___ | |  | || |     \n");
  printf("  \\___ \\| |  | || |     \n");
  printf("  ____) | |__| || |____ \n");
  printf(" |_____/ \\____/ |______|\n");
  printf("             |          \n");
  printf("             Q          \n");
  printf("       OurSQL-BIH REPL ENGINE\n");
  printf("       Respectfully... run your queries\n");
  printf("       Version 0.1.0-dev\n\n");

  printf("Type '.help' for options.\n");
  printf("Type '.exit' to leave this madness.\n");
}

void print_prompt() { printf("f_yeah_db 🤞🏾> "); }

void read_input(InputBuffer *input_buffer) {
  ssize_t bytes_read =
      getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);

  if (bytes_read <= 0) {
    printf("Error reading input\n");
    exit(EXIT_FAILURE);
  }

  input_buffer->input_length = bytes_read - 1;
  input_buffer->buffer[bytes_read - 1] = 0;
};

Cursor *table_start(Table *table) {
  Cursor *cursor = malloc(sizeof(Cursor));
  cursor->table = table;

  cursor->page_num = table->root_page_num;
  cursor->cell_num = 0;

  void *root_node = get_page(table->pager, table->root_page_num);
  uint32_t num_cells = *leaf_node_num_cells(root_node);
  cursor->end_of_table = num_cells == 0;

  return cursor;
}

Cursor *table_end(Table *table) {
  Cursor *cursor = malloc(sizeof(Cursor));
  cursor->table = table;

  cursor->page_num = table->root_page_num;

  void *root_node = get_page(table->pager, table->root_page_num);
  uint32_t num_cells = *leaf_node_num_cells(root_node);
  cursor->cell_num = num_cells;
  cursor->end_of_table = true;

  return cursor;
}

void *cursor_value(Cursor *cursor) {
  uint32_t page_num = cursor->page_num;

  void *page = get_page(cursor->table->pager, page_num);
  return leaf_node_value(page, cursor->cell_num);
}

void cursor_advance(Cursor *cursor) {
  uint32_t page_num = cursor->page_num;
  void *node = get_page(cursor->table->pager, page_num);

  cursor->cell_num += 1;
  if (cursor->cell_num >= (*leaf_node_num_cells(node))) {
    cursor->end_of_table = true;
  }
}

ExecuteResult execute_insert(Statement *statement, Table *table) {
  void *node = get_page(table->pager, table->root_page_num);
  if (*(leaf_node_num_cells(node)) >= LEAF_NODE_MAX_CELLS) {
    return EXECUTE_TABLE_FULL;
  }

  Row *row_to_insert = &(statement->row_to_insert);
  Cursor *cursor = table_end(table);

  leaf_node_insert(cursor, row_to_insert->id, row_to_insert);

  free(cursor);
  return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement *statement, Table *table) {
  Cursor *cursor = table_start(table);

  Row row;

  while (!(cursor->end_of_table)) {
    uint32_t *c_value = cursor_value(cursor);
    deserialize_row(c_value, &row);
    print_row(&row);
    cursor_advance(cursor);
  }
  return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement *statement, Table *table) {
  switch (statement->type) {
  case (STATEMENT_INSERT):
    return execute_insert(statement, table);
  case (STATMENT_SELECT):
    return execute_select(statement, table);
  }
}

// Basic REPL-CLI (Sometimes you've got to learn to run before you can walk --
// Tony Stank)
int main(int argc, char *argv[]) {
  print_start_screen();

  if (argc < 2) {
    printf("DB filname is required");
    exit(EXIT_FAILURE);
  }

  char *filename = argv[1];
  Table *table = db_open(filename);

  InputBuffer *input_buffer = new_input_buffer();

  while (true) {
    print_prompt();
    read_input(input_buffer);

    if (strcmp(input_buffer->buffer, "exit") == 0) {
      close_input_buffer(input_buffer);
      exit(EXIT_SUCCESS);
    } else {
      if (input_buffer->buffer[0] == '.') {
        switch (do_meta_command(input_buffer, table)) {
        case (META_COMMAND_SUCCESS):
          continue;
        case (META_COMMAND_UNRECOGNISED):
          printf("Unrecognised command '%s' .\n", input_buffer->buffer);
          continue;
        }
      }

      Statement statement;

      switch (prepare_statement(input_buffer, &statement)) {
      case (PREPARE_SUCCESS):
        break;
      case (PREPARE_STRING_TOO_LONG):
        printf("String is too log. \n");
        continue;

      case (PREPARE_SYNTAX_ERROR):
        printf("Syntax error bih cannot parse 🧙🏻‍♀️");
      case (PREPARE_UNRECOGNISED_STATEMENT):
        printf("Unrecognised keyword at start of '%s' . \n",
               input_buffer->buffer);
        continue;
      }

      execute_statement(&statement, table);
      printf("Executed. \n");
    }
  }
}
