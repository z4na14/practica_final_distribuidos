CC=gcc
# Para que mas targets, asi se queda lol
CFLAGS=-g #DEBUG
#CFLAGS=-Wall -Wextra -Werror -O3 #RELEASE
BUILD_DIR=./build
SRC_DIR=./src
LIBS_DIR=./lib
TEST_DIR=./tests

INCLUDE_DIRS=-I$(LIBS_DIR) -I$(SRC_DIR)/inc

REPORT_DIR=./report
BIN_DIR=./bin

NAME=ejercicio_final.zip

#############################################################
# AUTHORS                                                   #
#   JORGE ADRIAN SAGHIN DUDULEA - 100522257@ALUMNOS.UC3M.ES #
#   DENIS LOREN MOLDOVAN        - 100522240@ALUMNOS.UC3M.ES #
#############################################################

.PHONY: all clean export

all:


$(BUILD_DIR)/sqlite3.o: $(LIBS_DIR)/sqlite/sqlite3.c
	$(CC) -fPIC -O3 -c $< -o $@



clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	mkdir -p $(BUILD_DIR) $(BIN_DIR)
	rm -f $(TEST_DIR)/db.sqlite ./db.sqlite # DB temporal de los tests

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