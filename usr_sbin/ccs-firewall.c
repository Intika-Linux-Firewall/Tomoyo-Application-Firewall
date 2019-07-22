/*
 * ccs-firewall.c
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
#include "readline.h"
#include <sys/time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main variables
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static unsigned short int ccs_retries = 0;
static FILE *ccs_domain_fp = NULL;
#define CCS_MAX_READLINE_HISTORY 20
static const char **ccs_readline_history = NULL;
static struct timeval start_time_allowance;
static bool firstrun = true;
static bool allownLearn = false;
static char ccs_buffer[32768] = "";
static char ccs_buffer_cleaned[32768] = "";
static char ccs_buffer_previous1[32768] = "";
static char ccs_buffer_previous2[32768] = "";
static char ccs_buffer_previous3[32768] = "";
static char message_question[32768] = "";
static char extracted_domain[32768] = "";
static int ccs_buffer_previous_answer1 = 2;
static int ccs_buffer_previous_answer2 = 2;
static int ccs_buffer_previous_answer3 = 2;
static int how_many_auto_query_repeat = 0;
static int ccs_readline_history_count = 0;
static int ccs_domain_policy_fd = EOF;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ccs_printw(const char *fmt, ...)
	__attribute__ ((format(printf, 1, 2)));

static _Bool ccs_handle_query(unsigned int serial);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Printf
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Keep alive
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ccs_send_keepalive(void)
{
	static time_t previous = 0;
	time_t now = time(NULL);
	if (previous != now || !previous) {
		previous = now;
		//old code
        //ret_ignored = write(ccs_query_fd, "\n", 1);
		write(ccs_query_fd, "\n", 1);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Popup Warning 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int popup_warning(const char *message, const char *timeout)
{
    char tmpmessage[2000] = "";
    int result = 0;
    strcat(tmpmessage, "(while ! wmctrl -F -a 'CCS-Tomoyo-Warning' -b add,above;do sleep 1;done) >/dev/null 2>&1 & ");
    strcat(tmpmessage, "zenity --timeout ");
    strcat(tmpmessage, timeout);
    strcat(tmpmessage, " --warning --no-markup --width=250 --height=50 --ok-label='Ok (");
    strcat(tmpmessage, timeout);
    strcat(tmpmessage, "s)' ");
    strcat(tmpmessage, "--title=CCS-Tomoyo-Query-Warning --text='");
    strcat(tmpmessage, message);
    strcat(tmpmessage, "' ");
    strcat(tmpmessage, ">/dev/null 2>&1");
    result = system(tmpmessage);
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Extract Domain
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char * extract_domain(const char *ccs_buffer, const char *debugStr) 
{
    //Vars
    int firstlinesize = 1;
    int secondlinesize = 1;
    extracted_domain[0] = '\0';

    //Get First Line Position
    while ((ccs_buffer[firstlinesize] != '\n') && (firstlinesize < 28500)) {
        firstlinesize++;
    } firstlinesize++;
    
    //Prepare Second Line
    const char* secondlinePos = ccs_buffer + firstlinesize;

    //Extract Domain With 3rd Line
    strcat(extracted_domain, secondlinePos);

    //Get Second Line Lenth
    while ((extracted_domain[secondlinesize] != '\n') && (firstlinesize < 28500)) {
        secondlinesize++;
    }

    //Clear Third Line 
    extracted_domain[secondlinesize] = '\0'; //Insert a terminating null character 
    
    //Clear Third Line Old Code : not needed, null make pointeur kill the rest and break chain
    //secondlinesize++; while ((domainString[secondlinesize] != '\n') && (firstlinesize < 28500)) {domainString[secondlinesize] = NULL;  //Or '\0'
    //secondlinesize++;}
    
    if (strcmp(debugStr, "true") == 0) {
        ccs_printw("\n");
        ccs_printw(" Extracted Domain :\n");
        ccs_printw(" %s\n", extracted_domain);
        ccs_printw("\n");
    } else {
        ccs_printw(" Extracted Domain                 = Ok\n");
    }
    
    return extracted_domain;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - String Helper
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void removeSpacesBetween(char *str, int x, int y)
{
    int count = 0; 
  
    // Traverse the given string.
    for (int i = 0; str[i]; i++) 
        if ((str[i] != ' ') || (i < x || i > y))
            str[count++] = str[i];
    str[count] = '\0'; 
}

static void cleanString(const char *ccs_buffer) 
{
    //Clean message
    int loop=0;
    int loopMarker=0;
    ccs_buffer_cleaned[0] = '\0';
    strcpy(ccs_buffer_cleaned, ccs_buffer);
    
    while (ccs_buffer_cleaned[loop] != '\0') {
        
        //Remove (task={ pid) bracelet 
        if (// Remove (task={ pid) bracelet ------------------------------------------------------------------------------------------
            (ccs_buffer_cleaned[loop] == 't') && (ccs_buffer_cleaned[loop+1] == 'a') 
            && (ccs_buffer_cleaned[loop+2] == 's') && (ccs_buffer_cleaned[loop+3] == 'k') 
            && (ccs_buffer_cleaned[loop+4] == '=') && (ccs_buffer_cleaned[loop+5] == '{')
           ){ // ---------------------------------------------------------------------------------------------------------------------
            loop = loop+5; 
            ccs_buffer_cleaned[loop] = ' ';
            removeSpacesBetween(ccs_buffer_cleaned, loop, loop+1);
        }
            
        if (// Keep first 2 chat # -AND- limit first line to 120 chars -AND- remove thigs after ppid... on the line... ---------------
            ((ccs_buffer_cleaned[loop] == '#') && (loop > 22)) || (loop == 120) || 
            ((ccs_buffer_cleaned[loop] == 'p') && (ccs_buffer_cleaned[loop+1] == 'p') 
             && (ccs_buffer_cleaned[loop+2] == 'i') && (ccs_buffer_cleaned[loop+3] == 'd') && (ccs_buffer_cleaned[loop+4] == '='))
           ) {// ---------------------------------------------------------------------------------------------------------------------
            ccs_buffer_cleaned[loop] = ' ';
            loopMarker = loop;
            while ((ccs_buffer_cleaned[loop] != '\0') && (ccs_buffer_cleaned[loop] != '\n')) {
                ccs_buffer_cleaned[loop] = ' ';
                loop++;
            }
            removeSpacesBetween(ccs_buffer_cleaned, loopMarker, loop-1);
        }
        loop++;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Prepare Main Question
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void prepare_main_question(const char *ccs_buffer, const char *timeout)
{
    //Clean message
    cleanString(ccs_buffer); //use ccs_buffer_cleaned afterward
    
    //Vars
    message_question[0] = '\0';
    
    //Prepare question
    strcat(message_question, "(while ! wmctrl -F -a 'CCS-Tomoyo-Query' -b add,above;do sleep 1;done) >/dev/null 2>&1 & ");
    strcat(message_question, "ans=$(zenity --timeout ");
    strcat(message_question, timeout);
    strcat(message_question, " ");
    strcat(message_question, "--question --no-markup --width=675 --height=150 --ellipsize --switch ");
    strcat(message_question, "--title=CCS-Tomoyo-Query ");
    strcat(message_question, "--extra-button 'Allow & Learn' "); // >>>>>>>>>>>>>>>>                                  ------- A (add policy)
    strcat(message_question, "--extra-button 'Allow All & Save' "); // >>>>>>>>>>>>> change_profile_policy to 2 + ccs ------- Y
    //strcat(message_question, "--extra-button 'Allow All' "); // >>>>>>>>>>>>>>>>>>>> change_profile_policy to 2       ------- Y
    strcat(message_question, "--extra-button 'Allow' "); // >>>>>>>>>>>>>>>>>>>>>>>>                                  ------- Y
    strcat(message_question, "--extra-button 'Deny ("); // >>>>>>>>>>>>>>>>>>>>>>>>>                                  ------- N (deny)
    strcat(message_question, timeout);
    strcat(message_question, "s)' ");  
    strcat(message_question, "--extra-button 'Deny All' "); // >>>>>>>>>>>>>>>>>>>>> change_profile_policy to 8       ------- N
    strcat(message_question, "--text='Tomoyo :\n");
    strcat(message_question, ccs_buffer_cleaned);
    strcat(message_question, " ?' ");
    strcat(message_question, "2>&1)");
    
    //Add bash suite
    strcat(message_question, " ; level=$?");
    strcat(message_question, " ; if [[ $ans = *\"Allow & Learn\"* ]]; then exit 100 ; fi "); // ------- 100  25600
    strcat(message_question, " ; if [[ $ans = *\"Allow All & Save\"* ]]; then exit 200 ; fi "); // ---- 200  51200
    strcat(message_question, " ; if [[ $ans = *\"Allow All\"* ]]; then exit 300 ; fi "); // ----------- 300  11264
    strcat(message_question, " ; if [[ $ans = *\"Allow\"* ]]; then exit 400 ; fi "); // --------------- 400  36864
    strcat(message_question, " ; if [[ $ans = *\"Deny All\"* ]]; then exit 500 ; fi "); // ------------ 500  62464
    strcat(message_question, " ; if [[ $ans = *\"Deny (\"* ]]; then exit 600 ; fi "); // -------------- 600  22528
    strcat(message_question, " ; if [ $level -eq 0 ]; then exit 1000 ; fi "); // ---------------------- 1000 59392 The zenity command worked
    strcat(message_question, " ; if [ $level -eq 1 ]; then exit 1000 ; fi "); // ---------------------- 1000 59392 The zenity command worked
    strcat(message_question, " ; if [ $level -eq 5 ]; then exit 2000 ; fi "); // ---------------------- 1000 53248 The zenity command timeout
    strcat(message_question, " ; if [ $level -ne 1 ]; then exit 3000 ; fi ;"); // --------------------- 3000 47104 The main zenity command did not worked 
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Popup Question
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int popup_question(const char *message, const char *timeout)
{
    char tmpmessage[2000] = "";
    int result = 0;
    
    //Prepare question
    strcat(tmpmessage, "(while ! wmctrl -F -a 'CCS-Tomoyo-Query' -b add,above;do sleep 1;done) >/dev/null 2>&1 & ");
    strcat(tmpmessage, "ans=$(zenity --timeout ");
    strcat(tmpmessage, timeout);
    strcat(tmpmessage, " ");
    strcat(tmpmessage, "--question --no-markup --width=250 --height=50 --switch ");
    strcat(tmpmessage, "--title=CCS-Tomoyo-Query ");
    strcat(tmpmessage, "--extra-button 'No (");
    strcat(tmpmessage, timeout);
    strcat(tmpmessage, "s)' ");
    strcat(tmpmessage, "--extra-button 'Yes' ");
    strcat(tmpmessage, "--text='");
    strcat(tmpmessage, message);       
    strcat(tmpmessage, "' ");
    strcat(tmpmessage, "2>&1)");
    
    //Add bash suite
    strcat(tmpmessage, " ; level=$?");
    strcat(tmpmessage, " ; if [[ $ans = *\"Yes\"* ]]; then exit 100 ; fi "); // ----------------- 100  25600
    strcat(tmpmessage, " ; if [[ $ans = *\"No (\"* ]]; then exit 200 ; fi "); // ---------------- 200  51200
    strcat(tmpmessage, " ; if [ $level -eq 0 ]; then exit 1000 ; fi "); // ---------------------- 1000 59392 The zenity command worked
    strcat(tmpmessage, " ; if [ $level -eq 1 ]; then exit 1000 ; fi "); // ---------------------- 1000 59392 The zenity command worked
    strcat(tmpmessage, " ; if [ $level -eq 5 ]; then exit 2000 ; fi "); // ---------------------- 1000 53248 The zenity command timeout
    strcat(tmpmessage, " ; if [ $level -ne 1 ]; then exit 3000 ; fi ;"); // --------------------- 3000 47104 The main zenity command did not worked  
    
    //Exec question
    result = system(tmpmessage);
    
    //Result
    ccs_printw("\n");
    ccs_printw(" Question :\n");
    ccs_printw(" Yes                              = 25600 \n");
    ccs_printw(" No                               = 51200\n");
    //ccs_printw(" Timeout                          = 53248\n");
    ccs_printw(" ----------------------------------------\n");
    ccs_printw(" Result                           = %d\n",result);
    ccs_printw("\n");
    
    //Return result
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - TODO ADD WINDOW TO DISPLAY HISTORY //a chaque fois que je fait un print le rajouter dans une varialbe 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - TODO ADD CLEAR LOG 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - TODO Tray Icon
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Save policy 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int save_policy(void)
{
    int xxresult = 0;

    //Exec save
    xxresult=system("ccs-savepolicy >/dev/null 2>&1");

    //Result
    ccs_printw("\n");
    ccs_printw(" Save Policy :\n");
    ccs_printw(" Ok                               = 0 ?\n");
    ccs_printw(" Nok                              = 256\n");
    ccs_printw(" ----------------------------------------\n");
    ccs_printw(" Result                           = %d\n",xxresult);
    ccs_printw("\n");

    if (xxresult != 0) {
        ccs_printw("\n");
        ccs_printw(" ----------------------------------------\n");
        ccs_printw("\nPolicy Saved                    = NOK !\n");
        ccs_printw(" ----------------------------------------\n");
        ccs_printw("\n");
        popup_warning("Tomoyo : Failed to save policy", "45");
    } else {
        ccs_printw("\n");
        ccs_printw(" ----------------------------------------\n");
        ccs_printw("\nPolicy Saved                    = OK !\n");
        ccs_printw(" ----------------------------------------\n");
        ccs_printw("\n");
    }

    //system("sleep 1 >/dev/null 2>&1"); //To be removed
    
    return xxresult;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Save policy question
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*static int save_policy_question(void)
{            
    //Ask to save
    if (popup_question("Tomoyo :\n\nDo you want to save policy and settings ?", "45") == 25600) {
        save_policy();
        return 0;
    } else {
        return 1;
    }
}*/

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Send notification
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int send_notification(const char *ccs_buffer)
{        
    int result = 0;
    char messagenotify[32768] = "";
    
    //Clean message
    cleanString(ccs_buffer); //use ccs_buffer_cleaned afterward
    
    //Prepare norification - Get current x use 
    strcat(messagenotify, "sudo -u $(ps auxw | grep -i screen | grep -v grep | cut -f 1 -d ' ') ");
    strcat(messagenotify, "notify-send -a Tomoyo -i cs-firewall Tomoyo '");
    strcat(messagenotify, ccs_buffer_cleaned);
    strcat(messagenotify, " ?' >/dev/null 2>&1");
    
    //Send notification
    result = system(messagenotify);
    
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Utility functions - Insert policy 
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void change_profile_policy(const char *ccs_buffer,const char *profileNum, const char *doSave)
{
    int xxresult = 0;

    const char* domainString = extract_domain(ccs_buffer, "true");

    char domainStringCommand[32768] = "";
    strcat(domainStringCommand, "ccs-setprofile ");
    strcat(domainStringCommand, profileNum);
    strcat(domainStringCommand, " '");
    strcat(domainStringCommand, domainString);
    strcat(domainStringCommand, "' >/dev/null 2>&1");

    ccs_printw("\n");
    ccs_printw(" Editing Profile Policy :\n");
    ccs_printw(" %s\n", domainStringCommand);
    ccs_printw("\n");
    
    //ccs-setprofile profileNum
    xxresult=system(domainStringCommand);
    if (xxresult != 0) {
        popup_warning("Tomoyo Allow-All : Failed to save policy !","45");
    }

    //Result
    ccs_printw("\n");
    ccs_printw(" Edit Profile Policy :\n");
    ccs_printw(" Ok                           = 0\n");
    ccs_printw(" Nok                          = !0\n");
    ccs_printw(" ------------------------------------\n");
    ccs_printw(" Result                       = %d\n",xxresult);
    ccs_printw("\n");
    
    //Save policy
    if (strcmp(doSave, "true") == 0) {
        save_policy();
    } //else {
        //save_policy_question();
    //}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Secondary Main Function
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static _Bool ccs_handle_query(unsigned int serial)
{
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Setups & vars & print request 
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    //Vars tomoyo
	int y;
	int x; //old code - not used but needed 
    //int ret_ignored; //old code
    unsigned int pid;
    
    //Vars
    int c = 'N';
    char *line = NULL;
	char pidbuf[128] = "";
	static unsigned int prev_pid = 0;
	char *cp = strstr(ccs_buffer, " (global-pid=");
    
	if (!cp || sscanf(cp + 13, "%u", &pid) != 1) {
		ccs_printw("\n\nERROR: Unsupported query.\n\n");
		return false;
	}
    
	cp = ccs_buffer + strlen(ccs_buffer);
	if (*(cp - 1) != '\n') {
		ccs_printw("\n\nERROR: Unsupported query.\n\n");
		return false;
	}
    
	*(cp - 1) = '\0';
	if (pid != prev_pid) {
		if (prev_pid) ccs_printw("\n------------------------------------------------------------------------\n");
		prev_pid = pid;
	}
    
    //Print request
	ccs_printw("%s\n", ccs_buffer);
    
    /* Is this domain query? */
	if (strstr(ccs_buffer, "\n#"))
		goto not_domain_query;
	memset(pidbuf, 0, sizeof(pidbuf));    
	snprintf(pidbuf, sizeof(pidbuf) - 1, "select Q=%u\n", serial);
	ccs_printw("Allow? ('Y'es/'N'o/'R'etry/'S'how policy/'A'dd to policy and retry):");
    ccs_printw("\n");
        
    //-------------------------------------------------
    //Debug
    //ccs_printw("\n");
    //ccs_printw("%s\n", ccs_initial_readline_data);
    //ccs_printw("\n");
    //getchar();
    //-------------------------------------------------
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Prepare delegare answer to gui 
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
        
    //Prepare Gui
    int xresult = 2;
    
    //Remove date and time from request varialbe 
    const char* substringcurrent = ccs_buffer + 22;
    const char* substring1 = ccs_buffer_previous1 + 22;
    const char* substring2 = ccs_buffer_previous2 + 22;
    const char* substring3 = ccs_buffer_previous3 + 22;
    
    //Start Debug Output
    ccs_printw("\n");
    //ccs_printw(" ----------------------------------------\n");
    //ccs_printw(" Debug Infos : --------------------------\n");
    ccs_printw(" ----------------------------------------\n");
    
    //Checking if we are in learning mode I/II
    if (allownLearn) {
        //Reset allownLearn after 2 min
        struct timeval current_time;
        double elapsed_time_secs = 0;
        gettimeofday(&current_time, NULL);
        elapsed_time_secs = current_time.tv_sec - start_time_allowance.tv_sec;            
        ccs_printw(" Learn Mode Elapsed               = %ds\n", (int) elapsed_time_secs);
        if (elapsed_time_secs > 120) { //2 Mins
            allownLearn = false;
            ccs_printw(" Learn Mode                       = Going Off - Timeout\n");
        }
    }    
    
    //Checking if we are in learning mode II/II
    if (allownLearn) {
        char domainStringHistory0[32768] = ""; //Current request 
        char domainStringHistory1[32768] = ""; //Last request
        
        strcat(domainStringHistory0, extract_domain(ccs_buffer, "false"));
        strcat(domainStringHistory1, extract_domain(substring1, "false"));
        
        if (strcmp(domainStringHistory0, domainStringHistory1) == 0) {
            xresult = 36864;
            ccs_printw(" Learn Mode                       = On\n");
        } else {
            allownLearn = false;
            ccs_printw(" Learn Mode                       = Going Off - Other App Req.\n");
        }
    } else {
        //Stdr State
        ccs_printw(" Learn Mode                       = Off\n");
    }
    
    //Getting request profile 
    const char* tmpprofile = ccs_buffer + 30;
    char requestprofile = '0';
    requestprofile=tmpprofile[0];
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Delegate answer to gui = generate zenity question only if domain profile is 0 or 1 
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    if (xresult == 2) {
        // ............................ Only ask if profile is 0 or 1
        if ((requestprofile == '0') || (requestprofile == '1')) {
            if ((strcmp(substring1, substringcurrent) != 0) || (firstrun)) { // .... To avoid repetition - check 3 past time 
                if ((strcmp(substring2, substringcurrent) != 0) || (firstrun)) {
                    if ((strcmp(substring3, substringcurrent) != 0) || (firstrun)) { 
                        //Main Question ---------------------------------------------------------------
                        //Init question
                        const char* message = message_question;
                        prepare_main_question(ccs_buffer, "45");
                                                
                        //Send Question: --------------------------------------------------------------
                        //fork fix ccs_send_keepalive that was leading to policy not saved 
                        //(when 'A' add policy) and also fix several window for same request issue 
                        //-----------------------------------------------------------------------------
                        
                        
                        //Use fork for notification to avoid waiting it...
                        pid_t child_pid_notification = -1;
                        child_pid_notification = fork();
                        
                        if (child_pid_notification == 0) {
                            //Child code
                            //Send Notification
                            ccs_send_keepalive();
                            send_notification(ccs_buffer);
                        } else {
                            int wait_loop=5; //Give notification 2.5 sec to react (this is a non blocking code...)
                            while (wait_loop != 0) {ccs_send_keepalive(); usleep(500); wait_loop--;}
                            kill(child_pid_notification, SIGKILL);
                            wait(NULL); //properly terminate child and hande child exit, avoid zombie process (called from child)
                        }
                        
                        //Parent code
                        pid_t child_pid = -1;
                        child_pid = fork();

                        if (child_pid == 0)
                            //Child code
                            {while (true) {ccs_send_keepalive(); usleep(500);}}
                        else { 
                            //Parent code
                            xresult = system(message);                            
                            kill(child_pid, SIGKILL); //SIGTERM = Graceful termination
                            wait(NULL); //properly terminate child and hande child exit, avoid zombie process (called from child)
                        }
                        
                        //-----------------------------------------------------------------------------
                        
                        //Keep Alive
                        ccs_send_keepalive();
                        
                        //First Run -------------------------------------------------------------------
                        if (firstrun) {
                            //copy past 0 result to 1
                            strcpy(ccs_buffer_previous1,ccs_buffer);
                            ccs_buffer_previous_answer1 = xresult;
                            //Init buffer 2 & 3 
                            strcpy(ccs_buffer_previous2,"------------------------B2 \n Int2----------\n-- Empty Buffer 2"); //Long to avoid empty with 
                            strcpy(ccs_buffer_previous3,"------------------------B3 \n Int3----------\n-- Empty Buffer 3"); //date supression done before
                            ccs_buffer_previous_answer2 = xresult;
                            ccs_buffer_previous_answer3 = xresult;
                            //Disable first run
                            firstrun=false;
                        } else {
                            //copy past 2 result to 3
                            strcpy(ccs_buffer_previous3,ccs_buffer_previous2);
                            ccs_buffer_previous_answer3 = ccs_buffer_previous_answer2;
                            //copy past 1 result to 2
                            strcpy(ccs_buffer_previous2,ccs_buffer_previous1);
                            ccs_buffer_previous_answer2 = ccs_buffer_previous_answer1;
                            //copy past 0 result to 1
                            strcpy(ccs_buffer_previous1,ccs_buffer);
                            ccs_buffer_previous_answer1 = xresult;
                        }
                        //Repeat Init -----------------------------------------------------------------
                        how_many_auto_query_repeat = 0;
                        //Main Question ---------------------------------------------------------------
                    } else {
                        xresult = ccs_buffer_previous_answer3;
                        how_many_auto_query_repeat++;
                    }
                } else {
                    xresult = ccs_buffer_previous_answer2;
                    how_many_auto_query_repeat++;
                }
            } else {
                xresult = ccs_buffer_previous_answer1;
                how_many_auto_query_repeat++;
            }

            //Avoid too many repeat because the firewall is not registered             
            if ((how_many_auto_query_repeat > 25) && ((xresult != 62464) && (xresult != 22528))) {
                char messagex[32768] = "";
                fprintf(stderr, "\n\n\nError some thing went wrong more than 15 same request !\n\n\n"
                "You need to register this program to %s to run this program.\n\n\n", CCS_PROC_POLICY_MANAGER);
                //Popup Warning
                popup_warning("Tomoyo : Error some thing went wrong more than 15 same request !","45");
                strcat(messagex, "Tomoyo : You need to register this program to ");
                strcat(messagex, CCS_PROC_POLICY_MANAGER);
                strcat(messagex, " to run this program");
                popup_warning(messagex,"45");
                return false;
            }
            if (how_many_auto_query_repeat > 150) {
                //Popup Warning Too Many Repeat
                ccs_printw("\n\n\nWarning same request repeated more than 150x\n\n\n");
                popup_warning("Tomoyo : Warning same request repeated more than 150x !","45");
                char messagex[32768] = "";
                strcat(messagex, "Tomoyo : It could be that you need to register this program to ");
                strcat(messagex, CCS_PROC_POLICY_MANAGER);
                strcat(messagex, " to run this program");
                popup_warning(messagex,"45");
                int question_error = popup_question("Tomoyo : quit monitor to avoid infenite loop ? \nTimeout will quit", "45");
                if ((question_error == 25600) || (question_error == 53248)) {
                    return false;
                }
            }
        } else {
            xresult=256;
        }
    }
    
    //-------------------------------------------------
    //old code
    //xresult = system(message);
    //ccs_send_keepalive();    
    //-------------------------------------------------
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Gui set result & result code 
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    //-------------------------------------------------
    //Debug
    //getchar();
    //-------------------------------------------------
    
    //Result is cancel by default
    c = 'N';
    
    //If Nothing
    if (xresult == 2)       {c = 'N';} 
    
    //If Denied (Passthrough requests)
    if (xresult == 256)     {c = 'N';} 
    
    //If Timeout
    if (xresult == 53248)   {c = 'N';}   
    
    //If Deny
    if (xresult == 22528)   {c = 'N';}    
    
    //If Allow    
    if (xresult == 36864)   {c = 'Y';}    
    
    //If Deny All
    if (xresult == 62464)   {c = 'Z';}
    
    //If Allow All
    if (xresult == 11264)   {c = 'X';}
    
    //If Allow & Learn 
    if (xresult == 25600)   {c = 'J';} 
    
    //If Allow All & Save
    if (xresult == 51200)   {c = 'K';} 
    
    //If Zenity Command Worked
    if (xresult == 59392)   {c = 'L';} 
    
    //If Zenity Command Did Not Work
    if (xresult == 47104)   {c = 'M';} 

    //Result
    ccs_printw("\n");
    //ccs_printw(" ----------------------------------------\n");
    //ccs_printw(" Allow & Learn                    = 25600\n");
    //ccs_printw(" Allow All & Save                 = 51200\n");
    //ccs_printw(" Allow All                        = 11264\n");
    //ccs_printw(" Allow                            = 36864\n");
    //ccs_printw(" Deny All                         = 62464\n");
    //ccs_printw(" Deny                             = 22528\n");
    //ccs_printw(" Timeout                          = 53248\n");
    //ccs_printw(" Zenity command worked            = 59392\n");
    //ccs_printw(" Zenity command did not work      = 47104\n");
    //ccs_printw(" Passtrough requests (profile >1) = 256\n");
    ccs_printw(" ----------------------------------------\n");
    ccs_printw(" Result                           = %d\n",xresult);
    ccs_printw(" Char Answer                      = ");ccs_printw("%c\n", c);   
    //ccs_printw("\n");
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Zenity available but answer not captured
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Y'es/'N'o/'R'etry/'S'how policy/'A'dd
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    if (c == 'L') {
        //stderr Warning
        fprintf(stderr, "\n\n\nTomoyo : Warning 59392 : Aswer was not captured may be\n"
        "because window was closed, this application need, this\n"
        "application need zenity v3.24 minimum.\n\n\n");
        //Popup Warning
        popup_warning("Tomoyo : Warning 59392 : Aswer was not captured may be because window was closed, this application need zenity v3.24 minimum.","120");
        //return false; //do not quit
        
        //Set true answer
        c = 'N';
        
        //Update output char answer
        ccs_printw(" True Char Answer                      = ");ccs_printw("%c\n", c);
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Zenity not available  
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    if (c == 'M') {
        //stderr Warning
        fprintf(stderr, "\n\n\nTomoyo : Warning 47104 : Unable to run zenity,\n"
        "this application need zenity v3.24 minimum,\n"
        "please install zenity from your repo\n"
        "or from github.\n\n\n");
        //Popup Warning
        popup_warning("Tomoyo : Warning 47104 : Unable to run zenity, this application need zenity v3.24 minimum, please install zenity from your repo or from github.","120");
        //return false;
        
        //Set true answer
        c = 'N';
        
        //Update output char answer
        ccs_printw(" True Char Answer                      = ");ccs_printw("%c\n", c);
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Allow-All  
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    if (c == 'X') {
        change_profile_policy(ccs_buffer , "2" , "false");
        
        //Set true answer
        c = 'Y';
        
        //Update output char answer
        ccs_printw(" True Char Answer                      = ");ccs_printw("%c\n", c);
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Deny-All
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    if (c == 'Z') {
        change_profile_policy(ccs_buffer , "8" , "false");
        
        //Set true answer
        c = 'N';
        
        //Update output char answer
        ccs_printw(" True Char Answer                      = ");ccs_printw("%c\n", c);
    }    

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Allow & Learn
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    if (c == 'J') {
        if (!allownLearn) {
            //Start timer
            gettimeofday(&start_time_allowance, NULL);
            //Enable learn for next request
            allownLearn = true;
        }
        
        //Answer set to allow
        c = 'A';
        
        //Update output char answer
        ccs_printw(" True Char Answer                      = ");ccs_printw("%c\n", c);
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Allow All & Save
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    if (c == 'K') {
        change_profile_policy(ccs_buffer , "2" , "true");
        
        //Set true answer
        c = 'Y';
        
        //Update output char answer
        ccs_printw(" True Char Answer                      = ");ccs_printw("%c\n", c);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Display policy
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    // Function to list policy 
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
		} 
        else {
            //old code version
			//ret_ignored = write(ccs_domain_policy_fd, pidbuf, strlen(pidbuf));
			write(ccs_domain_policy_fd, pidbuf, strlen(pidbuf));
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
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Allow - Yes - Append kernel policy
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    //Convert yes to append on learning mode
    if (allownLearn && c == 'Y') {
        c = 'A';
    }
    
	//Append to domain policy 
	if (c != 'A' && c != 'a')
		goto not_append;
    
    //Answer set to r
	c = 'r';
    
    //The getyx macro places the current cursor position of the given window in the two integer variables y and x.
    //The default window. A default window called stdscr, which is the size of the terminal screen, 
    //is supplied. To use the stdscr window, you don't need to do any initializations. 
    //You can also divide the screen to several parts and create a window to represent each part.
	getyx(stdscr, y, x);
    
    //Locate last occurrence of character in string (strrchr)
	cp = strrchr(ccs_buffer, '\n');
    
	if (!cp)
		return false;
    
    //Trunc cp var after last return \n
	*cp++ = '\0';
    
	ccs_initial_readline_data = cp;
	ccs_readline_history_count =
		ccs_add_history(cp, ccs_readline_history,
				ccs_readline_history_count,
				CCS_MAX_READLINE_HISTORY);
    
    //Read line and auto return (modified readline.c)
	line = ccs_readline(y, 0, "Enter new entry> ", ccs_readline_history,
			    ccs_readline_history_count, 128000, 8);
    
    //The scrollok option controls what happens when the cursor of a window 
    //is moved off the edge of the window or scrolling region
    //either as a result of a newline action on the bottom line, or typing 
    //the last character of the last line. If disabled, (bf is FALSE), 
    //the cursor is left on the bottom line. If enabled, (bf is TRUE),
    //the window is scrolled up one line (Note that to get the physical 
    //scrolling effect on the terminal, it is also necessary to call idlok)
    //scrollok() means that if you try to print too many lines to output, 
    //it will use the terminal's scroll region (hardware if available, 
    //software if necessary) to scroll the region up, causing loss of 
    //data at the top of the current scroll region
    //scrollok - enable or disable scrolling on a window
	scrollok(stdscr, TRUE);
    
	ccs_printw("\n");
	if (!line || !*line) {
		ccs_printw("\nNone added.\n");
        popup_warning("Tomoyo : Rule none added","45");
		goto not_append;
	}
    
    //Adding rule
	ccs_readline_history_count =
		ccs_add_history(line, ccs_readline_history,
				ccs_readline_history_count,
				CCS_MAX_READLINE_HISTORY);
    
	if (ccs_network_mode) {
		fprintf(ccs_domain_fp, "%s%s\n", pidbuf, line);
		fflush(ccs_domain_fp);
	} else {
        //old code 
		//ret_ignored = write(ccs_domain_policy_fd, pidbuf, strlen(pidbuf));
		//ret_ignored = write(ccs_domain_policy_fd, line, strlen(line));
		//ret_ignored = write(ccs_domain_policy_fd, "\n", 1);
		write(ccs_domain_policy_fd, pidbuf, strlen(pidbuf));
		write(ccs_domain_policy_fd, line, strlen(line));
		write(ccs_domain_policy_fd, "\n", 1);
	}
    
	ccs_printw("\nAdded '%s'.\n", line);
    
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
	//old code
    //ret_ignored = write(ccs_query_fd, ccs_buffer, strlen(ccs_buffer));
	write(ccs_query_fd, ccs_buffer, strlen(ccs_buffer));
	ccs_printw("\n");
	return true;
    
not_domain_query:
	ccs_printw("Allow? ('Y'es/'N'o/'R'etry):");
    
    ccs_send_keepalive();
    int question = popup_question("Tomoyo : Non domain query request...\nAllow ?", "45");
    
    //Default value
    c = 'N';
        
    if (question == 53248) { //Timeout
        c = 'N';
    } else {
        if (question == 25600) { //Yes
            c = 'Y';
        } else {
            c = 'N';
        }
    }
    
	ccs_printw("%c\n", c);
	goto write_answer;
    
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main start functionS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
		ccs_domain_policy_fd = open(CCS_PROC_POLICY_DOMAIN_POLICY, O_RDWR);
	}
        
	if (ccs_query_fd == EOF) {
		fprintf(stderr,"You can't run this utility for this kernel.\n");
        popup_warning("Tomoyo : You can't run this utility for this kernel","45");
		return 1;
	} else if (!ccs_network_mode && write(ccs_query_fd, "", 0) != 0) {
        char message[32768] = "";
		fprintf(stderr, "You need to register this program to %s to run this program.\n", CCS_PROC_POLICY_MANAGER);
        //Popup Warning
        strcat(message, "Tomoyo : You need to register this program to ");
        strcat(message, CCS_PROC_POLICY_MANAGER);
        strcat(message, "to run this program");
        popup_warning(message,"45");
		return 1;
	}
    
	ccs_readline_history = ccs_malloc(CCS_MAX_READLINE_HISTORY * sizeof(const char *));
    
	ccs_send_keepalive();
    
    //Curses - initscr is normally the first curses routine to call when initializing a program. 
    //A few special routines sometimes need to be called before it; these are slk_init, filter, 
    //ripoffline, use_env. For multiple-terminal applications, newterm may be called before initscr. 
    //A program that outputs to more than one terminal should use the newterm routine 
    //for each terminal instead of initscr
	initscr();
    
    //Curses - The cbreak routine disables line buffering and erase/kill character-processing 
    //(interrupt and flow control characters are unaffected), making characters typed by 
    //the user immediately available to the program. The nocbreak routine returns the 
    //terminal to normal (cooked) mode.
	cbreak();
    
    //Curses - The echo and noecho routines control whether characters typed by 
    //the user are echoed by getch as they are typed
	noecho();
    
    //Curses - The nl and nonl routines control whether the underlying display device 
    //translates the return key into newline on input, and whether it translates newline 
    //into return and line-feed on output (in either case, the call addch('\n') does 
    //the equivalent of return and line feed on the virtual screen)
	nonl();
    
    //Curses - If the intrflush option is enabled, (bf is TRUE), when an interrupt key 
    //is pressed on the keyboard (interrupt, break, quit) all output in the tty driver 
    //queue will be flushed, giving the effect of faster response to the interrupt, 
    //but causing curses to have the wrong idea of what is on the screen. Disabling 
    //(bf is FALSE), the option prevents the flush. The default for the option is
    //inherited from the tty driver settings. The window argument is ignored
	intrflush(stdscr, FALSE);
    
    //Curses - The keypad option enables the keypad of the user's terminal. 
    //If enabled (bf is TRUE), the user can press a function key (such as an arrow key) 
    //and wgetch returns a single value representing the function key, as in KEY_LEFT. 
    //If disabled (bf is FALSE), curses does not treat function keys specially and the 
    //program has to interpret the escape sequences itself. If the keypad in the terminal 
    //can be turned on (made to transmit) and off (made to work locally), turning on 
    //this option causes the terminal keypad to be turned on when wgetch is called. 
    //The default value for keypad is false.
	keypad(stdscr, TRUE);
    
    //Curses - The clear and wclear routines are like erase and werase, but they 
    //also call clearok, so that the screen is cleared completely on the next
    //call to wrefresh for that window and repainted from scratch.
	clear();
    
    //Curses - The refresh and wrefresh routines (or wnoutrefresh and doupdate) 
    //must be called to get actual output to the terminal, as other routines merely 
    //manipulate data structures. The routine wrefresh copies the named window to the 
    //physical terminal screen, taking into account what is already there to do 
    //optimizations. The refresh routine is the same, using stdscr as the default 
    //window. Unless leaveok has been enabled, the physical cursor of the terminal 
    //is left at the location of the cursor for that window.
	refresh();
    
    //Curses - The scrollok option controls what happens when the cursor of a window
    //is moved off the edge of the window or scrolling region, either as a result 
    //of a newline action on the bottom line, or typing the last character of 
    //the last line. If disabled, (bf is FALSE), the cursor is left on the bottom 
    //line. If enabled, (bf is TRUE), the window is scrolled up one line (Note 
    //that to get the physical scrolling effect on the terminal, it 
    //is also necessary to call idlok).
	scrollok(stdscr, TRUE);
    
    //Start monitoring
	if (ccs_network_mode) {
		const u32 ip = ntohl(ccs_network_ip);
		ccs_printw("Monitoring /proc/ccs/query via %u.%u.%u.%u:%u.",
			   (u8) (ip >> 24), (u8) (ip >> 16), (u8) (ip >> 8),
			   (u8) ip, ntohs(ccs_network_port));
	} else {
        ccs_printw("Monitoring /proc/ccs/query .");
    }
    
	ccs_printw(" Press Ctrl-C to terminate.\n\n");
    
    //Main monitoring 
	while (true) {
		unsigned int serial;
		char *cp;
        
		/* Wait for query and read query. */
		memset(ccs_buffer, 0, sizeof(ccs_buffer));
		if (ccs_network_mode) {
			int i;
			//int ret_ignored; //old code
			//ret_ignored = write(ccs_query_fd, "", 1); //old code
			write(ccs_query_fd, "", 1);
			for (i = 0; i < sizeof(ccs_buffer) - 1; i++) {
				if (read(ccs_query_fd, ccs_buffer + i, 1) != 1) break;
				if (!ccs_buffer[i])	goto read_ok;
			}
			break;
		} else {
			struct pollfd pfd;
			pfd.fd = ccs_query_fd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			poll(&pfd, 1, -1);
			if (!(pfd.revents & POLLIN)) continue;
			if (read(ccs_query_fd, ccs_buffer, sizeof(ccs_buffer) - 1) <= 0) continue;
		}
        
read_ok:
		cp = strchr(ccs_buffer, '\n');
		if (!cp) continue;
        //Cut variable
		*cp = '\0';

		/* Get query number. */
		if (sscanf(ccs_buffer, "Q%u-%hu", &serial, &ccs_retries) != 2) continue;
		memmove(ccs_buffer, cp + 1, strlen(cp + 1) + 1);

		/* Clear pending input. */;
		timeout(0);
		while (true) {
			int c = ccs_getch2();
			if (c == EOF || c == ERR) break;
		}
		timeout(1000);
		if (ccs_handle_query(serial)) continue;
		break;
	}
    
    //Curses - 
	endwin();
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// End.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
