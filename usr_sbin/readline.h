/*
 * readline.h
 *
 * TOMOYO Linux's utilities.
 *
 * Copyright (C) 2005-2011  NTT DATA CORPORATION
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
#include <ncurses.h>

static int ccs_getch0(void)
{
	static int enter_key = EOF;
	int c;
again:
	c = getch();
	if (c == 127 || c == 8)
		c = KEY_BACKSPACE;
	/* syslog(LOG_INFO, "ccs_getch0='%c' (%d)\n", c, c); */
	if (c == '\r' || c == '\n') {
		if (enter_key == EOF)
			enter_key = c;
		else if (c != enter_key)
			goto again;
	}
	return c;
}

static int ccs_getch2(void)
{
	static int c0 = 0;
	static int c1 = 0;
	static int c2 = 0;
	static int c3 = 0;
	static int len = 0;
	if (len > 0) {
		c0 = c1;
		c1 = c2;
		c2 = c3;
		len--;
		return c0;
	}
	c0 = ccs_getch0();
	if (c0 != 0x1B)
		return c0;
	c1 = ccs_getch0();
	if (c1 != '[') {
		len = 1;
		return c0;
	}
	c2 = ccs_getch0();
	if (c2 < '1' || c2 > '6') {
		len = 2;
		return c0;
	}
	c3 = ccs_getch0();
	if (c3 != '~') {
		len = 3;
		return c0;
	}
	/* syslog(LOG_INFO, "ccs_getch2='%c'\n", c2); */
	switch (c2) {
	case '1':
		return KEY_HOME;
	case '2':
		return KEY_IC;
	case '3':
		return KEY_DC;
	case '4':
		return KEY_END;
	case '5':
		return KEY_PPAGE;
	case '6':
		return KEY_NPAGE;
	}
	return 0;
}

static int ccs_add_history(const char *buffer, const char **history,
			   const int history_count, const int max_history)
{
	char *cp = buffer ? strdup(buffer) : NULL;
	if (!cp)
		return history_count;
	if (history_count && !strcmp(history[history_count - 1], cp)) {
		free(cp);
		return history_count;
	}
	if (history_count < max_history) {
		history[history_count] = cp;
		return history_count + 1;
	} else if (max_history) {
		int i;
		free((char *) history[0]);
		for (i = 0; i < history_count - 1; i++)
			history[i] = history[i + 1];
		history[history_count - 1] = cp;
		return history_count;
	}
	return 0;
}

static int ccs_query_fd = EOF;
static char *ccs_initial_readline_data = NULL;

static char *ccs_readline(const int start_y, const int start_x,
			  const char *prompt, const char *history[],
			  const int history_count, const int max_length,
			  const int scroll_width)
{
	const int prompt_len = prompt ? strlen(prompt) : 0;
	int buffer_len = 0;
	int line_pos = 0;
	int cur_pos = 0;
	int history_pos = 0;
	_Bool tmp_saved = false;
	static char *buffer = NULL;
	static char *tmp_buffer = NULL;
	{
		int i;
		for (i = 0; i < history_count; i++)
			if (!history[i])
				return NULL;
	}
	{
		char *tmp;
		tmp = realloc(buffer, max_length + 1);
		if (!tmp)
			return NULL;
		buffer = tmp;
		tmp = realloc(tmp_buffer, max_length + 1);
		if (!tmp)
			return NULL;
		tmp_buffer = tmp;
		memset(buffer, 0, max_length + 1);
		memset(tmp_buffer, 0, max_length + 1);
	}
	move(start_y, start_x);
	history_pos = history_count;
	if (ccs_initial_readline_data) {
		strncpy(buffer, ccs_initial_readline_data, max_length);
		buffer_len = strlen(buffer);
		ungetch(KEY_END);
	}
	while (true) {
		int window_width;
		int window_height; //old code - not used but needed
		int c;
		int x; //old code - not used but needed
		int y;
		int i;
		//int ret_ignored; //old code
		getmaxyx(stdscr, window_height, window_width);
		window_width -= prompt_len;
		getyx(stdscr, y, x);
		move(y, 0);
		while (cur_pos > window_width - 1) {
			cur_pos--;
			line_pos++;
		}
		if (prompt_len)
			printw("%s", prompt);
		for (i = line_pos; i < line_pos + window_width; i++) {
			if (i < buffer_len)
				addch(buffer[i]);
			else
				break;
		}
		clrtoeol();
		move(y, cur_pos + prompt_len);
		refresh();
        ////////////////////////////////////////////////////////////////////////////////////
        //Disable input and send back retrurn directly
		//c = ccs_getch2();
		c = '\n';
        ////////////////////////////////////////////////////////////////////////////////////
		if (ccs_query_fd != EOF)
			//ret_ignored = write(ccs_query_fd, "\n", 1); //old code
			write(ccs_query_fd, "\n", 1);
		if (c == 4) { /* Ctrl-D */
			if (!buffer_len)
				buffer_len = -1;
			break;
		} else if (c == KEY_IC) {
			scrollok(stdscr, TRUE);
			printw("\n");
			for (i = 0; i < history_count; i++)
				printw("%d: '%s'\n", i, history[i]);
			scrollok(stdscr, FALSE);
		} else if (c >= 0x20 && c <= 0x7E &&
			   buffer_len < max_length - 1) {
			for (i = buffer_len - 1; i >= line_pos + cur_pos; i--)
				buffer[i + 1] = buffer[i];
			buffer[line_pos + cur_pos] = c;
			buffer[++buffer_len] = '\0';
			if (cur_pos < window_width - 1)
				cur_pos++;
			else
				line_pos++;
		} else if (c == '\r' || c == '\n') {
			break;
		} else if (c == KEY_BACKSPACE) {
			if (line_pos + cur_pos) {
				buffer_len--;
				for (i = line_pos + cur_pos - 1;
				     i < buffer_len; i++)
					buffer[i] = buffer[i + 1];
				buffer[buffer_len] = '\0';
				if (line_pos >= scroll_width && cur_pos == 0) {
					line_pos -= scroll_width;
					cur_pos += scroll_width - 1;
				} else if (cur_pos) {
					cur_pos--;
				} else if (line_pos) {
					line_pos--;
				}
			}
		} else if (c == KEY_DC) {
			if (line_pos + cur_pos < buffer_len) {
				buffer_len--;
				for (i = line_pos + cur_pos; i < buffer_len;
				     i++)
					buffer[i] = buffer[i + 1];
				buffer[buffer_len] = '\0';
			}
		} else if (c == KEY_UP) {
			if (history_pos) {
				if (!tmp_saved) {
					tmp_saved = true;
					strncpy(tmp_buffer, buffer,
						max_length);
				}
				history_pos--;
				strncpy(buffer, history[history_pos],
					max_length);
				buffer_len = strlen(buffer);
				goto end_key;
			}
		} else if (c == KEY_DOWN) {
			if (history_pos < history_count - 1) {
				history_pos++;
				strncpy(buffer, history[history_pos],
					max_length);
				buffer_len = strlen(buffer);
				goto end_key;
			} else if (tmp_saved) {
				tmp_saved = false;
				history_pos = history_count;
				strncpy(buffer, tmp_buffer, max_length);
				buffer_len = strlen(buffer);
				goto end_key;
			}
		} else if (c == KEY_HOME) {
			cur_pos = 0;
			line_pos = 0;
		} else if (c == KEY_END) {
			goto end_key;
		} else if (c == KEY_LEFT) {
			if (line_pos >= scroll_width && cur_pos == 0) {
				line_pos -= scroll_width;
				cur_pos += scroll_width - 1;
			} else if (cur_pos) {
				cur_pos--;
			} else if (line_pos) {
				line_pos--;
			}
		} else if (c == KEY_RIGHT) {
			if (line_pos + cur_pos < buffer_len) {
				if (cur_pos < window_width - 1)
					cur_pos++;
				else if (line_pos + cur_pos <
					 buffer_len - scroll_width &&
					 cur_pos >= scroll_width - 1) {
					cur_pos -= scroll_width - 1;
					line_pos += scroll_width;
				} else {
					line_pos++;
				}
			}
		}
		continue;
end_key:
		cur_pos = buffer_len;
		line_pos = 0;
		if (cur_pos > window_width - 1) {
			line_pos = buffer_len - (window_width - 1);
			cur_pos = window_width - 1;
		}
	}
	if (buffer_len == -1)
		return NULL;
	ccs_normalize_line(buffer);
	return strdup(buffer);
}
