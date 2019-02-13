/*
 * ccstools.c
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2012  NTT DATA CORPORATION
 *
 * Version: 1.8.5   2015/11/11
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License v2 as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */
#include "ccstools.h"

struct ccs_savename_entry {
	struct ccs_savename_entry *next;
	struct ccs_path_info entry;
};

#define CCS_SAVENAME_MAX_HASH            256

/* Use ccs-editpolicy-agent process? */
_Bool ccs_network_mode = false;
/* The IPv4 address of the remote host running the ccs-editpolicy-agent . */
u32 ccs_network_ip = INADDR_NONE;
/* The port number of the remote host running the ccs-editpolicy-agent . */
u16 ccs_network_port = 0;
/* The list of processes currently running. */
struct ccs_task_entry *ccs_task_list = NULL;
/* The length of ccs_task_list . */
int ccs_task_list_len = 0;
/* Read files without calling ccs_normalize_line() ? */
_Bool ccs_freadline_raw = false;

/* Prototypes */

static _Bool ccs_byte_range(const char *str);
static _Bool ccs_decimal(const char c);
static _Bool ccs_hexadecimal(const char c);
static _Bool ccs_alphabet_char(const char c);
static u8 ccs_make_byte(const u8 c1, const u8 c2, const u8 c3);
static int ccs_const_part_length(const char *filename);
static int ccs_domainname_compare(const void *a, const void *b);
static int ccs_path_info_compare(const void *a, const void *b);
static void ccs_sort_domain_policy(struct ccs_domain_policy *dp);

/* Utility functions */

/**
 * ccs_out_of_memory - Print error message and abort.
 *
 * This function does not return.
 */
static void ccs_out_of_memory(void)
{
	fprintf(stderr, "Out of memory. Aborted.\n");
	exit(1);
}

/**
 * ccs_strdup - strdup() with abort on error.
 *
 * @string: String to duplicate.
 *
 * Returns copy of @string on success, abort otherwise.
 */
char *ccs_strdup(const char *string)
{
	char *cp = strdup(string);
	if (!cp)
		ccs_out_of_memory();
	return cp;
}

/**
 * ccs_realloc - realloc() with abort on error.
 *
 * @ptr:  Pointer to void.
 * @size: New size.
 *
 * Returns return value of realloc() on success, abort otherwise.
 */
void *ccs_realloc(void *ptr, const size_t size)
{
	void *vp = realloc(ptr, size);
	if (!vp)
		ccs_out_of_memory();
	return vp;
}

/**
 * ccs_realloc2 - realloc() with abort on error.
 *
 * @ptr:  Pointer to void.
 * @size: New size.
 *
 * Returns return value of realloc() on success, abort otherwise.
 *
 * Allocated memory is cleared with 0.
 */
void *ccs_realloc2(void *ptr, const size_t size)
{
	void *vp = ccs_realloc(ptr, size);
	memset(vp, 0, size);
	return vp;
}

/**
 * ccs_malloc - malloc() with abort on error.
 *
 * @size: Size to allocate.
 *
 * Returns return value of malloc() on success, abort otherwise.
 *
 * Allocated memory is cleared with 0.
 */
void *ccs_malloc(const size_t size)
{
	void *vp = malloc(size);
	if (!vp)
		ccs_out_of_memory();
	memset(vp, 0, size);
	return vp;
}

/**
 * ccs_str_starts - Check whether the given string starts with the given keyword.
 *
 * @str:   Pointer to "char *".
 * @begin: Pointer to "const char *".
 *
 * Returns true if @str starts with @begin, false otherwise.
 *
 * Note that @begin will be removed from @str before returning true. Therefore,
 * @str must not be "const char *".
 *
 * Note that this function in kernel source has different arguments and behaves
 * differently.
 */
_Bool ccs_str_starts(char *str, const char *begin)
{
	const int len = strlen(begin);
	if (strncmp(str, begin, len))
		return false;
	memmove(str, str + len, strlen(str + len) + 1);
	return true;
}

/**
 * ccs_byte_range - Check whether the string is a \ooo style octal value.
 *
 * @str: Pointer to the string.
 *
 * Returns true if @str is a \ooo style octal value, false otherwise.
 */
static _Bool ccs_byte_range(const char *str)
{
	return *str >= '0' && *str++ <= '3' &&
		*str >= '0' && *str++ <= '7' &&
		*str >= '0' && *str <= '7';
}

/**
 * ccs_decimal - Check whether the character is a decimal character.
 *
 * @c: The character to check.
 *
 * Returns true if @c is a decimal character, false otherwise.
 */
static _Bool ccs_decimal(const char c)
{
	return c >= '0' && c <= '9';
}

/**
 * ccs_hexadecimal - Check whether the character is a hexadecimal character.
 *
 * @c: The character to check.
 *
 * Returns true if @c is a hexadecimal character, false otherwise.
 */
static _Bool ccs_hexadecimal(const char c)
{
	return (c >= '0' && c <= '9') ||
		(c >= 'A' && c <= 'F') ||
		(c >= 'a' && c <= 'f');
}

/**
 * ccs_alphabet_char - Check whether the character is an alphabet.
 *
 * @c: The character to check.
 *
 * Returns true if @c is an alphabet character, false otherwise.
 */
static _Bool ccs_alphabet_char(const char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/**
 * ccs_make_byte - Make byte value from three octal characters.
 *
 * @c1: The first character.
 * @c2: The second character.
 * @c3: The third character.
 *
 * Returns byte value.
 */
static u8 ccs_make_byte(const u8 c1, const u8 c2, const u8 c3)
{
	return ((c1 - '0') << 6) + ((c2 - '0') << 3) + (c3 - '0');
}

/**
 * ccs_normalize_line - Format string.
 *
 * @buffer: The line to normalize.
 *
 * Returns nothing.
 *
 * Leading and trailing whitespaces are removed.
 * Multiple whitespaces are packed into single space.
 */
void ccs_normalize_line(char *buffer)
{
	unsigned char *sp = (unsigned char *) buffer;
	unsigned char *dp = (unsigned char *) buffer;
	_Bool first = true;
	while (*sp && (*sp <= ' ' || 127 <= *sp))
		sp++;
	while (*sp) {
		if (!first)
			*dp++ = ' ';
		first = false;
		while (' ' < *sp && *sp < 127)
			*dp++ = *sp++;
		while (*sp && (*sp <= ' ' || 127 <= *sp))
			sp++;
	}
	*dp = '\0';
}

/**
 * ccs_partial_name_hash - Hash name.
 *
 * @c:        A unsigned long value.
 * @prevhash: A previous hash value.
 *
 * Returns new hash value.
 *
 * This function is copied from partial_name_hash() in the kernel source.
 */
static inline unsigned long ccs_partial_name_hash(unsigned long c,
						  unsigned long prevhash)
{
	return (prevhash + (c << 4) + (c >> 4)) * 11;
}

/**
 * ccs_full_name_hash - Hash full name.
 *
 * @name: Pointer to "const unsigned char".
 * @len:  Length of @name in byte.
 *
 * Returns hash value.
 *
 * This function is copied from full_name_hash() in the kernel source.
 */
static inline unsigned int ccs_full_name_hash(const unsigned char *name,
					      unsigned int len)
{
	unsigned long hash = 0;
	while (len--)
		hash = ccs_partial_name_hash(*name++, hash);
	return (unsigned int) hash;
}

/**
 * ccs_const_part_length - Evaluate the initial length without a pattern in a token.
 *
 * @filename: The string to evaluate.
 *
 * Returns the initial length without a pattern in @filename.
 */
static int ccs_const_part_length(const char *filename)
{
	int len = 0;
	if (filename) {
		while (true) {
			char c = *filename++;
			if (!c)
				break;
			if (c != '\\') {
				len++;
				continue;
			}
			c = *filename++;
			switch (c) {
			case '\\':  /* "\\" */
				len += 2;
				continue;
			case '0':   /* "\ooo" */
			case '1':
			case '2':
			case '3':
				c = *filename++;
				if (c < '0' || c > '7')
					break;
				c = *filename++;
				if (c < '0' || c > '7')
					break;
				len += 4;
				continue;
			}
			break;
		}
	}
	return len;
}

/**
 * ccs_fprintf_encoded - fprintf() using TOMOYO's escape rules.
 *
 * @fp:       Pointer to "FILE".
 * @pathname: String to print.
 *
 * Returns nothing.
 */
void ccs_fprintf_encoded(FILE *fp, const char *pathname)
{
	while (true) {
		unsigned char c = *(const unsigned char *) pathname++;
		if (!c)
			break;
		if (c == '\\') {
			fputc('\\', fp);
			fputc('\\', fp);
		} else if (c > ' ' && c < 127) {
			fputc(c, fp);
		} else {
			fprintf(fp, "\\%c%c%c", (c >> 6) + '0',
				((c >> 3) & 7) + '0', (c & 7) + '0');
		}
	}
}

/**
 * ccs_decode - Decode a string in TOMOYO's rule to a string in C.
 *
 * @ascii: Pointer to "const char".
 * @bin:   Pointer to "char". Must not contain wildcards nor '\000'.
 *
 * Returns true if @ascii was successfully decoded, false otherwise.
 *
 * Note that it is legal to pass @ascii == @bin if the caller want to decode
 * a string in a temporary buffer.
 */
_Bool ccs_decode(const char *ascii, char *bin)
{
	while (true) {
		char c = *ascii++;
		*bin++ = c;
		if (!c)
			break;
		if (c == '\\') {
			char d;
			char e;
			u8 f;
			c = *ascii++;
			switch (c) {
			case '\\':      /* "\\" */
				continue;
			case '0':       /* "\ooo" */
			case '1':
			case '2':
			case '3':
				d = *ascii++;
				if (d < '0' || d > '7')
					break;
				e = *ascii++;
				if (e < '0' || e > '7')
					break;
				f = (u8) ((c - '0') << 6) +
					(((u8) (d - '0')) << 3) +
					(((u8) (e - '0')));
				if (f <= ' ' || f >= 127) {
					*(bin - 1) = f;
					continue;
				}
			}
			return false;
		} else if (c <= ' ' || c >= 127) {
			return false;
		}
	}
	return true;
}

/**
 * ccs_correct_word2 - Check whether the given string follows the naming rules.
 *
 * @string: The byte sequence to check. Not '\0'-terminated.
 * @len:    Length of @string.
 *
 * Returns true if @string follows the naming rules, false otherwise.
 */
static _Bool ccs_correct_word2(const char *string, size_t len)
{
	u8 recursion = 20;
	const char *const start = string;
	_Bool in_repetition = false;
	if (!len)
		goto out;
	while (len--) {
		unsigned char c = *string++;
		if (c == '\\') {
			if (!len--)
				goto out;
			c = *string++;
			if (c >= '0' && c <= '3') {
				unsigned char d;
				unsigned char e;
				if (!len-- || !len--)
					goto out;
				d = *string++;
				e = *string++;
				if (d < '0' || d > '7' || e < '0' || e > '7')
					goto out;
				c = ccs_make_byte(c, d, e);
				if (c <= ' ' || c >= 127)
					continue;
				goto out;
			}
			switch (c) {
			case '\\':  /* "\\" */
			case '+':   /* "\+" */
			case '?':   /* "\?" */
			case 'x':   /* "\x" */
			case 'a':   /* "\a" */
			case '-':   /* "\-" */
				continue;
			}
			if (!recursion--)
				goto out;
			switch (c) {
			case '*':   /* "\*" */
			case '@':   /* "\@" */
			case '$':   /* "\$" */
			case 'X':   /* "\X" */
			case 'A':   /* "\A" */
				continue;
			case '{':   /* "/\{" */
				if (string - 3 < start || *(string - 3) != '/')
					goto out;
				in_repetition = true;
				continue;
			case '}':   /* "\}/" */
				if (*string != '/')
					goto out;
				if (!in_repetition)
					goto out;
				in_repetition = false;
				continue;
			}
			goto out;
		} else if (in_repetition && c == '/') {
			goto out;
		} else if (c <= ' ' || c >= 127) {
			goto out;
		}
	}
	if (in_repetition)
		goto out;
	return true;
out:
	return false;
}

/**
 * ccs_correct_word - Check whether the given string follows the naming rules.
 *
 * @string: The string to check.
 *
 * Returns true if @string follows the naming rules, false otherwise.
 */
_Bool ccs_correct_word(const char *string)
{
	return ccs_correct_word2(string, strlen(string));
}

/**
 * ccs_correct_path - Check whether the given pathname follows the naming rules.
 *
 * @filename: The pathname to check.
 *
 * Returns true if @filename follows the naming rules, false otherwise.
 */
_Bool ccs_correct_path(const char *filename)
{
	return *filename == '/' && ccs_correct_word(filename);
}

/**
 * ccs_domain_def - Check whether the given token can be a domainname.
 *
 * @buffer: The token to check.
 *
 * Returns true if @buffer possibly be a domainname, false otherwise.
 */
_Bool ccs_domain_def(const char *buffer)
{
	const char *cp;
	int len;
	if (*buffer != '<')
		return false;
	cp = strchr(buffer, ' ');
	if (!cp)
		len = strlen(buffer);
	else
		len = cp - buffer;
	return buffer[len - 1] == '>' &&
		ccs_correct_word2(buffer + 1, len - 2);
}

/**
 * ccs_correct_domain - Check whether the given domainname follows the naming rules.
 *
 * @domainname: The domainname to check.
 *
 * Returns true if @domainname follows the naming rules, false otherwise.
 */
_Bool ccs_correct_domain(const char *domainname)
{
	if (!domainname || !ccs_domain_def(domainname))
		return false;
	domainname = strchr(domainname, ' ');
	if (!domainname++)
		return true;
	while (1) {
		const char *cp = strchr(domainname, ' ');
		if (!cp)
			break;
		if (*domainname != '/' ||
		    !ccs_correct_word2(domainname, cp - domainname))
			goto out;
		domainname = cp + 1;
	}
	return ccs_correct_path(domainname);
out:
	return false;
}

/**
 * ccs_file_matches_pattern2 - Pattern matching without '/' character and "\-" pattern.
 *
 * @filename:     The start of string to check.
 * @filename_end: The end of string to check.
 * @pattern:      The start of pattern to compare.
 * @pattern_end:  The end of pattern to compare.
 *
 * Returns true if @filename matches @pattern, false otherwise.
 */
static _Bool ccs_file_matches_pattern2(const char *filename,
				       const char *filename_end,
				       const char *pattern,
				       const char *pattern_end)
{
	while (filename < filename_end && pattern < pattern_end) {
		char c;
		if (*pattern != '\\') {
			if (*filename++ != *pattern++)
				return false;
			continue;
		}
		c = *filename;
		pattern++;
		switch (*pattern) {
			int i;
			int j;
		case '?':
			if (c == '/') {
				return false;
			} else if (c == '\\') {
				if (filename[1] == '\\')
					filename++;
				else if (ccs_byte_range(filename + 1))
					filename += 3;
				else
					return false;
			}
			break;
		case '\\':
			if (c != '\\')
				return false;
			if (*++filename != '\\')
				return false;
			break;
		case '+':
			if (!ccs_decimal(c))
				return false;
			break;
		case 'x':
			if (!ccs_hexadecimal(c))
				return false;
			break;
		case 'a':
			if (!ccs_alphabet_char(c))
				return false;
			break;
		case '0':
		case '1':
		case '2':
		case '3':
			if (c == '\\' && ccs_byte_range(filename + 1)
			    && !strncmp(filename + 1, pattern, 3)) {
				filename += 3;
				pattern += 2;
				break;
			}
			return false; /* Not matched. */
		case '*':
		case '@':
			for (i = 0; i <= filename_end - filename; i++) {
				if (ccs_file_matches_pattern2(filename + i,
							      filename_end,
							      pattern + 1,
							      pattern_end))
					return true;
				c = filename[i];
				if (c == '.' && *pattern == '@')
					break;
				if (c != '\\')
					continue;
				if (filename[i + 1] == '\\')
					i++;
				else if (ccs_byte_range(filename + i + 1))
					i += 3;
				else
					break; /* Bad pattern. */
			}
			return false; /* Not matched. */
		default:
			j = 0;
			c = *pattern;
			if (c == '$') {
				while (ccs_decimal(filename[j]))
					j++;
			} else if (c == 'X') {
				while (ccs_hexadecimal(filename[j]))
					j++;
			} else if (c == 'A') {
				while (ccs_alphabet_char(filename[j]))
					j++;
			}
			for (i = 1; i <= j; i++) {
				if (ccs_file_matches_pattern2(filename + i,
							      filename_end,
							      pattern + 1,
							      pattern_end))
					return true;
			}
			return false; /* Not matched or bad pattern. */
		}
		filename++;
		pattern++;
	}
	while (*pattern == '\\' &&
	       (*(pattern + 1) == '*' || *(pattern + 1) == '@'))
		pattern += 2;
	return filename == filename_end && pattern == pattern_end;
}

/**
 * ccs_file_matches_pattern - Pattern matching without '/' character.
 *
 * @filename:     The start of string to check.
 * @filename_end: The end of string to check.
 * @pattern:      The start of pattern to compare.
 * @pattern_end:  The end of pattern to compare.
 *
 * Returns true if @filename matches @pattern, false otherwise.
 */
static _Bool ccs_file_matches_pattern(const char *filename,
				      const char *filename_end,
				      const char *pattern,
				      const char *pattern_end)
{
	const char *pattern_start = pattern;
	_Bool first = true;
	_Bool result;
	while (pattern < pattern_end - 1) {
		/* Split at "\-" pattern. */
		if (*pattern++ != '\\' || *pattern++ != '-')
			continue;
		result = ccs_file_matches_pattern2(filename, filename_end,
						   pattern_start, pattern - 2);
		if (first)
			result = !result;
		if (result)
			return false;
		first = false;
		pattern_start = pattern;
	}
	result = ccs_file_matches_pattern2(filename, filename_end,
					   pattern_start, pattern_end);
	return first ? result : !result;
}

/**
 * ccs_path_matches_pattern2 - Do pathname pattern matching.
 *
 * @f: The start of string to check.
 * @p: The start of pattern to compare.
 *
 * Returns true if @f matches @p, false otherwise.
 */
static _Bool ccs_path_matches_pattern2(const char *f, const char *p)
{
	const char *f_delimiter;
	const char *p_delimiter;
	while (*f && *p) {
		f_delimiter = strchr(f, '/');
		if (!f_delimiter)
			f_delimiter = f + strlen(f);
		p_delimiter = strchr(p, '/');
		if (!p_delimiter)
			p_delimiter = p + strlen(p);
		if (*p == '\\' && *(p + 1) == '{')
			goto recursive;
		if (!ccs_file_matches_pattern(f, f_delimiter, p, p_delimiter))
			return false;
		f = f_delimiter;
		if (*f)
			f++;
		p = p_delimiter;
		if (*p)
			p++;
	}
	/* Ignore trailing "\*" and "\@" in @pattern. */
	while (*p == '\\' &&
	       (*(p + 1) == '*' || *(p + 1) == '@'))
		p += 2;
	return !*f && !*p;
recursive:
	/*
	 * The "\{" pattern is permitted only after '/' character.
	 * This guarantees that below "*(p - 1)" is safe.
	 * Also, the "\}" pattern is permitted only before '/' character
	 * so that "\{" + "\}" pair will not break the "\-" operator.
	 */
	if (*(p - 1) != '/' || p_delimiter <= p + 3 || *p_delimiter != '/' ||
	    *(p_delimiter - 1) != '}' || *(p_delimiter - 2) != '\\')
		return false; /* Bad pattern. */
	do {
		/* Compare current component with pattern. */
		if (!ccs_file_matches_pattern(f, f_delimiter, p + 2,
					      p_delimiter - 2))
			break;
		/* Proceed to next component. */
		f = f_delimiter;
		if (!*f)
			break;
		f++;
		/* Continue comparison. */
		if (ccs_path_matches_pattern2(f, p_delimiter + 1))
			return true;
		f_delimiter = strchr(f, '/');
	} while (f_delimiter);
	return false; /* Not matched. */
}

/**
 * ccs_path_matches_pattern - Check whether the given filename matches the given pattern.
 *
 * @filename: The filename to check.
 * @pattern:  The pattern to compare.
 *
 * Returns true if matches, false otherwise.
 *
 * The following patterns are available.
 *   \\     \ itself.
 *   \ooo   Octal representation of a byte.
 *   \*     Zero or more repetitions of characters other than '/'.
 *   \@     Zero or more repetitions of characters other than '/' or '.'.
 *   \?     1 byte character other than '/'.
 *   \$     One or more repetitions of decimal digits.
 *   \+     1 decimal digit.
 *   \X     One or more repetitions of hexadecimal digits.
 *   \x     1 hexadecimal digit.
 *   \A     One or more repetitions of alphabet characters.
 *   \a     1 alphabet character.
 *
 *   \-     Subtraction operator.
 *
 *   /\{dir\}/   '/' + 'One or more repetitions of dir/' (e.g. /dir/ /dir/dir/
 *               /dir/dir/dir/ ).
 */
_Bool ccs_path_matches_pattern(const struct ccs_path_info *filename,
			       const struct ccs_path_info *pattern)
{
	/*
	if (!filename || !pattern)
		return false;
	*/
	const char *f = filename->name;
	const char *p = pattern->name;
	const int len = pattern->const_len;
	/* If @pattern doesn't contain pattern, I can use strcmp(). */
	if (!pattern->is_patterned)
		return !ccs_pathcmp(filename, pattern);
	/* Don't compare directory and non-directory. */
	if (filename->is_dir != pattern->is_dir)
		return false;
	/* Compare the initial length without patterns. */
	if (strncmp(f, p, len))
		return false;
	f += len;
	p += len;
	return ccs_path_matches_pattern2(f, p);
}

/**
 * ccs_string_compare - strcmp() for qsort() callback.
 *
 * @a: Pointer to "void".
 * @b: Pointer to "void".
 *
 * Returns return value of strcmp().
 */
int ccs_string_compare(const void *a, const void *b)
{
	return strcmp(*(char **) a, *(char **) b);
}

/**
 * ccs_pathcmp - strcmp() for "struct ccs_path_info".
 *
 * @a: Pointer to "const struct ccs_path_info".
 * @b: Pointer to "const struct ccs_path_info".
 *
 * Returns true if @a != @b, false otherwise.
 */
_Bool ccs_pathcmp(const struct ccs_path_info *a, const struct ccs_path_info *b)
{
	return a->hash != b->hash || strcmp(a->name, b->name);
}

/**
 * ccs_fill_path_info - Fill in "struct ccs_path_info" members.
 *
 * @ptr: Pointer to "struct ccs_path_info" to fill in.
 *
 * The caller sets "struct ccs_path_info"->name.
 */
void ccs_fill_path_info(struct ccs_path_info *ptr)
{
	const char *name = ptr->name;
	const int len = strlen(name);
	ptr->total_len = len;
	ptr->const_len = ccs_const_part_length(name);
	ptr->is_dir = len && (name[len - 1] == '/');
	ptr->is_patterned = (ptr->const_len < len);
	ptr->hash = ccs_full_name_hash((const unsigned char *) name, len);
}

/**
 * ccs_savename - Remember string data.
 *
 * @name: Pointer to "const char".
 *
 * Returns pointer to "const struct ccs_path_info" on success, abort otherwise.
 *
 * The returned pointer refers shared string. Thus, the caller must not free().
 */
const struct ccs_path_info *ccs_savename(const char *name)
{
	/* The list of names. */
	static struct ccs_savename_entry name_list[CCS_SAVENAME_MAX_HASH];
	struct ccs_savename_entry *ptr;
	struct ccs_savename_entry *prev = NULL;
	unsigned int hash;
	int len;
	static _Bool first_call = true;
	if (!name)
		ccs_out_of_memory();
	len = strlen(name) + 1;
	hash = ccs_full_name_hash((const unsigned char *) name, len - 1);
	if (first_call) {
		int i;
		first_call = false;
		memset(&name_list, 0, sizeof(name_list));
		for (i = 0; i < CCS_SAVENAME_MAX_HASH; i++) {
			name_list[i].entry.name = "/";
			ccs_fill_path_info(&name_list[i].entry);
		}
	}
	for (ptr = &name_list[hash % CCS_SAVENAME_MAX_HASH]; ptr;
	     ptr = ptr->next) {
		if (hash == ptr->entry.hash && !strcmp(name, ptr->entry.name))
			return &ptr->entry;
		prev = ptr;
	}
	ptr = ccs_malloc(sizeof(*ptr) + len);
	ptr->next = NULL;
	ptr->entry.name = ((char *) ptr) + sizeof(*ptr);
	memmove((void *) ptr->entry.name, name, len);
	ccs_fill_path_info(&ptr->entry);
	prev->next = ptr; /* prev != NULL because name_list is not empty. */
	return &ptr->entry;
}

/**
 * ccs_parse_number - Parse a ccs_number_entry.
 *
 * @number: Number or number range.
 * @entry:  Pointer to "struct ccs_number_entry".
 *
 * Returns 0 on success, -EINVAL otherwise.
 */
int ccs_parse_number(const char *number, struct ccs_number_entry *entry)
{
	unsigned long min;
	unsigned long max;
	char *cp;
	memset(entry, 0, sizeof(*entry));
	if (number[0] != '0') {
		if (sscanf(number, "%lu", &min) != 1)
			return -EINVAL;
	} else if (number[1] == 'x' || number[1] == 'X') {
		if (sscanf(number + 2, "%lX", &min) != 1)
			return -EINVAL;
	} else if (sscanf(number, "%lo", &min) != 1)
		return -EINVAL;
	cp = strchr(number, '-');
	if (cp)
		number = cp + 1;
	if (number[0] != '0') {
		if (sscanf(number, "%lu", &max) != 1)
			return -EINVAL;
	} else if (number[1] == 'x' || number[1] == 'X') {
		if (sscanf(number + 2, "%lX", &max) != 1)
			return -EINVAL;
	} else if (sscanf(number, "%lo", &max) != 1)
		return -EINVAL;
	entry->min = min;
	entry->max = max;
	return 0;
}

/*
 * Routines for parsing IPv4 or IPv6 address.
 * These are copied from lib/hexdump.c net/core/utils.c .
 */
#include <ctype.h>

static int hex_to_bin(char ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	ch = tolower(ch);
	if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	return -1;
}

#define IN6PTON_XDIGIT		0x00010000
#define IN6PTON_DIGIT		0x00020000
#define IN6PTON_COLON_MASK	0x00700000
#define IN6PTON_COLON_1		0x00100000	/* single : requested */
#define IN6PTON_COLON_2		0x00200000	/* second : requested */
#define IN6PTON_COLON_1_2	0x00400000	/* :: requested */
#define IN6PTON_DOT		0x00800000	/* . */
#define IN6PTON_DELIM		0x10000000
#define IN6PTON_NULL		0x20000000	/* first/tail */
#define IN6PTON_UNKNOWN		0x40000000

static inline int xdigit2bin(char c, int delim)
{
	int val;

	if (c == delim || c == '\0')
		return IN6PTON_DELIM;
	if (c == ':')
		return IN6PTON_COLON_MASK;
	if (c == '.')
		return IN6PTON_DOT;

	val = hex_to_bin(c);
	if (val >= 0)
		return val | IN6PTON_XDIGIT | (val < 10 ? IN6PTON_DIGIT : 0);

	if (delim == -1)
		return IN6PTON_DELIM;
	return IN6PTON_UNKNOWN;
}

static int in4_pton(const char *src, int srclen, u8 *dst, int delim,
		    const char **end)
{
	const char *s;
	u8 *d;
	u8 dbuf[4];
	int ret = 0;
	int i;
	int w = 0;

	if (srclen < 0)
		srclen = strlen(src);
	s = src;
	d = dbuf;
	i = 0;
	while(1) {
		int c;
		c = xdigit2bin(srclen > 0 ? *s : '\0', delim);
		if (!(c & (IN6PTON_DIGIT | IN6PTON_DOT | IN6PTON_DELIM |
			   IN6PTON_COLON_MASK))) {
			goto out;
		}
		if (c & (IN6PTON_DOT | IN6PTON_DELIM | IN6PTON_COLON_MASK)) {
			if (w == 0)
				goto out;
			*d++ = w & 0xff;
			w = 0;
			i++;
			if (c & (IN6PTON_DELIM | IN6PTON_COLON_MASK)) {
				if (i != 4)
					goto out;
				break;
			}
			goto cont;
		}
		w = (w * 10) + c;
		if ((w & 0xffff) > 255) {
			goto out;
		}
cont:
		if (i >= 4)
			goto out;
		s++;
		srclen--;
	}
	ret = 1;
	memcpy(dst, dbuf, sizeof(dbuf));
out:
	if (end)
		*end = s;
	return ret;
}

static int in6_pton(const char *src, int srclen, u8 *dst, int delim,
		    const char **end)
{
	const char *s, *tok = NULL;
	u8 *d, *dc = NULL;
	u8 dbuf[16];
	int ret = 0;
	int i;
	int state = IN6PTON_COLON_1_2 | IN6PTON_XDIGIT | IN6PTON_NULL;
	int w = 0;

	memset(dbuf, 0, sizeof(dbuf));

	s = src;
	d = dbuf;
	if (srclen < 0)
		srclen = strlen(src);

	while (1) {
		int c;

		c = xdigit2bin(srclen > 0 ? *s : '\0', delim);
		if (!(c & state))
			goto out;
		if (c & (IN6PTON_DELIM | IN6PTON_COLON_MASK)) {
			/* process one 16-bit word */
			if (!(state & IN6PTON_NULL)) {
				*d++ = (w >> 8) & 0xff;
				*d++ = w & 0xff;
			}
			w = 0;
			if (c & IN6PTON_DELIM) {
				/* We've processed last word */
				break;
			}
			/*
			 * COLON_1 => XDIGIT
			 * COLON_2 => XDIGIT|DELIM
			 * COLON_1_2 => COLON_2
			 */
			switch (state & IN6PTON_COLON_MASK) {
			case IN6PTON_COLON_2:
				dc = d;
				state = IN6PTON_XDIGIT | IN6PTON_DELIM;
				if (dc - dbuf >= sizeof(dbuf))
					state |= IN6PTON_NULL;
				break;
			case IN6PTON_COLON_1|IN6PTON_COLON_1_2:
				state = IN6PTON_XDIGIT | IN6PTON_COLON_2;
				break;
			case IN6PTON_COLON_1:
				state = IN6PTON_XDIGIT;
				break;
			case IN6PTON_COLON_1_2:
				state = IN6PTON_COLON_2;
				break;
			default:
				state = 0;
			}
			tok = s + 1;
			goto cont;
		}

		if (c & IN6PTON_DOT) {
			ret = in4_pton(tok ? tok : s, srclen + (int)(s - tok),
				       d, delim, &s);
			if (ret > 0) {
				d += 4;
				break;
			}
			goto out;
		}

		w = (w << 4) | (0xff & c);
		state = IN6PTON_COLON_1 | IN6PTON_DELIM;
		if (!(w & 0xf000)) {
			state |= IN6PTON_XDIGIT;
		}
		if (!dc && d + 2 < dbuf + sizeof(dbuf)) {
			state |= IN6PTON_COLON_1_2;
			state &= ~IN6PTON_DELIM;
		}
		if (d + 2 >= dbuf + sizeof(dbuf)) {
			state &= ~(IN6PTON_COLON_1|IN6PTON_COLON_1_2);
		}
cont:
		if ((dc && d + 4 < dbuf + sizeof(dbuf)) ||
		    d + 4 == dbuf + sizeof(dbuf)) {
			state |= IN6PTON_DOT;
		}
		if (d >= dbuf + sizeof(dbuf)) {
			state &= ~(IN6PTON_XDIGIT|IN6PTON_COLON_MASK);
		}
		s++;
		srclen--;
	}

	i = 15; d--;

	if (dc) {
		while(d >= dc)
			dst[i--] = *d--;
		while(i >= dc - dbuf)
			dst[i--] = 0;
		while(i >= 0)
			dst[i--] = *d--;
	} else
		memcpy(dst, dbuf, sizeof(dbuf));

	ret = 1;
out:
	if (end)
		*end = s;
	return ret;
}

/**
 * ccs_parse_ip - Parse a ccs_ip_address_entry.
 *
 * @address: IP address or IP range.
 * @entry:   Pointer to "struct ccs_address_entry".
 *
 * Returns 0 on success, -EINVAL otherwise.
 */
int ccs_parse_ip(const char *address, struct ccs_ip_address_entry *entry)
{
	u8 * const min = entry->min;
	u8 * const max = entry->max;
	const char *end;
	memset(entry, 0, sizeof(*entry));
	if (!strchr(address, ':') &&
	    in4_pton(address, -1, min, '-', &end) > 0) {
		entry->is_ipv6 = false;
		if (!*end)
			memmove(max, min, 4);
		else if (*end++ != '-' ||
			 in4_pton(end, -1, max, '\0', &end) <= 0 || *end)
			return -EINVAL;
		return 0;
	}
	if (in6_pton(address, -1, min, '-', &end) > 0) {
		entry->is_ipv6 = true;
		if (!*end)
			memmove(max, min, 16);
		else if (*end++ != '-' ||
			 in6_pton(end, -1, max, '\0', &end) <= 0 || *end)
			return -EINVAL;
		return 0;
	}
	return -EINVAL;
}

/**
 * ccs_open_stream - Establish IP connection.
 *
 * @filename: String to send to remote ccs-editpolicy-agent program.
 *
 * Retruns file descriptor on success, EOF otherwise.
 */
int ccs_open_stream(const char *filename)
{
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	char c;
	int len = strlen(filename) + 1;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ccs_network_ip;
	addr.sin_port = ccs_network_port;
	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) ||
	    write(fd, filename, len) != len || read(fd, &c, 1) != 1 || c) {
		close(fd);
		return EOF;
	}
	return fd;
}

/**
 * ccs_find_domain - Find a domain by name and other attributes.
 *
 * @dp:          Pointer to "const struct ccs_domain_policy".
 * @domainname0: Name of domain to find.
 *
 * Returns index number (>= 0) in the @dp array if found, EOF otherwise.
 */
int ccs_find_domain(const struct ccs_domain_policy *dp,
		    const char *domainname0)
{
	int i;
	struct ccs_path_info domainname;
	domainname.name = domainname0;
	ccs_fill_path_info(&domainname);
	for (i = 0; i < dp->list_len; i++) {
		if (!ccs_pathcmp(&domainname, dp->list[i].domainname))
			return i;
	}
	return EOF;
}

/**
 * ccs_assign_domain - Create a domain by name and other attributes.
 *
 * @dp:         Pointer to "struct ccs_domain_policy".
 * @domainname: Name of domain to find.
 *
 * Returns index number (>= 0) in the @dp array if created or already exists,
 * abort otherwise.
 */
int ccs_assign_domain(struct ccs_domain_policy *dp, const char *domainname)
{
	int index = ccs_find_domain(dp, domainname);
	if (index >= 0)
		return index;
	if (!ccs_correct_domain(domainname)) {
		fprintf(stderr, "Invalid domainname '%s'\n", domainname);
		ccs_out_of_memory();
	}
	index = dp->list_len++;
	dp->list = ccs_realloc(dp->list, dp->list_len *
			       sizeof(struct ccs_domain_info));
	memset(&dp->list[index], 0, sizeof(struct ccs_domain_info));
	dp->list[index].domainname = ccs_savename(domainname);
	return index;
}

/**
 * ccs_get_ppid - Get PPID of the given PID.
 *
 * @pid: A pid_t value.
 *
 * Returns PPID value.
 */
static pid_t ccs_get_ppid(const pid_t pid)
{
	char buffer[1024];
	FILE *fp;
	pid_t ppid = 1;
	memset(buffer, 0, sizeof(buffer));
	snprintf(buffer, sizeof(buffer) - 1, "/proc/%u/status", pid);
	fp = fopen(buffer, "r");
	if (fp) {
		while (memset(buffer, 0, sizeof(buffer)) &&
		       fgets(buffer, sizeof(buffer) - 1, fp)) {
			if (sscanf(buffer, "PPid: %u", &ppid) == 1)
				break;
		}
		fclose(fp);
	}
	return ppid;
}

/**
 * ccs_get_name - Get comm name of the given PID.
 *
 * @pid: A pid_t value.
 *
 * Returns comm name using on success, NULL otherwise.
 *
 * The caller must free() the returned pointer.
 */
static char *ccs_get_name(const pid_t pid)
{
	char buffer[1024];
	FILE *fp;
	memset(buffer, 0, sizeof(buffer));
	snprintf(buffer, sizeof(buffer) - 1, "/proc/%u/status", pid);
	fp = fopen(buffer, "r");
	if (fp) {
		static const int offset = sizeof(buffer) / 6;
		while (memset(buffer, 0, sizeof(buffer)) &&
		       fgets(buffer, sizeof(buffer) - 1, fp)) {
			if (!strncmp(buffer, "Name:\t", 6)) {
				char *cp = buffer + 6;
				memmove(buffer, cp, strlen(cp) + 1);
				cp = strchr(buffer, '\n');
				if (cp)
					*cp = '\0';
				break;
			}
		}
		fclose(fp);
		if (buffer[0] && strlen(buffer) < offset - 1) {
			const char *src = buffer;
			char *dest = buffer + offset;
			while (1) {
				unsigned char c = *src++;
				if (!c) {
					*dest = '\0';
					break;
				}
				if (c == '\\') {
					c = *src++;
					if (c == '\\') {
						memmove(dest, "\\\\", 2);
						dest += 2;
					} else if (c == 'n') {
						memmove(dest, "\\012", 4);
						dest += 4;
					} else {
						break;
					}
				} else if (c > ' ' && c <= 126) {
					*dest++ = c;
				} else {
					*dest++ = '\\';
					*dest++ = (c >> 6) + '0';
					*dest++ = ((c >> 3) & 7) + '0';
					*dest++ = (c & 7) + '0';
				}
			}
			return strdup(buffer + offset);
		}
	}
	return NULL;
}

/* Serial number for sorting ccs_task_list . */
static int ccs_dump_index = 0;

/**
 * ccs_sort_process_entry - Sort ccs_tasklist list.
 *
 * @pid:   Pid to search.
 * @depth: Depth of the process for printing like pstree command.
 *
 * Returns nothing.
 */
static void ccs_sort_process_entry(const pid_t pid, const int depth)
{
	int i;
	for (i = 0; i < ccs_task_list_len; i++) {
		if (pid != ccs_task_list[i].pid)
			continue;
		ccs_task_list[i].index = ccs_dump_index++;
		ccs_task_list[i].depth = depth;
		ccs_task_list[i].selected = true;
	}
	for (i = 0; i < ccs_task_list_len; i++) {
		if (pid != ccs_task_list[i].ppid)
			continue;
		ccs_sort_process_entry(ccs_task_list[i].pid, depth + 1);
	}
}

/**
 * ccs_task_entry_compare - Compare routine for qsort() callback.
 *
 * @a: Pointer to "void".
 * @b: Pointer to "void".
 *
 * Returns index diff value.
 */
static int ccs_task_entry_compare(const void *a, const void *b)
{
	const struct ccs_task_entry *a0 = (struct ccs_task_entry *) a;
	const struct ccs_task_entry *b0 = (struct ccs_task_entry *) b;
	return a0->index - b0->index;
}

/**
 * ccs_add_process_entry - Add entry for running processes.
 *
 * @line:    A line containing PID and profile and domainname.
 * @ppid:    Parent PID.
 * @name:    Comm name (allocated by strdup()).
 *
 * Returns nothing.
 *
 * @name is free()d on failure.
 */
static void ccs_add_process_entry(const char *line, const pid_t ppid,
				  char *name)
{
	int index;
	unsigned int pid = 0;
	int profile = -1;
	char *domain;
	if (!line || sscanf(line, "%u %u", &pid, &profile) != 2) {
		free(name);
		return;
	}
	domain = strchr(line, '<');
	if (domain)
		domain = ccs_strdup(domain);
	else
		domain = ccs_strdup("<UNKNOWN>");
	index = ccs_task_list_len++;
	ccs_task_list = ccs_realloc(ccs_task_list, ccs_task_list_len *
				    sizeof(struct ccs_task_entry));
	memset(&ccs_task_list[index], 0, sizeof(ccs_task_list[0]));
	ccs_task_list[index].pid = pid;
	ccs_task_list[index].ppid = ppid;
	ccs_task_list[index].profile = profile;
	ccs_task_list[index].name = name;
	ccs_task_list[index].domain = domain;
}

/**
 * ccs_read_process_list - Read all process's information.
 *
 * @show_all: Ture if kernel threads should be included, false otherwise.
 *
 * Returns nothing.
 */
void ccs_read_process_list(_Bool show_all)
{
	int i;
	while (ccs_task_list_len) {
		ccs_task_list_len--;
		free((void *) ccs_task_list[ccs_task_list_len].name);
		free((void *) ccs_task_list[ccs_task_list_len].domain);
	}
	ccs_dump_index = 0;
	if (ccs_network_mode) {
		FILE *fp = ccs_open_write(show_all ?
					  "proc:all_process_status" :
					  "proc:process_status");
		if (!fp)
			return;
		ccs_get();
		while (true) {
			char *line = ccs_freadline(fp);
			unsigned int pid = 0;
			unsigned int ppid = 0;
			char *name;
			if (!line)
				break;
			sscanf(line, "PID=%u PPID=%u", &pid, &ppid);
			name = strstr(line, "NAME=");
			if (name)
				name = ccs_strdup(name + 5);
			else
				name = ccs_strdup("<UNKNOWN>");
			line = ccs_freadline(fp);
			ccs_add_process_entry(line, ppid, name);
		}
		ccs_put();
		fclose(fp);
	} else {
		static const int line_len = 8192;
		char *line;
		int status_fd = open(CCS_PROC_POLICY_PROCESS_STATUS, O_RDWR);
		DIR *dir = opendir("/proc/");
		if (status_fd == EOF || !dir) {
			if (status_fd != EOF)
				close(status_fd);
			if (dir)
				closedir(dir);
			return;
		}
		line = ccs_malloc(line_len);
		while (1) {
			char *name;
			//int ret_ignored; //old code
			unsigned int pid = 0;
			char buffer[128];
			char test[16];
			struct dirent *dent = readdir(dir);
			if (!dent)
				break;
			if (dent->d_type != DT_DIR ||
			    sscanf(dent->d_name, "%u", &pid) != 1 || !pid)
				continue;
			memset(buffer, 0, sizeof(buffer));
			if (!show_all) {
				snprintf(buffer, sizeof(buffer) - 1,
					 "/proc/%u/exe", pid);
				if (readlink(buffer, test, sizeof(test)) <= 0)
					continue;
			}
			name = ccs_get_name(pid);
			if (!name)
				name = ccs_strdup("<UNKNOWN>");
			snprintf(buffer, sizeof(buffer) - 1, "%u\n", pid);
			//ret_ignored = write(status_fd, buffer, strlen(buffer)); //old code
			write(status_fd, buffer, strlen(buffer));
			memset(line, 0, line_len);
			//ret_ignored = read(status_fd, line, line_len - 1); //old code
			read(status_fd, line, line_len - 1);
			ccs_add_process_entry(line, ccs_get_ppid(pid), name);
		}
		free(line);
		closedir(dir);
		close(status_fd);
	}
	ccs_sort_process_entry(1, 0);
	for (i = 0; i < ccs_task_list_len; i++) {
		if (ccs_task_list[i].selected) {
			ccs_task_list[i].selected = false;
			continue;
		}
		ccs_task_list[i].index = ccs_dump_index++;
		ccs_task_list[i].depth = 0;
	}
	qsort(ccs_task_list, ccs_task_list_len, sizeof(struct ccs_task_entry),
	      ccs_task_entry_compare);
}

/**
 * ccs_open_write - Open a file for writing.
 *
 * @filename: String to send to remote ccs-editpolicy-agent program if using
 *            network mode, file to open for writing otherwise.
 *
 * Returns pointer to "FILE" on success, NULL otherwise.
 */
FILE *ccs_open_write(const char *filename)
{
	if (ccs_network_mode) {
		const int fd = socket(AF_INET, SOCK_STREAM, 0);
		struct sockaddr_in addr;
		FILE *fp;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = ccs_network_ip;
		addr.sin_port = ccs_network_port;
		if (connect(fd, (struct sockaddr *) &addr, sizeof(addr))) {
			close(fd);
			return NULL;
		}
		fp = fdopen(fd, "r+");
		/* setbuf(fp, NULL); */
		fprintf(fp, "%s", filename);
		fputc(0, fp);
		fflush(fp);
		if (fgetc(fp) != 0) {
			fclose(fp);
			return NULL;
		}
		return fp;
	} else {
		return fdopen(open(filename, O_WRONLY), "w");
	}
}

/**
 * ccs_close_write - Close stream opened by ccs_open_write().
 *
 * @fp: Pointer to "FILE".
 *
 * Returns true on success, false otherwise.
 */
_Bool ccs_close_write(FILE *fp)
{
	_Bool result = true;
	if (ccs_network_mode) {
		if (fputc(0, fp) == EOF)
			result = false;
		if (fflush(fp) == EOF)
			result = false;
		if (fgetc(fp) == EOF)
			result = false;
	}
	if (fclose(fp) == EOF)
		result = false;
	return result;
}


/**
 * ccs_open_read - Open a file for reading.
 *
 * @filename: String to send to remote ccs-editpolicy-agent program if using
 *            network mode, file to open for reading otherwise.
 *
 * Returns pointer to "FILE" on success, NULL otherwise.
 */
FILE *ccs_open_read(const char *filename)
{
	if (ccs_network_mode) {
		FILE *fp = ccs_open_write(filename);
		if (fp) {
			fputc(0, fp);
			fflush(fp);
		}
		return fp;
	} else {
		return fopen(filename, "r");
	}
}

/**
 * ccs_move_proc_to_file - Save /proc/ccs/ to /etc/ccs/ .
 *
 * @src:  Filename to save from.
 * @dest: Filename to save to.
 *
 * Returns true on success, false otherwise.
 */
_Bool ccs_move_proc_to_file(const char *src, const char *dest)
{
	FILE *proc_fp = ccs_open_read(src);
	FILE *file_fp;
	_Bool result = true;
	if (!proc_fp) {
		fprintf(stderr, "Can't open %s for reading.\n", src);
		return false;
	}
	file_fp = dest ? fopen(dest, "w") : stdout;
	if (!file_fp) {
		fprintf(stderr, "Can't open %s for writing.\n", dest);
		fclose(proc_fp);
		return false;
	}
	while (true) {
		const int c = fgetc(proc_fp);
		if (ccs_network_mode && !c)
			break;
		if (c == EOF)
			break;
		if (fputc(c, file_fp) == EOF)
			result = false;
	}
	fclose(proc_fp);
	if (file_fp != stdout)
		if (fclose(file_fp) == EOF)
			result = false;
	return result;
}

/**
 * ccs_clear_domain_policy - Clean up domain policy.
 *
 * @dp: Pointer to "struct ccs_domain_policy".
 *
 * Returns nothing.
 */
void ccs_clear_domain_policy(struct ccs_domain_policy *dp)
{
	int index;
	for (index = 0; index < dp->list_len; index++) {
		free(dp->list[index].string_ptr);
		dp->list[index].string_ptr = NULL;
		dp->list[index].string_count = 0;
	}
	free(dp->list);
	dp->list = NULL;
	dp->list_len = 0;
}

/**
 * ccs_find_domain_by_ptr - Find a domain by memory address.
 *
 * @dp:         Pointer to "struct ccs_domain_policy".
 * @domainname: Pointer to "const struct ccs_path_info".
 *
 * Returns index number (>= 0) in the @dp array if found, EOF otherwise.
 *
 * This function is faster than faster than ccs_find_domain() because
 * this function searches for a domain by address (i.e. avoid strcmp()).
 */
int ccs_find_domain_by_ptr(struct ccs_domain_policy *dp,
			   const struct ccs_path_info *domainname)
{
	int i;
	for (i = 0; i < dp->list_len; i++) {
		if (dp->list[i].domainname == domainname)
			return i;
	}
	return EOF;
}

/**
 * ccs_domain_name - Return domainname.
 *
 * @dp:    Pointer to "const struct ccs_domain_policy".
 * @index: Index in the @dp array.
 *
 * Returns domainname.
 *
 * Note that this function does not validate @index value.
 */
const char *ccs_domain_name(const struct ccs_domain_policy *dp,
			    const int index)
{
	return dp->list[index].domainname->name;
}

/**
 * ccs_domainname_compare - strcmp() for qsort() callback.
 *
 * @a: Pointer to "void".
 * @b: Pointer to "void".
 *
 * Returns return value of strcmp().
 */
static int ccs_domainname_compare(const void *a, const void *b)
{
	return strcmp(((struct ccs_domain_info *) a)->domainname->name,
		      ((struct ccs_domain_info *) b)->domainname->name);
}

/**
 * ccs_path_info_compare - strcmp() for qsort() callback.
 *
 * @a: Pointer to "void".
 * @b: Pointer to "void".
 *
 * Returns return value of strcmp().
 */
static int ccs_path_info_compare(const void *a, const void *b)
{
	const char *a0 = (*(struct ccs_path_info **) a)->name;
	const char *b0 = (*(struct ccs_path_info **) b)->name;
	return strcmp(a0, b0);
}

/**
 * ccs_sort_domain_policy - Sort domain policy.
 *
 * @dp: Pointer to "struct ccs_domain_policy".
 *
 * Returns nothing.
 */
static void ccs_sort_domain_policy(struct ccs_domain_policy *dp)
{
	int i;
	qsort(dp->list, dp->list_len, sizeof(struct ccs_domain_info),
	      ccs_domainname_compare);
	for (i = 0; i < dp->list_len; i++)
		qsort(dp->list[i].string_ptr, dp->list[i].string_count,
		      sizeof(struct ccs_path_info *), ccs_path_info_compare);
}

/**
 * ccs_read_domain_policy - Read domain policy from file or network or stdin.
 *
 * @dp:       Pointer to "struct ccs_domain_policy".
 * @filename: Domain policy's pathname.
 *
 * Returns nothing.
 */
void ccs_read_domain_policy(struct ccs_domain_policy *dp, const char *filename)
{
	FILE *fp = stdin;
	if (filename) {
		fp = ccs_open_read(filename);
		if (!fp) {
			fprintf(stderr, "Can't open %s\n", filename);
			return;
		}
	}
	ccs_get();
	ccs_handle_domain_policy(dp, fp, true);
	ccs_put();
	if (fp != stdin)
		fclose(fp);
	ccs_sort_domain_policy(dp);
}

/**
 * ccs_write_domain_policy - Write domain policy to file descriptor.
 *
 * @dp: Pointer to "struct ccs_domain_policy".
 * @fd: File descriptor.
 *
 * Returns 0.
 */
int ccs_write_domain_policy(struct ccs_domain_policy *dp, const int fd)
{
	int i;
	int j;
	for (i = 0; i < dp->list_len; i++) {
		const struct ccs_path_info **string_ptr
			= dp->list[i].string_ptr;
		const int string_count = dp->list[i].string_count;
		//int ret_ignored; //old code
		//ret_ignored = write(fd, dp->list[i].domainname->name, dp->list[i].domainname->total_len); //old code
		write(fd, dp->list[i].domainname->name, dp->list[i].domainname->total_len);
		//ret_ignored = write(fd, "\n", 1); //old code
		write(fd, "\n", 1);
		if (dp->list[i].profile_assigned) {
			char buf[128];
			memset(buf, 0, sizeof(buf));
			snprintf(buf, sizeof(buf) - 1, "use_profile %u\n\n",
				 dp->list[i].profile);
			//ret_ignored = write(fd, buf, strlen(buf)); //old code
			write(fd, buf, strlen(buf));
		} else
			//ret_ignored = write(fd, "\n", 1); //old code
			write(fd, "\n", 1);
		for (j = 0; j < string_count; j++) {
			//ret_ignored = write(fd, string_ptr[j]->name, string_ptr[j]->total_len); //old code
			write(fd, string_ptr[j]->name, string_ptr[j]->total_len);
			//ret_ignored = write(fd, "\n", 1); //old code
			write(fd, "\n", 1);
		}
		//ret_ignored = write(fd, "\n", 1); //old code
		write(fd, "\n", 1);
	}
	return 0;
}

/**
 * ccs_delete_domain - Delete a domain from domain policy.
 *
 * @dp:    Pointer to "struct ccs_domain_policy".
 * @index: Index in the @dp array.
 *
 * Returns nothing.
 */
void ccs_delete_domain(struct ccs_domain_policy *dp, const int index)
{
	if (index >= 0 && index < dp->list_len) {
		int i;
		free(dp->list[index].string_ptr);
		for (i = index; i < dp->list_len - 1; i++)
			dp->list[i] = dp->list[i + 1];
		dp->list_len--;
	}
}

/**
 * ccs_add_string_entry - Add string entry to a domain.
 *
 * @dp:    Pointer to "struct ccs_domain_policy".
 * @entry: String to add.
 * @index: Index in the @dp array.
 *
 * Returns 0 if successfully added or already exists, -EINVAL otherwise.
 */
int ccs_add_string_entry(struct ccs_domain_policy *dp, const char *entry,
			 const int index)
{
	const struct ccs_path_info **acl_ptr;
	int acl_count;
	const struct ccs_path_info *cp;
	int i;
	if (index < 0 || index >= dp->list_len) {
		fprintf(stderr, "ERROR: domain is out of range.\n");
		return -EINVAL;
	}
	if (!entry || !*entry)
		return -EINVAL;
	cp = ccs_savename(entry);

	acl_ptr = dp->list[index].string_ptr;
	acl_count = dp->list[index].string_count;

	/* Check for the same entry. */
	for (i = 0; i < acl_count; i++)
		/* Faster comparison, for they are ccs_savename'd. */
		if (cp == acl_ptr[i])
			return 0;

	acl_ptr = ccs_realloc(acl_ptr, (acl_count + 1) *
			      sizeof(const struct ccs_path_info *));
	acl_ptr[acl_count++] = cp;
	dp->list[index].string_ptr = acl_ptr;
	dp->list[index].string_count = acl_count;
	return 0;
}

/**
 * ccs_del_string_entry - Delete string entry from a domain.
 *
 * @dp:    Pointer to "struct ccs_domain_policy".
 * @entry: String to remove.
 * @index: Index in the @dp array.
 *
 * Returns 0 if successfully removed, -ENOENT if not found,
 * -EINVAL otherwise.
 */
int ccs_del_string_entry(struct ccs_domain_policy *dp, const char *entry,
			 const int index)
{
	const struct ccs_path_info **acl_ptr;
	int acl_count;
	const struct ccs_path_info *cp;
	int i;
	if (index < 0 || index >= dp->list_len) {
		fprintf(stderr, "ERROR: domain is out of range.\n");
		return -EINVAL;
	}
	if (!entry || !*entry)
		return -EINVAL;
	cp = ccs_savename(entry);

	acl_ptr = dp->list[index].string_ptr;
	acl_count = dp->list[index].string_count;

	for (i = 0; i < acl_count; i++) {
		/* Faster comparison, for they are ccs_savename'd. */
		if (cp != acl_ptr[i])
			continue;
		dp->list[index].string_count--;
		for (; i < acl_count - 1; i++)
			acl_ptr[i] = acl_ptr[i + 1];
		return 0;
	}
	return -ENOENT;
}

/**
 * ccs_handle_domain_policy - Parse domain policy.
 *
 * @dp:       Pointer to "struct ccs_domain_policy".
 * @fp:       Pointer to "FILE".
 * @is_write: True if input, false if output.
 *
 * Returns nothing.
 */
void ccs_handle_domain_policy(struct ccs_domain_policy *dp, FILE *fp,
			      _Bool is_write)
{
	int i;
	int index = EOF;
	if (!is_write)
		goto read_policy;
	while (true) {
		char *line = ccs_freadline_unpack(fp);
		_Bool is_delete = false;
		_Bool is_select = false;
		unsigned int profile;
		if (!line)
			break;
		if (ccs_str_starts(line, "delete "))
			is_delete = true;
		else if (ccs_str_starts(line, "select "))
			is_select = true;
		ccs_str_starts(line, "domain=");
		if (ccs_domain_def(line)) {
			if (is_delete) {
				index = ccs_find_domain(dp, line);
				if (index >= 0)
					ccs_delete_domain(dp, index);
				index = EOF;
				continue;
			}
			if (is_select) {
				index = ccs_find_domain(dp, line);
				continue;
			}
			index = ccs_assign_domain(dp, line);
			continue;
		}
		if (index == EOF || !line[0])
			continue;
		if (sscanf(line, "use_profile %u", &profile) == 1) {
			dp->list[index].profile = (u8) profile;
			dp->list[index].profile_assigned = 1;
		} else if (is_delete)
			ccs_del_string_entry(dp, line, index);
		else
			ccs_add_string_entry(dp, line, index);
	}
	return;
read_policy:
	for (i = 0; i < dp->list_len; i++) {
		int j;
		const struct ccs_path_info **string_ptr
			= dp->list[i].string_ptr;
		const int string_count = dp->list[i].string_count;
		fprintf(fp, "%s\n", ccs_domain_name(dp, i));
		if (dp->list[i].profile_assigned)
			fprintf(fp, "use_profile %u\n", dp->list[i].profile);
		fprintf(fp, "\n");
		for (j = 0; j < string_count; j++)
			fprintf(fp, "%s\n", string_ptr[j]->name);
		fprintf(fp, "\n");
	}
}

/* Is the shared buffer for ccs_freadline() and ccs_shprintf() owned? */
static _Bool ccs_buffer_locked = false;

/**
 * ccs_get - Mark the shared buffer for ccs_freadline() and ccs_shprintf() owned.
 *
 * Returns nothing.
 *
 * This is for avoiding accidental overwriting.
 * ccs_freadline() and ccs_shprintf() have their own memory buffer.
 */
void ccs_get(void)
{
	if (ccs_buffer_locked)
		ccs_out_of_memory();
	ccs_buffer_locked = true;
}

/**
 * ccs_put - Mark the shared buffer for ccs_freadline() and ccs_shprintf() no longer owned.
 *
 * Returns nothing.
 *
 * This is for avoiding accidental overwriting.
 * ccs_freadline() and ccs_shprintf() have their own memory buffer.
 */
void ccs_put(void)
{
	if (!ccs_buffer_locked)
		ccs_out_of_memory();
	ccs_buffer_locked = false;
}

/**
 * ccs_shprintf - sprintf() to dynamically allocated buffer.
 *
 * @fmt: The printf()'s format string, followed by parameters.
 *
 * Returns pointer to dynamically allocated buffer.
 *
 * The caller must not free() the returned pointer.
 */
char *ccs_shprintf(const char *fmt, ...)
{
	while (true) {
		static char *policy = NULL;
		static int max_policy_len = 0;
		va_list args;
		int len;
		va_start(args, fmt);
		len = vsnprintf(policy, max_policy_len, fmt, args);
		va_end(args);
		if (len < 0)
			ccs_out_of_memory();
		if (len >= max_policy_len) {
			max_policy_len = len + 1;
			policy = ccs_realloc(policy, max_policy_len);
		} else
			return policy;
	}
}

/**
 * ccs_freadline - Read a line from file to dynamically allocated buffer.
 *
 * @fp: Pointer to "FILE".
 *
 * Returns pointer to dynamically allocated buffer on success, NULL otherwise.
 *
 * The caller must not free() the returned pointer.
 */
char *ccs_freadline(FILE *fp)
{
	static char *policy = NULL;
	int pos = 0;
	while (true) {
		static int max_policy_len = 0;
		const int c = fgetc(fp);
		if (c == EOF)
			return NULL;
		if (ccs_network_mode && !c)
			return NULL;
		if (pos == max_policy_len) {
			max_policy_len += 4096;
			policy = ccs_realloc(policy, max_policy_len);
		}
		policy[pos++] = (char) c;
		if (c == '\n') {
			policy[--pos] = '\0';
			break;
		}
	}
	if (!ccs_freadline_raw)
		ccs_normalize_line(policy);
	return policy;
}

/**
 * ccs_freadline_unpack - Read a line from file to dynamically allocated buffer.
 *
 * @fp: Pointer to "FILE". Maybe NULL.
 *
 * Returns pointer to dynamically allocated buffer on success, NULL otherwise.
 *
 * The caller must not free() the returned pointer.
 *
 * The caller must repeat calling this function without changing @fp (or with
 * changing @fp to NULL) until this function returns NULL, for this function
 * caches a line if the line is packed. Otherwise, some garbage lines might be
 * returned to the caller.
 */
char *ccs_freadline_unpack(FILE *fp)
{
	static char *previous_line = NULL;
	static char *cached_line = NULL;
	static int pack_start = 0;
	static int pack_len = 0;
	if (cached_line)
		goto unpack;
	if (!fp)
		return NULL;
	{
		char *pos;
		unsigned int offset;
		unsigned int len;
		char *line = ccs_freadline(fp);
		if (!line)
			return NULL;
		/*
		 * Skip
		 *   <$namespace>
		 * prefix unless this line represents a domainname.
		 */
		if (ccs_domain_def(line) && !ccs_correct_domain(line)) {
			pos = strchr(line, ' ');
			if (!pos++)
				pos = line;
		} else
			pos = line;
		/*
		 * Skip
		 *   acl_group $group
		 * prefix if this line is a line of exception policy.
		 */
		if (sscanf(pos, "acl_group %u", &offset) == 1 && offset < 256)
			pos = strchr(pos + 11, ' ');
		else
			pos = NULL;
		if (pos++)
			offset = pos - line;
		else
			offset = 0;
		/*
		 * Only "file " and "network " are subjected to unpacking.
		 */
		if (!strncmp(line + offset, "file ", 5)) {
			char *cp = line + offset + 5;
			char *cp2 = strchr(cp + 1, ' ');
			len = cp2 - cp;
			if (cp2 && memchr(cp, '/', len)) {
				pack_start = cp - line;
				goto prepare;
			}
		} else if (!strncmp(line + offset, "network ", 8)) {
			char *cp = strchr(line + offset + 8, ' ');
			char *cp2 = NULL;
			if (cp)
				cp = strchr(cp + 1, ' ');
			if (cp)
				cp2 = strchr(cp + 1, ' ');
			cp++;
			len = cp2 - cp;
			if (cp2 && memchr(cp, '/', len)) {
				pack_start = cp - line;
				goto prepare;
			}
		}
		return line;
prepare:
		pack_len = len;
		cached_line = ccs_strdup(line);
	}
unpack:
	{
		char *line = NULL;
		char *pos = cached_line + pack_start;
		char *cp = memchr(pos, '/', pack_len);
		unsigned int len = cp - pos;
		free(previous_line);
		previous_line = NULL;
		if (!cp) {
			previous_line = cached_line;
			cached_line = NULL;
			line = previous_line;
		} else if (pack_len == 1) {
			/* Ignore trailing empty word. */
			free(cached_line);
			cached_line = NULL;
		} else {
			/* Current string is "abc d/e/f ghi". */
			line = ccs_strdup(cached_line);
			previous_line = line;
			/* Overwrite "abc d/e/f ghi" with "abc d ghi". */
			memmove(line + pack_start + len, pos + pack_len,
				strlen(pos + pack_len) + 1);
			/* Overwrite "abc d/e/f ghi" with "abc e/f ghi". */
			cp++;
			memmove(pos, cp, strlen(cp) + 1);
			/* Forget "d/" component. */
			pack_len -= len + 1;
			/* Ignore leading and middle empty word. */
			if (!len)
				goto unpack;
		}
		return line;
	}
}

/**
 * ccs_check_remote_host - Check whether the remote host is running with the TOMOYO 1.8 kernel or not.
 *
 * Returns true if running with TOMOYO 1.8 kernel, false otherwise.
 */
_Bool ccs_check_remote_host(void)
{
	int major = 0;
	int minor = 0;
	int rev = 0;
	FILE *fp = ccs_open_read("version");
	if (!fp ||
	    fscanf(fp, "%u.%u.%u", &major, &minor, &rev) < 2 ||
	    major != 1 || minor != 8) {
		const u32 ip = ntohl(ccs_network_ip);
		fprintf(stderr, "Can't connect to %u.%u.%u.%u:%u\n",
			(u8) (ip >> 24), (u8) (ip >> 16),
			(u8) (ip >> 8), (u8) ip, ntohs(ccs_network_port));
		if (fp)
			fclose(fp);
		return false;
	}
	fclose(fp);
	return true;
}
