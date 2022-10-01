/* wordle.c : cheat at word puzzles */
/* Public Domain 2022 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <editline.h>

#define WORDS_FILENAME "/usr/share/dict/words"
#define WORDLEN 5
#define LINEMAX 256
#define WHITESPACE " \t\r\n"

#define OK (0)
#define ERR (-1)

#define IS_INNER(p) ((intptr_t)(p) & 1)

union word_node;

struct word_inner {
	union word_node *child[2];
	unsigned byteofs;
	unsigned char mask;
};

union word_node {
	struct word_inner inner;
	char data[WORDLEN + 1];
};

static union word_node *top_node;
static char valid_set[27];
static const char alphabet[27] = "abcdefghijklmnopqrstuvwxyz";

/* create a union word_node pointer from a struct word_inner pointer */
static inline union word_node *
make_inner(struct word_inner *inner)
{
	return (union word_node*)((intptr_t)inner | 1);
}

/* extract valid struct word_inner pointer from modified union word_node pointer */
static inline struct word_inner *
inner_ptr(union word_node *node)
{
	return (struct word_inner*)((intptr_t)node & ~1);
}

static inline const char *
keyget(union word_node *node) {
	return IS_INNER(node) ? NULL : node->data;
}

static inline unsigned
critbit_dir(unsigned char ch, unsigned char mask)
{
	return (1 + (ch | mask)) >> 8;
}

static union word_node *
find_nearest(union word_node *top, const char *key, size_t len)
{
	if (!top) {
		return NULL;
	}

	while (IS_INNER(top)) {
		struct word_inner *inner = inner_ptr(top);
		unsigned char ch = (len > inner->byteofs) ? key[inner->byteofs] : 0;
		unsigned dir = critbit_dir(ch, inner->mask);
		top = inner->child[dir];
	}

	return top;
}

static inline unsigned
critbit_critbit(const char *a_key, const char *b_key, unsigned *dir_out, unsigned char *mask_out)
{
	if (!a_key || !b_key) {
		fprintf(stderr, "ERROR in data\n");
		exit(1);
	}

	unsigned byteofs;
	for (byteofs = 0; *a_key && *a_key == *b_key; a_key++, b_key++) {
		        byteofs++;
	}

	unsigned char c;
	c = *a_key ^ *b_key;
	c |= c >> 1;
	c |= c >> 2;
	c |= c >> 4;
	c = (c & ~(c >> 1)) ^ 255;

	if (dir_out) {
		*dir_out = critbit_dir(*a_key, c);
	}
	if (mask_out) {
		*mask_out = c;
	}

	return byteofs;
}

static union word_node **
walk1(union word_node **top_ptr, const char *key, size_t len, unsigned limit, unsigned char mask)
{
	while (IS_INNER(*top_ptr)) {
		struct word_inner *inner = inner_ptr(*top_ptr);

		if (inner->byteofs > limit) {
			break;
		}
		if (inner->byteofs == limit && inner->mask > mask) {
			break;
		}

		unsigned char ch = (len > inner->byteofs) ? key[inner->byteofs] : 0;
		unsigned dir = critbit_dir(ch, inner->mask);
		top_ptr = &inner->child[dir];
	}

	return top_ptr;
}

int
words_add(const char *s, int n)
{
	/* create outer node */
	union word_node *outer = malloc(n + 1);
	if (!outer || IS_INNER(outer)) {
		perror("malloc()");
		exit(1);
	}
	memcpy(outer->data, s, n + 1);

	if (!top_node) {
		top_node = outer;
		return OK;
	}

	/* find the insertion point */
	union word_node *nearest = find_nearest(top_node, s, n);
	const char *q;
	if (!nearest || IS_INNER(nearest) || !(q = keyget(nearest))) {
		fprintf(stderr, "ERROR in tree\n");
		exit(1);
	}
	if (!strcmp(s, q)) {
		fprintf(stderr, "Duplicate key found! (%s)\n", s);
		return ERR;
	}

	/* walk to inner node where critical bit exists */
	unsigned dir;
	unsigned char mask;
	unsigned byteofs = critbit_critbit(s, q, &dir, &mask);
	union word_node **wherep = walk1(&top_node, s, n, byteofs, mask);

	/* allocate new node and place at wherep */
	struct word_inner *inner = malloc(sizeof(*inner));
	if (!inner || IS_INNER(inner)) {
		perror("malloc()");
		exit(1);
	}
	*inner = (struct word_inner){
			.byteofs = byteofs,
			.mask = mask,
		};
	inner->child[dir] = outer;
	inner->child[dir ^ 1] = *wherep;
	*wherep = make_inner(inner);

	return OK;
}

static int
filter_check(const char *value)
{
	while (*value) {
		if (!strchr(valid_set, *value)) {
			return ERR; /* no match */
		}
		value++;
	}

	return OK;
}

void
words_init(void)
{
	strcpy(valid_set, alphabet);

	FILE *f = fopen(WORDS_FILENAME, "r");
	if (!f) {
		perror(WORDS_FILENAME);
		exit(1);
	}

	char s[LINEMAX];
	while (fgets(s, sizeof(s), f)) {
		int n = strlen(s);

		/* remove trailing newline */
		if (s[n - 1] == '\n') {
			s[--n] = 0;
		}

		if (n == WORDLEN && filter_check(s) == OK) {
			words_add(s, n);
		}
	}

	fclose(f);
}

const char *
words_find(const char *key)
{
	if (!top_node) {
		return NULL; /* tree empty */
	}

	unsigned keylen = strlen(key);
	union word_node *outer = find_nearest(top_node, key, keylen);
	if (!outer) {
		fprintf(stderr, "ERROR in tree while searching\n");
		return NULL;
	}
	const char *q = keyget(outer);
	if (strcmp(q, key)) {
		return NULL; /* not found */
	}

	return q;
}

static int
pattern_check(const char *value, const char *pattern)
{
	while (*value) {
		if (*pattern == '?') {
			/* wildcards must match the filter set */
			if (!strchr(valid_set, tolower(*value))) {
				return ERR; /* not in filter set */
			}
		} else {
			/* literal characters must match */
			if (tolower(*value) != tolower(*pattern)) {
				return ERR; /* no match */
			}
		}

		value++;
		pattern++;
	}

	/* must have consumed entire pattern */
	return *pattern == 0 ? OK : ERR;
}

static void
test(union word_node *curr, const char *pattern)
{
	if (IS_INNER(curr)) {
		struct word_inner *inner = inner_ptr(curr);

		unsigned dir;
		for (dir = 0; dir < 2; dir++) {
			test(inner->child[dir], pattern);
		}
	} else {
		const char *key = keyget(curr);
		if (!key) {
			fprintf(stderr, "ERROR in data\n");
			exit(1);
		}
		if (pattern_check(key, pattern) == OK) {
			printf("WORD: %s\n", key);
		}
	}
}

static int
add_to_set(const char *set)
{
	char c;
	unsigned len = strlen(valid_set);
	while ((c = *set++)) {
		/* skip if already in set */
		char *p = strchr(valid_set, c);
		if (p) continue;

		if (len + 1 >= sizeof(valid_set)) {
			fprintf(stderr, "ERROR in adding to set\n");
			return ERR;
		}
		/* append to end of set */
		valid_set[len++] = c;
		valid_set[len] = 0;
	}

	return OK;
}

static void
remove_from_set(const char *set)
{
	char c;
	unsigned len = strlen(valid_set);
	while ((c = *set++)) {
		/* find point to trim */
		char *p = strchr(valid_set, c);
		if (!p) continue;
		unsigned rem = len - (p - valid_set);

		if (rem) {
			memmove(p, p + 1, rem);
		} else {
			*p = 0;
		}
	}
}

static void
help(void)
{
	printf("Command reference:\n"
	       "  quit - terminate the program\n"
	       "  try [pattern] - try a pattern\n"
	       "  eliminate [letters] - remove letters to the valid set\n"
	       "  restore [letters] - add letters to the valid set\n"
	      );
}

static int
command(char *line)
{
	/* discard leading whitespace */
	while (isspace(*line)) {
		line++;
	}

	/* extract first word */
	const char *cmd = line;
	int cmdlen = strcspn(line, WHITESPACE);
	line += cmdlen;
	if (*line) {
		*line = 0;
		line++;
	}

	/* discard whitespace after first word */
	while (isspace(*line)) {
		line++;
	}

	if (!strcmp("quit", cmd)) {
		return ERR;
	} else if (!strcmp("help", cmd)) {
		help();
	} else if (!strcmp("try", cmd)) {
		if (!*line) {
			printf("Usage: try [word-pattern]\n");
		}
		if (filter_check(line) != OK) {
			printf("Unknown command!\n");
		}
		printf("TRY %s\n", line);
		test(top_node, line);
	} else if (!strcmp("eliminate", cmd)) {
		printf("OLD set: %s\n", valid_set);
		remove_from_set(line);
		printf("NEW set: %s\n", valid_set);
	} else if (!strcmp("restore", cmd)) {
		printf("OLD set: %s\n", valid_set);
		add_to_set(line);
		printf("NEW set: %s\n", valid_set);
	} else {
		printf("Unknown command!\n");
	}

	return OK;
}

static void
interactive(void)
{
	char *line;
        while ((line = readline("CLI> ")) != NULL) {
		int result = command(line);
		free(line);
		if (result != OK) {
			break;
		}
	}
}

int
main()
{
	words_init();

#if 0
	// diagnostics
	printf("%s\n", words_find("stone") ? : "ERROR");
	printf("%s\n", words_find("foggy") ? : "ERROR");
	printf("%s\n", words_find("vwxyz") ? : "UNKNOWN");
	test(top_node, "?dd??");
#endif

	help();
	interactive();

	return 0;
}
