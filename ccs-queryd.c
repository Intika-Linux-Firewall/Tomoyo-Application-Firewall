/*
 * ccs-queryd.c
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
#include "ccstools.h"
#include <ncurses.h>

/*TUNED*READLINE.H************************************************************************************************************/
/****************************************************************************************************************************/
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
		int window_height;
		int c;
		int x;
		int y;
		int i;
		int ret_ignored;
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
		//c = ccs_getch2();
		c = '\n';
        ////////////////////////////////////////////////////////////////////////////////////
		if (ccs_query_fd != EOF)
			ret_ignored = write(ccs_query_fd, "\n", 1);
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
/****************************************************************************************************************************/
/****************************************************************************************************************************/

/* Prototypes */

static void ccs_printw(const char *fmt, ...)
	__attribute__ ((format(printf, 1, 2)));
static _Bool ccs_handle_query(unsigned int serial);

/* Utility functions */

static void ccs_printw(const char *fmt, ...)
{
	va_list args;
	int i;
	int len;
	char *buffer;
	va_start(args, fmt);
	len = vsnprintf((char *) &i, sizeof(i) - 1, fmt, args) + 16;
	va_end(args);
	buffer = ccs_malloc(len);
	va_start(args, fmt);
	len = vsnprintf(buffer, len, fmt, args);
	va_end(args);
	for (i = 0; i < len; i++) {
		addch(buffer[i]);
		refresh();
	}
	free(buffer);
}

static void ccs_send_keepalive(void)
{
	static time_t previous = 0;
	time_t now = time(NULL);
	if (previous != now || !previous) {
		int ret_ignored;
		previous = now;
		ret_ignored = write(ccs_query_fd, "\n", 1);
	}
}

/* Variables */

static unsigned short int ccs_retries = 0;

static FILE *ccs_domain_fp = NULL;
static int ccs_domain_policy_fd = EOF;
#define CCS_MAX_READLINE_HISTORY 20
static const char **ccs_readline_history = NULL;
static int ccs_readline_history_count = 0;
static char ccs_buffer[32768];
static char ccs_buffer_previous1[32768];
static char ccs_buffer_previous2[32768];
static char ccs_buffer_previous3[32768];
static int ccs_buffer_previous_answer1 = 2;
static int ccs_buffer_previous_answer2 = 2;
static int ccs_buffer_previous_answer3 = 2;


/* Main functions */

static _Bool ccs_handle_query(unsigned int serial)
{
	int c = 0;
	int y;
	int x;
	int ret_ignored;
	char *line = NULL;
	static unsigned int prev_pid = 0;
	unsigned int pid;
	char pidbuf[128];
	char *cp = strstr(ccs_buffer, " (global-pid=");
	if (!cp || sscanf(cp + 13, "%u", &pid) != 1) {
		ccs_printw("ERROR: Unsupported query.\n");
		return false;
	}
	cp = ccs_buffer + strlen(ccs_buffer);
	if (*(cp - 1) != '\n') {
		ccs_printw("ERROR: Unsupported query.\n");
		return false;
	}
    
	*(cp - 1) = '\0';
	if (pid != prev_pid) {
		if (prev_pid)
			ccs_printw("----------------------------------------"
				   "\n");
		prev_pid = pid;
	}
    
	ccs_printw("%s\n", ccs_buffer);

    /* Is this domain query? */
	if (strstr(ccs_buffer, "\n#"))
		goto not_domain_query;
	memset(pidbuf, 0, sizeof(pidbuf));
	snprintf(pidbuf, sizeof(pidbuf) - 1, "select Q=%u\n", serial);
	ccs_printw("Allow? ('Y'es/'N'o/'R'etry/'S'how policy/'A'dd to policy "
		   "and retry):");
	/*while (true) {
		c = ccs_getch2();
		if (c == 'Y' || c == 'y' || c == 'N' || c == 'n' || c == 'R' ||
		    c == 'r' || c == 'A' || c == 'a' || c == 'S' || c == 's')
			break;
		ccs_send_keepalive();
	}*/
	//ccs_printw("%c\n", c);
    ccs_printw("\n");
    
    //Delegare answer to gui
    
    //Remove date and time from request varialbe 
    const char* substringcurrent = ccs_buffer + 22;
    const char* substring1 = ccs_buffer_previous1 + 22;
    const char* substring2 = ccs_buffer_previous2 + 22;
    const char* substring3 = ccs_buffer_previous3 + 22;
    
    //intika patch 
    char message[32768] = "";
    int xresult = 2;
    strcat(message, "(while ! wmctrl -F -a 'CCS-Tomoto-Query' -b add,above;do sleep 1;done) & ");
    strcat(message, "zenity --timeout 15 --question --no-markup --width=500 --height=250 --title=CCS-Tomoto-Query --cancel-label='No (Default 15s)' --text='Tomoto :\n");
    strcat(message, ccs_buffer);
    strcat(message, " ?'");
    
    //To avoid repetition - check 3 past time 
    if (strcmp(substring1, substringcurrent) != 0) {
        if (strcmp(substring2, substringcurrent) != 0) {
            if (strcmp(substring3, substringcurrent) != 0) {
                xresult = system(message);
                ccs_send_keepalive();
                //copy past 2 result to 3
                strcpy(ccs_buffer_previous3,ccs_buffer_previous2);
                ccs_buffer_previous_answer3 = ccs_buffer_previous_answer2;
                //copy past 1 result to 2
                strcpy(ccs_buffer_previous2,ccs_buffer_previous1);
                ccs_buffer_previous_answer2 = ccs_buffer_previous_answer1;
                //copy past 0 result to 1
                strcpy(ccs_buffer_previous1,ccs_buffer);
                ccs_buffer_previous_answer1 = xresult;
            } else {
                xresult = ccs_buffer_previous_answer3;
            }
        } else {
            xresult = ccs_buffer_previous_answer2;
        }
    } else {
        xresult = ccs_buffer_previous_answer1;
    }
    //xresult = system(message);
    //ccs_send_keepalive();
    
    //Result
    //Yes     = 0
    //No      = 256   
    //Timeout = 1280
    ccs_printw("\n");
    ccs_printw("\n%d\n",xresult);
    ccs_printw("\n");
    ccs_printw("Yes     = 0\n");
    ccs_printw("No      = 256\n");
    ccs_printw("Timeout = 1280\n");
    ccs_printw("Result  = %d\n",xresult);
    
    c = 'n';
    if (xresult == 0) {
        c = 'a';
    }
    ccs_printw("%c\n", c);
    
	if (c == 'S' || c == 's') {
		if (ccs_network_mode) {
			fprintf(ccs_domain_fp, "%s", pidbuf);
			fputc(0, ccs_domain_fp);
			fflush(ccs_domain_fp);
			rewind(ccs_domain_fp);
			while (1) {
				char c;
				if (fread(&c, 1, 1, ccs_domain_fp) != 1 || !c)
					break;
				addch(c);
				refresh();
				ccs_send_keepalive();
			}
		} else {
			ret_ignored = write(ccs_domain_policy_fd, pidbuf,
					    strlen(pidbuf));
			while (1) {
				int i;
				int len = read(ccs_domain_policy_fd,
					       ccs_buffer,
					       sizeof(ccs_buffer) - 1);
				if (len <= 0)
					break;
				for (i = 0; i < len; i++) {
					addch(ccs_buffer[i]);
					refresh();
				}
				ccs_send_keepalive();
			}
		}
		c = 'r';
	}

	/* Append to domain policy. */
	if (c != 'A' && c != 'a')
		goto not_append;
	c = 'r';
	getyx(stdscr, y, x);
	cp = strrchr(ccs_buffer, '\n');
	if (!cp)
		return false;
	*cp++ = '\0';
	ccs_initial_readline_data = cp;
	ccs_readline_history_count =
		ccs_add_history(cp, ccs_readline_history,
				ccs_readline_history_count,
				CCS_MAX_READLINE_HISTORY);
    
    ccs_printw("\n");
    ccs_printw("%s\n", ccs_initial_readline_data);
    ccs_printw("\n");
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
	line = ccs_readline(y, 0, "Enter new entry> ", ccs_readline_history,
			    ccs_readline_history_count, 128000, 8);
	scrollok(stdscr, TRUE);
	ccs_printw("\n");
	if (!line || !*line) {
		ccs_printw("None added.\n");
		goto not_append;
	}
	ccs_readline_history_count =
		ccs_add_history(line, ccs_readline_history,
				ccs_readline_history_count,
				CCS_MAX_READLINE_HISTORY);
	if (ccs_network_mode) {
		fprintf(ccs_domain_fp, "%s%s\n", pidbuf, line);
		fflush(ccs_domain_fp);
	} else {
		ret_ignored = write(ccs_domain_policy_fd, pidbuf,
				    strlen(pidbuf));
		ret_ignored = write(ccs_domain_policy_fd, line, strlen(line));
		ret_ignored = write(ccs_domain_policy_fd, "\n", 1);
	}
	ccs_printw("Added '%s'.\n", line);
not_append:
	free(line);
write_answer:
	/* Write answer. */
	if (c == 'Y' || c == 'y' || c == 'A' || c == 'a')
		c = 1;
	else if (c == 'R' || c == 'r')
		c = 3;
	else
		c = 2;
	snprintf(ccs_buffer, sizeof(ccs_buffer) - 1, "A%u=%u\n", serial, c);
	ret_ignored = write(ccs_query_fd, ccs_buffer, strlen(ccs_buffer));
	ccs_printw("\n");
	return true;
not_domain_query:
	ccs_printw("Allow? ('Y'es/'N'o/'R'etry):");
	/*while (true) {
		c = ccs_getch2();
		if (c == 'Y' || c == 'y' || c == 'N' || c == 'n' ||
		    c == 'R' || c == 'r')
			break;
		ccs_send_keepalive();
	}*/
    
    //Intika TODO Incluse Domain Request 
    c = 'n';
    //Intika TODO Incluse Domain Request 
    
	ccs_printw("%c\n", c);
	goto write_answer;
}

int main(int argc, char *argv[])
{
	if (argc == 1)
		goto ok;
	{
		char *cp = strchr(argv[1], ':');
		if (cp) {
			*cp++ = '\0';
			ccs_network_ip = inet_addr(argv[1]);
			ccs_network_port = htons(atoi(cp));
			ccs_network_mode = true;
			if (!ccs_check_remote_host())
				return 1;
			goto ok;
		}
	}
	printf("Usage: %s [remote_ip:remote_port]\n\n", argv[0]);
	printf("This program is used for granting access requests manually."
	       "\n");
	printf("This program shows access requests that are about to be "
	       "rejected by the kernel's decision.\n");
	printf("If you answer before the kernel's decision takes effect, your "
	       "decision will take effect.\n");
	printf("You can use this program to respond to accidental access "
	       "requests triggered by non-routine tasks (such as restarting "
	       "daemons after updating).\n");
	printf("To terminate this program, use 'Ctrl-C'.\n");
	return 0;
ok:
	if (ccs_network_mode) {
		ccs_query_fd = ccs_open_stream("proc:query");
		ccs_domain_fp = ccs_open_write(CCS_PROC_POLICY_DOMAIN_POLICY);
	} else {
		ccs_query_fd = open(CCS_PROC_POLICY_QUERY, O_RDWR);
		ccs_domain_policy_fd = open(CCS_PROC_POLICY_DOMAIN_POLICY,
					    O_RDWR);
	}
	if (ccs_query_fd == EOF) {
		fprintf(stderr,
			"You can't run this utility for this kernel.\n");
		return 1;
	} else if (!ccs_network_mode && write(ccs_query_fd, "", 0) != 0) {
		fprintf(stderr, "You need to register this program to %s to "
			"run this program.\n", CCS_PROC_POLICY_MANAGER);
		return 1;
	}
	ccs_readline_history = ccs_malloc(CCS_MAX_READLINE_HISTORY *
					  sizeof(const char *));
	ccs_send_keepalive();
	initscr();
	cbreak();
	noecho();
	nonl();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	clear();
	refresh();
	scrollok(stdscr, TRUE);
	if (ccs_network_mode) {
		const u32 ip = ntohl(ccs_network_ip);
		ccs_printw("Monitoring /proc/ccs/query via %u.%u.%u.%u:%u.",
			   (u8) (ip >> 24), (u8) (ip >> 16), (u8) (ip >> 8),
			   (u8) ip, ntohs(ccs_network_port));
	} else
		ccs_printw("Monitoring /proc/ccs/query .");
	ccs_printw(" Press Ctrl-C to terminate.\n\n");
	while (true) {
		unsigned int serial;
		char *cp;
		/* Wait for query and read query. */
		memset(ccs_buffer, 0, sizeof(ccs_buffer));
		if (ccs_network_mode) {
			int i;
			int ret_ignored;
			ret_ignored = write(ccs_query_fd, "", 1);
			for (i = 0; i < sizeof(ccs_buffer) - 1; i++) {
				if (read(ccs_query_fd, ccs_buffer + i, 1) != 1)
					break;
				if (!ccs_buffer[i])
					goto read_ok;
			}
			break;
		} else {
			struct pollfd pfd;
			pfd.fd = ccs_query_fd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			poll(&pfd, 1, -1);
			if (!(pfd.revents & POLLIN))
				continue;
			if (read(ccs_query_fd, ccs_buffer,
				 sizeof(ccs_buffer) - 1) <= 0)
				continue;
		}
read_ok:
		cp = strchr(ccs_buffer, '\n');
		if (!cp)
			continue;
		*cp = '\0';

		/* Get query number. */
		if (sscanf(ccs_buffer, "Q%u-%hu", &serial, &ccs_retries) != 2)
			continue;
		memmove(ccs_buffer, cp + 1, strlen(cp + 1) + 1);

		/* Clear pending input. */;
		timeout(0);
		while (true) {
			int c = ccs_getch2();
			if (c == EOF || c == ERR)
				break;
		}
		timeout(1000);
		if (ccs_handle_query(serial))
			continue;
		break;
	}
	endwin();
	return 0;
}

