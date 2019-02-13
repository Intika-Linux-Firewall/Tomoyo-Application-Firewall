/*
 * ccstools.h
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
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <asm/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <poll.h>

#define s8 __s8
#define u8 __u8
#define u16 __u16
#define u32 __u32
#define true  1
#define false 0

/***** CONSTANTS DEFINITION START *****/

#define CCS_PROC_POLICY_DIR              "/proc/ccs/"
#define CCS_PROC_POLICY_DOMAIN_POLICY    "/proc/ccs/domain_policy"
#define CCS_PROC_POLICY_EXCEPTION_POLICY "/proc/ccs/exception_policy"
#define CCS_PROC_POLICY_AUDIT            "/proc/ccs/audit"
#define CCS_PROC_POLICY_MANAGER          "/proc/ccs/manager"
#define CCS_PROC_POLICY_STAT             "/proc/ccs/stat"
#define CCS_PROC_POLICY_PROCESS_STATUS   "/proc/ccs/.process_status"
#define CCS_PROC_POLICY_PROFILE          "/proc/ccs/profile"
#define CCS_PROC_POLICY_QUERY            "/proc/ccs/query"

/***** CONSTANTS DEFINITION END *****/

/***** STRUCTURES DEFINITION START *****/

struct ccs_path_info {
	const char *name;
	u32 hash;           /* = ccs_full_name_hash(name, total_len) */
	u16 total_len;      /* = strlen(name)                        */
	u16 const_len;      /* = ccs_const_part_length(name)         */
	_Bool is_dir;       /* = ccs_strendswith(name, "/")          */
	_Bool is_patterned; /* = const_len < total_len               */
};

struct ccs_ip_address_entry {
	u8 min[16];
	u8 max[16];
	_Bool is_ipv6;
};

struct ccs_number_entry {
	unsigned long min;
	unsigned long max;
};

struct ccs_domain_info {
	const struct ccs_path_info *domainname;
	const struct ccs_path_info **string_ptr;
	int string_count;
	u8 profile;
	_Bool profile_assigned;
	u8 group;
};

struct ccs_domain_policy {
	struct ccs_domain_info *list;
	int list_len;
	unsigned char *list_selected;
};

struct ccs_task_entry {
	pid_t pid;
	pid_t ppid;
	char *name;
	char *domain;
	u8 profile;
	_Bool selected;
	int index;
	int depth;
};

/***** STRUCTURES DEFINITION END *****/

/***** PROTOTYPES DEFINITION START *****/

FILE *ccs_open_read(const char *filename);
FILE *ccs_open_write(const char *filename);
_Bool ccs_check_remote_host(void);
_Bool ccs_close_write(FILE *fp);
_Bool ccs_correct_domain(const char *domainname);
_Bool ccs_correct_path(const char *filename);
_Bool ccs_correct_word(const char *string);
_Bool ccs_decode(const char *ascii, char *bin);
_Bool ccs_domain_def(const char *domainname);
_Bool ccs_move_proc_to_file(const char *src, const char *dest);
_Bool ccs_path_matches_pattern(const struct ccs_path_info *pathname0,
			       const struct ccs_path_info *pattern0);
_Bool ccs_pathcmp(const struct ccs_path_info *a,
		  const struct ccs_path_info *b);
_Bool ccs_str_starts(char *str, const char *begin);
char *ccs_freadline(FILE *fp);
char *ccs_freadline_unpack(FILE *fp);
char *ccs_shprintf(const char *fmt, ...)
	__attribute__ ((format(printf, 1, 2)));
char *ccs_strdup(const char *string);
const char *ccs_domain_name(const struct ccs_domain_policy *dp,
			    const int index);
const struct ccs_path_info *ccs_savename(const char *name);
int ccs_add_string_entry(struct ccs_domain_policy *dp, const char *entry,
			 const int index);
int ccs_assign_domain(struct ccs_domain_policy *dp, const char *domainname);
int ccs_del_string_entry(struct ccs_domain_policy *dp, const char *entry,
			 const int index);
int ccs_find_domain(const struct ccs_domain_policy *dp,
		    const char *domainname0);
int ccs_find_domain_by_ptr(struct ccs_domain_policy *dp,
			   const struct ccs_path_info *domainname);
int ccs_open_stream(const char *filename);
int ccs_parse_ip(const char *address, struct ccs_ip_address_entry *entry);
int ccs_parse_number(const char *number, struct ccs_number_entry *entry);
int ccs_string_compare(const void *a, const void *b);
int ccs_write_domain_policy(struct ccs_domain_policy *dp, const int fd);
struct ccs_path_group_entry *ccs_find_path_group(const char *group_name);
void *ccs_malloc(const size_t size);
void *ccs_realloc(void *ptr, const size_t size);
void *ccs_realloc2(void *ptr, const size_t size);
void ccs_clear_domain_policy(struct ccs_domain_policy *dp);
void ccs_delete_domain(struct ccs_domain_policy *dp, const int index);
void ccs_fill_path_info(struct ccs_path_info *ptr);
void ccs_fprintf_encoded(FILE *fp, const char *ccs_pathname);
void ccs_get(void);
void ccs_handle_domain_policy(struct ccs_domain_policy *dp, FILE *fp,
			      _Bool is_write);
void ccs_normalize_line(char *buffer);
void ccs_put(void);
void ccs_read_domain_policy(struct ccs_domain_policy *dp,
			    const char *filename);
void ccs_read_process_list(_Bool show_all);

extern _Bool ccs_freadline_raw;
extern _Bool ccs_network_mode;
extern int ccs_task_list_len;
extern struct ccs_task_entry *ccs_task_list;
extern u16 ccs_network_port;
extern u32 ccs_network_ip;

/***** PROTOTYPES DEFINITION END *****/
