/* wordle.c : cheat at word puzzles */
/* Public Domain 2022 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <editline.h>

// #define WORDS_FILENAME "/usr/share/dict/words"
#define WORDS_FILENAME "wordlist.txt"
#define WORDLEN 5
#define LINEMAX 256
#define WHITESPACE " \t\r\n"
#define ALPHABETSET_LEN (26 + 1) /* A-Z plus null terminator */

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
static char valid_set[WORDLEN][ALPHABETSET_LEN];
static char needed_set[ALPHABETSET_LEN]; /* default set for try patterns */
static const char alphabet[ALPHABETSET_LEN] = "abcdefghijklmnopqrstuvwxyz";

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
add_to_set(const char *set, char dest[ALPHABETSET_LEN])
{
	char c;
	unsigned len = strlen(dest);
	while ((c = *set++)) {
		/* skip if already in set */
		char *p = strchr(dest, c);
		if (p) continue;

		if (len + 1 >= ALPHABETSET_LEN) {
			fprintf(stderr, "ERROR in adding to set\n");
			return ERR;
		}
		/* append to end of set */
		dest[len++] = c;
		dest[len] = 0;
	}

	return OK;
}

static int
add_to_valid_set(const char *set, int i)
{
	if (i < 0 || i >= WORDLEN) {
		return ERR;
	}
	return add_to_set(set, valid_set[i]);
}

static int
set_valid_set(const char *set, int i)
{
	if (i < 0 || i >= WORDLEN) {
		return ERR;
	}
	valid_set[i][0] = 0; /* clear the valid set */
	return add_to_valid_set(set, i); /* add a letter back in */
}

static void
remove_from_set(const char *set, char dest[ALPHABETSET_LEN])
{
	char c;
	if (!dest) {
		return;
	}
	unsigned len = strlen(dest);
	while ((c = *set++)) {
		/* find point to trim */
		char *p = strchr(dest, c);
		if (!p) continue;
		unsigned rem = len - (p - dest);

		if (rem) {
			memmove(p, p + 1, rem);
		} else {
			*p = 0;
		}
	}
}

static int
remove_from_valid_set(const char *set, int i)
{
	if (i < 0 || i >= WORDLEN) {
		return ERR;
	}
	remove_from_set(set, valid_set[i]);
	return OK;
}

static int
required_set_check(const char *value, const char *required_set)
{
	char set[ALPHABETSET_LEN];

	if (required_set) {
		strcpy(set, required_set);
	} else {
		set[0] = 0; /* empty set */
	}

	for (; *value; value++) {
		char c = tolower(*value);
		if (!isalpha(c)) {
			continue; /* ignore punctuation and markup */
		}
		if (strchr(set, c)) {
			char single[2] = { c, 0 };
			remove_from_set(single, set);
			// printf("Removed %s from %s\n", single, set);
		}
	}

	/* did we empty the set? */
	if (set[0]) {
		// printf("Warning: still in set: %s\n", set);
		return ERR;
	}

	return OK;
}

static int
filter_check(const char *value)
{
	int i;
	for(i = 0; *value; value++, i++) {
		if (!strchr(valid_set[i], tolower(*value))) {
			return ERR; /* no match */
		}
	}

	return OK;
}

void
words_reset(void)
{
	int i;
	for (i = 0; i < WORDLEN; i++) {
		strcpy(valid_set[i], alphabet);
	}

	/* wordle words never end in 's' */
	remove_from_valid_set("s", WORDLEN - 1);

	needed_set[0] = 0;
}

void
words_init(void)
{
	words_reset();

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
	int i;
	for(i = 0; *value; value++, pattern++, i++) {
		if (i >= WORDLEN) {
			printf("ERROR: Pattern length exceeds WORDLEN (%d)\n", i);
			return ERR;
		}
		if (*pattern == '?') {
			/* wildcards must match the filter set */
			if (!strchr(valid_set[i], tolower(*value))) {
				return ERR; /* not in filter set */
			}
		} else {
			/* literal characters must match */
			if (tolower(*value) != tolower(*pattern)) {
				return ERR; /* no match */
			}
		}
	}

	/* must have consumed entire pattern */
	return *pattern == 0 ? OK : ERR;
}

static unsigned wordwrap_columns;
static unsigned wordwrap_position;

static void
wordwrap_start(void)
{
	const char *env_columns = getenv("COLUMNS");
	wordwrap_columns = env_columns ? atoi(env_columns) : 80;
	wordwrap_position = 0;
}

static void
wordwrap_end(void)
{
	if (wordwrap_position) {
		fputc('\n', stdout);
		wordwrap_position = 0;
	}
}

static void
wordwrap_print(const char *word)
{
	if (!word) {
		return;
	}
	unsigned wordlen = strlen(word);

	/* handle really long words by making a best effort to start them on a new line */
	if (wordlen >= wordwrap_columns) {
		if (wordwrap_position) {
			fputc('\n', stdout);
		}
		fputs(word, stdout);
		fputc('\n', stdout);
		wordwrap_position = 0;
		return;
	}

	/* try to pack short words on a single line. wrap if too long. */
	if (wordwrap_position + wordlen > wordwrap_columns) {
		fputc('\n', stdout);
		wordwrap_position = 0;
	}
	/* prefix additional words with a space */
	if (wordwrap_position) {
		fputc(' ', stdout);
	}
	fputs(word, stdout);
	wordwrap_position += wordlen;
}


static int
test(union word_node *curr, const char *pattern, const char *required_set)
{
	int count = 0;
	char dummypattern[WORDLEN+1];

	/* treat empty set as same as a missing (NULL) set */
	if (required_set && !*required_set) {
		required_set = NULL;
	}

	/* if pattern is NULL or empty, apply a default pattern "?????" */
	if (!pattern || !*pattern) {
		int i;
		for (i = 0; i < WORDLEN; i++) {
			dummypattern[i] = '?';
		}
		dummypattern[i] = 0;
		pattern = dummypattern;
	}

	if (IS_INNER(curr)) {
		struct word_inner *inner = inner_ptr(curr);

		unsigned dir;
		for (dir = 0; dir < 2; dir++) {
			count += test(inner->child[dir], pattern, required_set);
		}
	} else {
		const char *key = keyget(curr);
		if (!key) {
			fprintf(stderr, "ERROR in data\n");
			exit(1);
		}
		if (pattern_check(key, pattern) == OK) {
			if (!required_set || required_set_check(key, required_set) == OK) {
				wordwrap_print(key);
				count++;
			}
		}
	}

	return count;
}

static void
print_valid_set(const char *label)
{
	if (label) {
		puts(label);
	}
	int i;
	for (i = 0; i < WORDLEN; i++) {
		printf("    [%2d] %s\n", i + 1, valid_set[i]);
	}
	printf("   NEED: %s\n", needed_set);
}

static void
help(void)
{
	printf("Command reference:\n"
	       "  quit - terminate the program\n"
	       "  reset - reset the valid letter set\n"
	       "  try [pattern] - try a pattern\n"
	       "  eliminate [letters] - remove letters to the valid set\n"
	       "  -[letters] - short-cut for 'eliminate'\n"
	       "  restore [letters] - add letters to the valid set\n"
	       "  +[letters] - short-cut for 'restore'\n"
	       "  guess - record a guess and its result\n"
	      );
}

static const char *
get_positions(char **line_inout)
{
	static char positions[WORDLEN + 1];
	char *line = *line_inout;
	int i;

	if (*line == '[') {
		/* load digits between [] into our set */
		line++;
		int n = strcspn(line, "]");
		if (n > WORDLEN || !line[n]) {
			return NULL; /* error */
		}
		memcpy(positions, line, n);
		positions[n] = 0;
		line += n + 1;
		while (isspace(*line)) {
			line++;
		}
	} else if (isdigit(*line)) {
		/* if line starts with digits, treat those as the set */
		int n = strspn(line, "0123456789");
		if (n > WORDLEN || !line[n]) {
			return NULL; /* error */
		}
		memcpy(positions, line, n);
		positions[n] = 0;
		line += n;
		while (isspace(*line)) {
			line++;
		}
	} else {
		/* initialize with all numbers in the set */
		for (i = 0; i < WORDLEN; i++) {
			positions[i] = '1' + i;
		}
		positions[i] = 0;
	}

	/* validate string - must only contain numbers in range */
	for (i = 0; positions[i]; i++) {
		if (positions[i] < '1' || positions[i] > ('0' + WORDLEN)) {
			return NULL; /* error */
		}
	}

	*line_inout = line;
	return positions;
}

static int
guess(char *line)
{
	/* extract first argument */
	const char *first = line;
	int firstlen = strcspn(first, WHITESPACE);
	line += firstlen;
	if (*line) {
		*line = 0;
		line++;
	}
	while (isspace(*line)) {
		line++;
	}

	/* extract second argument */
	const char *second = line;
	int secondlen = strcspn(second, WHITESPACE);
	line += secondlen;
	if (*line) {
		*line = 0;
		line++;
	}
	while (isspace(*line)) {
		line++;
	}

	if (!*first || !*second || *line) {
		printf ("ERROR: must provide exactly 2 arguments\n");
		return ERR; /* we must have exactly 2 arguments */
	}

	if (firstlen != secondlen && firstlen != WORDLEN) {
		printf("ERROR: guess must exactly match result and WORDLEN\n");
		return ERR; /* guess must exactly match result and WORDLEN. */
	}

#if 0
	if (filter_check(first) != OK) {
		printf("Word not found in dictionary! (%s)\n", first);
		return ERR;
	}
#endif

	// DEBUG: printf("FIRST=\"%s\" SECOND=\"%s\"\n", first, second);

	char set[2];
	int i, j;
	for (i = 0; i < secondlen; i++) {
		// DEBUG: printf("i=%d\n", i);
		switch (second[i]) {
		case '0':
		case 'x':
			/* letter not found */
			set[0] = first[i];
			set[1] = 0;
			for (j = 0; j < WORDLEN; j++) {
				remove_from_valid_set(set, i);
			}
			// DEBUG: printf("[%d] not found\n", i);
			break;
		case '?':
		case 'y':
			/* letter in word but not this position */
			set[0] = first[i];
			set[1] = 0;
			remove_from_valid_set(set, i);
			add_to_set(set, needed_set);
			// DEBUG: printf("[%d] maybe %c\n", i, first[i]);
			break;
		case '!':
		case 'g':
			/* letter known to be at this position */
			set[0] = first[i];
			set[1] = 0;
			set_valid_set(set, i);
			add_to_set(set, needed_set);
			// DEBUG: printf("[%d] match!\n", i);
			break;
		case '.':
			/* ignore this position */
			break;
		default:
			return ERR;
		}
	}

	return OK;
}

static int
command(char *line)
{
	char cmdtmp[2];

	/* discard leading whitespace */
	while (isspace(*line)) {
		line++;
	}

	if (!*line) {
		return OK; /* ignore blank lines */
	}

	const char *cmd;

	/* check if the first letter is a punctuation shortcut */
	if (ispunct(*line)) {
		/* copy punctuation into temp buffer */
		cmdtmp[0] = *line++;
		cmdtmp[1] = 0;
		cmd = cmdtmp;
	} else {
		/* extract first word */
		cmd = line;
		int cmdlen = strcspn(line, WHITESPACE);
		line += cmdlen;
		if (*line) {
			*line = 0;
			line++;
		}
	}

	/* discard whitespace after first word */
	while (isspace(*line)) {
		line++;
	}

	if (!strcmp("quit", cmd)) {
		return ERR;
	} else if (!strcmp("help", cmd)) {
		help();
	} else if (!strcmp("reset", cmd)) {
		words_reset();
	} else if (!strcmp("try", cmd)) {
		if (!*line) {
			printf("Usage: try <word-pattern> [<optional-required-letters> | -]\n");
			return OK;
		}

		/* extract first argument */
		const char *first = line;
		int firstlen = strcspn(first, WHITESPACE);
		line += firstlen;
		if (*line) {
			*line = 0;
			line++;
		}
		while (isspace(*line)) {
			line++;
		}

		/* extract second argument */
		const char *second = line;
		int secondlen = strcspn(second, WHITESPACE);
		line += secondlen;
		if (*line) {
			*line = 0;
			line++;
		}
		while (isspace(*line)) {
			line++;
		}

		printf("TRY \"%s\" [%s]\n", first, second);
		wordwrap_start();
		int count = test(top_node, first, *second ? *second == '-' ? NULL : second : needed_set);
		wordwrap_end();
		printf("Found %d words\n", count);
	} else if (!strcmp("eliminate", cmd) || !strcmp("-", cmd)) {
		const char *positions = get_positions(&line);
		if (!positions) {
			printf("Usage: eliminate [<12345>] <letters>\n");
			return OK;
		}

		print_valid_set("OLD set: ");
		int i;
		for (i = 0; i < WORDLEN && positions[i]; i++) {
			printf("Removing #%c '%s'\n", positions[i], line);

			remove_from_valid_set(line, positions[i] - '1');
		}
		print_valid_set("NEW set: ");
	} else if (!strcmp("restore", cmd) || !strcmp("+", cmd)) {
		const char *positions = get_positions(&line);
		if (!positions) {
			printf("Usage: restore [<12345>] <letters>\n");
			return OK;
		}

		print_valid_set("OLD set: ");
		int i;
		for (i = 0; i < WORDLEN && positions[i]; i++) {
			add_to_valid_set(line, positions[i] - '1');
		}
		print_valid_set("NEW set: ");
	} else if (!strcmp("guess", cmd)) {
		if (guess(line) != OK) {
			printf("Usage: guess <word> <result>\n"
			       "    result must contain %d characters in the form of:\n"
			       "       0/x - grey, no match\n"
			       "       ?/y - yellow, in word but not in this position\n"
			       "       !/g - green, letter in this position\n"
			       "       .   - ignore this position\n", WORDLEN);
		}

		wordwrap_start();
		int count = test(top_node, NULL, needed_set);
		wordwrap_end();
		printf("Found %d words\n", count);
		print_valid_set("NEW set: ");
	} else {
		printf("Unknown command!\n");
	}

	return OK;
}

static void
interactive(void)
{
	char *line;
        while ((line = readline("> ")) != NULL) {
		if (*line) {
			add_history(line);
		}
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
	test(top_node, "?dd??", NULL);
#endif

	help();
	interactive();

	return 0;
}
