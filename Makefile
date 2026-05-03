CC       = gcc
#CFLAGS  = -g                        #DEBUG
CFLAGS   = -Wall -Wextra -Werror -O3 #RELEASE
BUILD_DIR = ./build
SRC_DIR   = ./src
LIBS_DIR  = ./lib
TEST_DIR  = ./tests

INCLUDE_DIRS = -I$(LIBS_DIR) -I$(SRC_DIR)/inc

REPORT_DIR = ./report

NAME = ejercicio_final.zip

#############################################################
# AUTHORS                                                   #
#   JORGE ADRIAN SAGHIN DUDULEA - 100522257@ALUMNOS.UC3M.ES #
#   DENIS LOREN MOLDOVAN        - 100522240@ALUMNOS.UC3M.ES #
#############################################################

.PHONY: all clean export server tests

all: server tests

# Directorio de salida
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compilar sqlite3 sin flags estrictos (no es código nuestro)
$(BUILD_DIR)/sqlite3.o: $(LIBS_DIR)/sqlite/sqlite3.c | $(BUILD_DIR)
	$(CC) -O3 -c $< -o $@

# Servidor
server: $(BUILD_DIR)/server
$(BUILD_DIR)/server: $(SRC_DIR)/server.c $(SRC_DIR)/users.c $(BUILD_DIR)/sqlite3.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -lpthread -ldl $^ -o $@

# Tests
tests: $(BUILD_DIR)/tests
$(BUILD_DIR)/tests: $(SRC_DIR)/tests.c $(SRC_DIR)/users.c $(BUILD_DIR)/sqlite3.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_DIRS) -lpthread -ldl $^ -o $@

clean:
	rm -rf $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)
	rm -f $(TEST_DIR)/db.sqlite ./db.sqlite

export:
	make clean
	typst compile $(REPORT_DIR)/report.typ ./report.pdf
	rm -f $(NAME)
	zip -r $(NAME) \
	      ./report.pdf \
	      ./Makefile \
	      $(SRC_DIR) \
	      $(LIBS_DIR) \
	      $(TEST_DIR)
	rm report.pdf
