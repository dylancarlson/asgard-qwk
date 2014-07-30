/************************************************************************/
/*                              AsgQWK.c                                */
/*                   QWIK off-line reader for Asgarde-86.               */
/************************************************************************/

/************************************************************************/
/*                              history                                 */
/*                                                                      */
/* 93Sep15 GSM  Created.                                                */
/************************************************************************/

#include "asg.h"

/************************************************************************/
/*                              Contents                                */
/*                                                                      */
/* QWKmain              Main entry to QWK routines.                     */
/* QWKUserConfig        QWK user configuration menu.                    */
/* ScanRooms            Room selection menu.                            */
/* UpdateLastRead                                                       */
/* CreateQWKPacket                                                      */
/* ExtractAndAdd                                                        */
/* OkToSend                                                             */
/* menus                                                                */
/* qwkglobalreplace                                                     */
/* qwkReadDate                                                          */
/* qwkReadTime                                                          */
/* RepReadDate                                                          */
/* RepReadTime                                                          */
/* ResetRooms                                                           */
/* ArchQWK                                                              */
/* InputREPPacket                                                       */
/* ImportQWKPacket                                                      */
/* CleanUpPacket                                                        */

/************************************************************************/

#define DoFree(p)       if ((p) != NULL) { free(p); }
#define VERSION         "V0.1"

/************************************************************************/
/*             external variable declarations in LOGEDIT.C              */
/************************************************************************/
extern CONFIG    cfg;
extern LogTable  *logTab;        /* RAM index of pippuls         */
extern logBuffer logBuf;         /* Pippul buffer                */
extern aRoom     roomBuf;
extern MessageBuffer *msgBuf;         /* Message buffer                    */
extern MessageBuffer *tempMess;         /* Message buffer                    */
extern NetBuffer netBuf;
extern rTable    *roomTab;
extern NetTable  *netTab;
extern FILE      *logfl;                /* log file descriptor          */
extern FILE      *roomfl;
extern FILE      *netfl;
extern struct floor  *FloorTab;
extern char NotForgotten;
extern char onConsole;
extern char        haveCarrier;
extern char        outFlag;   /* will be one of the above     */
extern AN_UNSIGNED crtColumn;      /* where are we on screen now?  */
extern char        noStop;
extern long   InChatTime;
extern char loggedIn;
extern int thisLog;
extern char *READ_ANY;
extern char FileTransStat;
extern FILE        *upfd;

static char *MonthTab[] = {
          "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
          "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"
};
SYS_FILE tempname;
SYS_FILE tempFile;
int totconf, totfnd, tottoyou;
unsigned char qwkbuf[128];
long currentsector;
FILE *qwkindfd, *qwkmsgfd, *qwkctlfd, *qwkpersfd;

struct qwkheader {
    unsigned char status;
    unsigned char number[7];
    unsigned char date[8];
    unsigned char time[5];
    unsigned char to[25];
    unsigned char from[25];
    unsigned char subject[25];
    unsigned char password[12];
    unsigned char ref_no[8];
    unsigned char size[6];
    unsigned char active;
    unsigned int  conf;
    unsigned int  logical_msgno;
    unsigned char nettag;
}; /* the important stuff from the header */
/* true header format, only fields I use */

struct qwkdefinition {
    char YourOwn;
    char ResetBBSMsg;
    char NewFileScan;
    char FileDate[8];
    char Bulletins;
    int  MaxPacket;
    int  MaxRoom;
    char UpProtocol;
    char DownProtocol;
    char Archiver;
    char Hangafterupload;
};

struct qwkdefinition qwkdef;

typedef struct {
    int Selected;
    MSG_NUMBER OldLastMessageNo;
    MSG_NUMBER LastMessageNo;
    MSG_NUMBER CurrentLastMessageNo;
    int AlternateConferenceNo;
} QwkRoom;

typedef struct {
    int messagesfound;
    int messagestoyou;
} UsedQwkRoom;

QwkRoom *QwkRooms;              /* RAM index of rooms           */
UsedQwkRoom *UsedQwkRooms;

#ifdef ANSI_PROTOTYPING
char QWKmain(void);
void QWKUserConfig(void);
void ScanRooms(void);
void UpdateLastRead(void);
void CreateQWKPacket(void);
void ExtractAndAdd(int room);
char OkToSend(void);
void menus(void);
void qwkglobalreplace(char *buf, char *qwkold);
int qwkReadDate(char *date, char *datestr);
int qwkReadTime(char *time, char *timestr);
int RepReadDate(char *date, char *datestr);
int RepReadTime(char *time, char *timestr);
void ResetRooms(void);
long ArchQWK(void);
void InputREPPacket(void);
void ImportQWKPacket(void);
void CleanUpPacket(void);
#endif

/************************************************************************/
/*      QWKmain() main controller.                                      */
/************************************************************************/
char QWKmain()
{
    FILE *qwklogfd;
    char HasLog;
    char tempStr[20];
    int rover;

    mPrintf("\n THOR Mail Reader %s\n %s\n ", VERSION,COPYRIGHT);
    if (!loggedIn) {
        mPrintf("Must log in to use THOR QWK Mail reader!\n ");
        return GOOD_SELECT;
    }
    QwkRooms = (QwkRoom *) GetDynamic(MAXROOMS * (sizeof (*QwkRooms)));
    UsedQwkRooms = (UsedQwkRoom *) GetDynamic(MAXROOMS * (sizeof (*UsedQwkRooms)));
    HasLog = FALSE;
    sprintf(tempStr, "qwklog.%03d", thisLog);
    makeSysName(tempname, tempStr, &cfg.QWKFilesArea);
    if ((qwklogfd=fopen(tempname, "rb")) != NULL) {
         if (fread(&qwkdef, sizeof qwkdef, 1, qwklogfd) == 1) {
             HasLog = TRUE;
             fread(QwkRooms, (sizeof (*QwkRooms)) * MAXROOMS, 1, qwklogfd);
         }
         fclose(qwklogfd);
     }
     if (HasLog == FALSE) {
         qwkdef.YourOwn = TRUE;
         qwkdef.NewFileScan = FALSE;
         strcpy(qwkdef.FileDate, "12/31/99");
         qwkdef.Bulletins = FALSE;
         qwkdef.MaxPacket = cfg.QWKMAXPACKET;
         qwkdef.MaxRoom = cfg.QWKMAXROOM;
         qwkdef.UpProtocol = 0;
         qwkdef.DownProtocol = 0;
         qwkdef.Archiver = 1;
         qwkdef.Hangafterupload = FALSE;
         for (rover = 0; rover < MAXROOMS; rover++) {
             QwkRooms[rover].Selected = 0;
             QwkRooms[rover].OldLastMessageNo = 0l;
             QwkRooms[rover].LastMessageNo =
                  logBuf.lbvisit[logBuf.lbgen[rover] & CALLMASK];
             QwkRooms[rover].CurrentLastMessageNo = 0l;
             QwkRooms[rover].AlternateConferenceNo = 0;
         }
     }
    menus();
    sprintf(tempStr, "qwklog.%03d", thisLog);
    makeSysName(tempname, tempStr, &cfg.QWKFilesArea);
    if ((qwklogfd=fopen(tempname, "wb")) != NULL) {
         if (fwrite(&qwkdef, sizeof qwkdef, 1, qwklogfd) == 1) {
             HasLog = TRUE;
             fwrite(QwkRooms,(sizeof (*QwkRooms)) * MAXROOMS, 1, qwklogfd);
         }
         fclose(qwklogfd);
     }
    free(QwkRooms);
    free(UsedQwkRooms);
    getRoom(LOBBY);
    return GOOD_SELECT;
}

/************************************************************************/
/*      QWKUserConfig() various settings for users.                     */
/************************************************************************/
void QWKUserConfig()
{
    char done = FALSE;
    char letter, letter1;
    long sessioncheck;
    do {
        mPrintf("\n THOR QWK Mail Reader configuration menu:\n ");
        mPrintf("                              Current setting\n ");
        mPrintf("[C]onfigure conferences       (shows when selecting)\n ");
        mPrintf("[R]eset high message pointers\n ");
        mPrintf("[S]end your own messages      %s\n ",
                        qwkdef.YourOwn ? "Yes" : "No");
/*        mPrintf("[N]ew files scan              %s\n ",
                        qwkdef.NewFileScan ? "Yes" : "No");
        mPrintf("[D]ate for new files          %s\n ", qwkdef.FileDate);
        mPrintf("[I]nclude new bulletins       %s\n ",
                        qwkdef.Bulletins ? "Yes" : "No");
  */      mPrintf("[M]aximum packet sizes        %3d/%3d\n ", qwkdef.MaxPacket, qwkdef.MaxRoom);
/*      mPrintf("[A]ttachment size limit       0K/Your attachments only\n ");
*/        mPrintf("[T]ransfer protocol-upload    %s\n ",
                            FindProtoName(qwkdef.UpProtocol));
        mPrintf("                   -download    %s\n ",
                            FindProtoName(qwkdef.DownProtocol));
        mPrintf("[P]acker (archiver)           %s\n ",
                          GetCompEnglish(qwkdef.Archiver));
  /*      mPrintf("[F]ormat for packets          QWK\n ");
  */      mPrintf("[G]oodbye after upload        %s\n ",
                        qwkdef.Hangafterupload ? "Yes" : "No");
        mPrintf("[H]elp with configuration\n ");
        mPrintf("[Q]uit THOR QWK Configuration menu\n ");
        sessioncheck=chkTimeSince(USER_SESSION) - InChatTime;
        if (sessionLimit > 0)
            mPrintf("You have been on for %lu minutes, and have %lu minutes left.\n ",
            (sessioncheck / 60), (((sessionLimit * 60) - sessioncheck) / 60));
        mPrintf("\n CONFIGURE MENU [C R S M T P G H Q]: ");
        letter1 = toUpper(iChar());
        mPrintf("\n ");
        switch (letter1) {
            case 'C': ScanRooms();              break;
            case 'R': ResetRooms();             break;
            case 'S':
                qwkdef.YourOwn = !qwkdef.YourOwn;
                break;
    /*        case 'N':
                qwkdef.NewFileScan = !qwkdef.NewFileScan;
                break;
            case 'I':
                qwkdef.Bulletins = !qwkdef.Bulletins;
                break;
            case 'A':
                mPrintf("Not used at this time\n ");
                break;
    */        case 'T':
                tempMess->mbtext[0] = NULL;
                UpProtsEnglish(tempMess->mbtext);
                mPrintf("\n%s\n Enter upload protocol: ", tempMess->mbtext);
                letter = toUpper(iChar());
                mPrintf("\n ");
                if ((qwkdef.UpProtocol = FindProtocolCode(letter, TRUE)) == -1) {
                    mPrintf("invalid protocol, setting to zmodem\n ");
                    qwkdef.UpProtocol = FindProtocolCode('Z', TRUE);
                }
                tempMess->mbtext[0] = NULL;
                DownProtsEnglish(tempMess->mbtext);
                mPrintf("%s\n Enter download protocol: ", tempMess->mbtext);
                letter = toUpper(iChar());
                mPrintf("\n ");
                if ((qwkdef.DownProtocol = FindProtocolCode(letter, FALSE)) == -1) {
                    mPrintf("invalid protocol, setting to zmodem\n ");
                    qwkdef.DownProtocol = FindProtocolCode('Z', FALSE);
                }
                break;
            case 'P':
                qwkdef.Archiver = GetUserCompression();
                break;
/*           case 'D':
                break;
 */           case 'M':
                qwkdef.MaxPacket = getNumber("Total messages in packet", 0, cfg.QWKMAXPACKET);
                qwkdef.MaxRoom = getNumber("Total messages per conference", 0, cfg.QWKMAXROOM);
                break;
            case 'G':
                qwkdef.Hangafterupload= !qwkdef.Hangafterupload;
                break;
/*            case 'F':
                mPrintf("Not used at this time\n ");
                break;
*/            case 'H':
                  tutorial(FALSE, "qwkconf.hlp", TRUE, FALSE);
                  break;
            case 'Q': done = TRUE;               break;
            default: done = TRUE;                break;
        }
        if (!done) writeSysTab();
    } while (!done);
}

/************************************************************************/
/*      ScanRooms() Room selection setup routine.                       */
/************************************************************************/
void ScanRooms()
{
#define ALL 0
#define SELECTED 1

    int rover, currentstart, i, newstart, done, innerdone, lpcnt;
    int lines, pagelength, chosen, CurrentFloor;
    char tempStr[10], scanflag, letter[5];

    newstart = 0;
    currentstart = 0;
    i = 0;
    if (logBuf.lbscrnlngth == 0)
        pagelength = 23;
    else
        pagelength = logBuf.lbscrnlngth;
    done=FALSE;
    scanflag = ALL;
    do {
        lines = 0;
        for (rover = currentstart; rover < MAXROOMS; rover++) {
                /* deep breath ... should rewrite this, prime example of
                   programming via accretion. */
            if ((KnownRoom(rover) || (NotForgotten &&
               roomTab[rover].rtflags.INUSE &&
               (aide && (cfg.BoolFlags.aideSeeAll || onConsole) &&
               (!roomTab[rover].rtflags.INVITE || onConsole))))) {
                if (QwkRooms[rover].Selected)
                    sprintf(tempStr, "(%7.7lu)", QwkRooms[rover].LastMessageNo);
                else
                    memset(tempStr, NULL, 7);
                if ((scanflag == SELECTED && QwkRooms[rover].Selected) ||
                   (scanflag == ALL)) {
                    mPrintf("%3d)%s%-20s %9s ", rover,
                     (QwkRooms[rover].Selected ? "*" : " "),
                     roomTab[rover].rtname,
                     tempStr);
                    i++;
                    if (i == 2) {
                        mPrintf("\n ");
                        i = 0;
                        lines = lines + 1;
                    }
                    if (lines == (pagelength - 5)) {
                        newstart = rover+1;
                        rover = MAXROOMS;
                    }
                }
            }
        }
        innerdone = FALSE;
        do {
/*            mPrintf("You have been on for %d minutes, and have %d minutes left.\n ", ontime, maxtime);  */
            if (newstart < MAXROOMS)
                mPrintf(" - More below, use [+] to view\n ");
            mPrintf("CONFERENCE MENU:\n Enter #, [*] select all, [L]ist, [+]Next, [-]Previous, List [A]ll\n ");
            mPrintf("List [O]nly selected, [R]everse selections, [T]op of conferences, or [Q]uit? ");
            getString("", letter, 5, 0);
            if (strlen(letter) == 0)
                letter[0]='?';
            for (lpcnt = 0; lpcnt < strlen(letter); lpcnt++)
                letter[lpcnt]=toUpper(letter[lpcnt]);
            mPrintf("\n ");
            switch (letter[0]) {
                case 'T':
                    currentstart = 0;
                    innerdone=TRUE;
                    break;

                case '+':
                    currentstart = newstart;
                    if (currentstart > MAXROOMS)
                        currentstart = MAXROOMS - (pagelength - 5);
                    innerdone = TRUE;
                    break;

                case '-':
                    currentstart = currentstart - (pagelength - 5);
                    if (currentstart < 0)
                        currentstart = 0;
                    innerdone = TRUE;
                    break;

                case 'L':
                    innerdone = TRUE;
                    break;

                case 'A':
                    scanflag = ALL;
                    break;

                case 'O':
                    scanflag = SELECTED;
                    break;

                case 'Q':
                    done = TRUE;
                    innerdone = TRUE;
                    break;

                case '*':
                case 'R':
                    for (chosen = 0; chosen < MAXROOMS; chosen++) {
                        if (KnownRoom(chosen) ||
                           (NotForgotten && roomTab[chosen].rtflags.INUSE &&
                           (aide && (cfg.BoolFlags.aideSeeAll || onConsole)) &&
                           (!roomTab[chosen].rtflags.INVITE || onConsole))) {
                           if (!QwkRooms[chosen].Selected || letter[0]== '*')
                               QwkRooms[chosen].Selected = TRUE;
                           else
                               QwkRooms[chosen].Selected = FALSE;
                        }
                    }
                    break;

                case '?':
                    mPrintf("Invalid selection. Try Again.\n ");
                    break;

                default:
/*                    chosen = getNumber("Enter room(conference) number", 0, MAXROOMS);  */
                    chosen = (int) atoi(letter);
                    if (KnownRoom(chosen) ||
                       (NotForgotten && roomTab[chosen].rtflags.INUSE &&
                       (aide && (cfg.BoolFlags.aideSeeAll || onConsole)) &&
                       (!roomTab[rover].rtflags.INVITE || onConsole))) {
                           if (QwkRooms[chosen].Selected)
                               QwkRooms[chosen].Selected = FALSE;
                           else
                               QwkRooms[chosen].Selected = TRUE;
                    }
                    else {
                        mPrintf("Room(conference) not known or not in use.\n ");
                    }
                    break;
            }
        } while (!innerdone);
    } while (!done);
}

/************************************************************************/
/*      UpdateLastRead() Updates QWK message pointer structure.  Called */
/*      only after a succesful download.                                */
/************************************************************************/
void UpdateLastRead()
{
    int rover;

    for (rover = 0; rover < MAXROOMS; rover++) {
        if (UsedQwkRooms[rover].messagesfound > 0) {
            QwkRooms[rover].OldLastMessageNo = QwkRooms[rover].LastMessageNo;
            QwkRooms[rover].LastMessageNo = QwkRooms[rover].CurrentLastMessageNo;
            QwkRooms[rover].CurrentLastMessageNo = 0l;
        }
    }
}

/************************************************************************/
/*      CreateQWKPacket() Scans all rooms for new messages and extracts */
/*      them IF the room is selected.                                   */
/************************************************************************/
void CreateQWKPacket()
{
    int rover, currentstart, i, newstart, done, innerdone;
    int   year, month, day, hours, minutes, seconds, milli;
    int lines, pagelength, chosen, success;
    char tempstr[30], tempstr2[30], scanflag, c;
    long packetSize;

    getRawDate(&year, &month, &day, &hours, &minutes,
                                        &seconds, &milli);
    makeSysName(tempname, "messages.dat", &cfg.QWKWorkArea);
    if ((qwkmsgfd=fopen(tempname, "wb")) == NULL) {
        crashout("Cannot Open messages data file!");
    }
    memset(qwkbuf, ' ', 128);
    sprintf(qwkbuf,"Produced by QMAIL...Copyright (c) 1987 by Sparkware. All Rights Reserved.");
    success = strlen(qwkbuf);
    qwkbuf[success]=' ';
    success=FALSE;
    fwrite(qwkbuf, 128, 1, qwkmsgfd);
    currentsector = 1;

    makeSysName(tempname, "personal.ndx", &cfg.QWKWorkArea);
    if ((qwkpersfd=fopen(tempname, "wb")) == NULL) {
        crashout("Cannot Open personal.ndx data file!");
    }

    makeSysName(tempname, "control.dat", &cfg.QWKWorkArea);
    if ((qwkctlfd=fopen(tempname, "wb")) == NULL) {
        crashout("Cannot Open control data file!");
    }
    fputs((cfg.codeBuf+cfg.nodeName), qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    fputs((cfg.CityState + cfg.codeBuf), qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    fputs((cfg.nodeId + cfg.codeBuf), qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    fputs(cfg.SysopName, qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    sprintf(tempstr, "0,%s",(cfg.codeBuf + cfg.nodeTitle));
    fputs(tempstr, qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    sprintf(tempstr, "%02d-%02d-%04d,%02d:%02d:%02d", month, day, year,
                     hours, minutes, seconds);
    fputs(tempstr, qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    sprintf(tempstr, "%s", strupr(logBuf.lbname));
    fputs(tempstr, qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    fputs(" ", qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    fputs("0", qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);

    mPrintf("\n Preparing mail packet...\n ");
    mPrintf("Press S to abort scan\n \n ");
    mPrintf("Total message limit: %d\n ", qwkdef.MaxPacket);
    mPrintf("Room(Conference) message limit: %d\n ", qwkdef.MaxRoom);
    mPrintf("                                          High    Last    Number      To\n ");
    mPrintf(" Number  Conference                    Message    Read     Found     You\n ");
    mPrintf("------------------------------------------------------------------------\n ");
    totfnd = 0;
    totconf = 0;
    outFlag = OUTOK;
    for (rover = 0; rover < MAXROOMS && onLine() && outFlag == OUTOK; rover++) {
        if (rover == MAILROOM) {
            getRoom(rover);
            fillMailRoom();                         /* update room also */
        }
        UsedQwkRooms[rover].messagesfound = 0;
        UsedQwkRooms[rover].messagestoyou=0;
        if (QwkRooms[rover].Selected && roomTab[rover].rtflags.INUSE ) {
            mPrintf("   %3d     %-20s        %7.7lu %7.7lu ", rover,
            roomTab[rover].rtname, roomTab[rover].rtlastMessage,
            QwkRooms[rover].LastMessageNo);
            ExtractAndAdd(rover);
            mPrintf("    %5d   %5d\n ", UsedQwkRooms[rover].messagesfound,
                                      UsedQwkRooms[rover].messagestoyou);
            tottoyou += UsedQwkRooms[rover].messagestoyou;
            mAbort();
        }
    }
    if (outFlag == OUTOK) {
        mPrintf("Total messages found: %d\n ", totfnd);
/*  Total new bulletins: 2  */
/*  Collecting new files:  22 new files  */
    }
    sprintf(tempstr,"%d", totfnd);
    fputs(tempstr, qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    sprintf(tempstr,"%d", totconf-1);
    fputs(tempstr, qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    for (rover = 0; rover < MAXROOMS; rover++) {
        if (UsedQwkRooms[rover].messagesfound != 0) {
            sprintf(tempstr, "%d", rover);
            fputs(tempstr, qwkctlfd);
            fputc('\r', qwkctlfd);
            fputc(NEWLINE, qwkctlfd);
            fputs(roomTab[rover].rtname, qwkctlfd);
            fputc('\r', qwkctlfd);
            fputc(NEWLINE, qwkctlfd);
        }
    }
    fputs("BULLETIN.QWK", qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    fputs("NEWS.QWK", qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    fputs("GOODBYE.QWK", qwkctlfd);
    fputc('\r', qwkctlfd);
    fputc(NEWLINE, qwkctlfd);
    fclose(qwkctlfd);
    fclose(qwkmsgfd);
    fclose(qwkpersfd);
    if (outFlag == OUTOK && totfnd > 0) {
        mPrintf("Would you like to receive this packet,\n ");
        mPrintf("[Y]es, [N]o, [G]oodbye when done? ");
        c = toUpper(iChar());
        mPrintf("\n ");
        switch (c) {
            case 'Y':
            case 'G':
                mPrintf("Packing QWK packet with %s\n ", GetCompEnglish(qwkdef.Archiver));
                packetSize = ArchQWK();
                mPrintf("Packet size: %lu bytes\n ", packetSize);
                sprintf(tempstr, "%s.qwk",(cfg.codeBuf+cfg.nodeTitle));
                mPrintf("Start your %s download of %s now\n ",
                FindProtoName(qwkdef.DownProtocol), tempstr);
                strcpy(tempstr2, (cfg.codeBuf + cfg.QWKWorkArea.saDirname));
                success = strlen(tempstr2);
                tempstr2[success-1]=NULL;
/*              printf("%s\n", tempstr2);  */
                if (chdir(tempstr2) == BAD_DIR)
                    printf("chdir failed!\n");
/*              printf("%s\n", getcwd(NULL, 100));
                iChar();                              */
                FileTransStat = FL_START;
                TranFiles(qwkdef.DownProtocol, "", tempstr, FALSE);
                if (c=='G')
                    HangUp(TRUE);
/*                printf("%d\n", FileTransStat);  */
                if (FileTransStat==FL_SUCCESS)
                    UpdateLastRead();
                break;
        }
    }
    outFlag = OUTOK;
    CleanUpPacket();
    homeSpace();
}


/************************************************************************/
/*      ExtractAndAdd() Reads the selected room for new messages and    */
/*      places them in the QWK packet in proper format.                 */
/************************************************************************/
void ExtractAndAdd(int room)
{
    union Converter {
        unsigned char uc[10];
        unsigned int  ui[5];
        unsigned long ul[2];
        float          f[2];
        double         d[1];
    };
    union Converter t;
    int sign, exp;
    struct qwkheader qwkhead;
    int rover, length, chrcnt, blocks, tempcheck, tc;
    SYS_FILE tempname;
    char *mp;
    char nwlcp= 227;
    int      i,h,j, start, finish, increment, MsgCount = 0, result;
    MSG_NUMBER lowLim, highLim, msgNo, MsgRead;
    char       pulled, PEUsed = FALSE, LoopIt, tempstr[50];

    getRoom(room);
    if (room == MAILROOM)
        fillMailRoom();                         /* update room also */

    start = 0;
    finish      = (room == MAILROOM) ? MAILSLOTS : MSGSPERRM;
    increment   = 1;
    lowLim  = QwkRooms[room].LastMessageNo + 1l;
    highLim = cfg.newest;

    if (cfg.oldest  > lowLim) {
        lowLim = cfg.oldest;
    }
    MsgRead = lowLim;

    sprintf(tempstr, "%03d.ndx", room);
    makeSysName(tempname, tempstr, &cfg.QWKWorkArea);
    if ((qwkindfd=fopen(tempname, "wb")) == NULL) {
        sprintf(tempstr,"Cannot create index file for conference %d", room);
        crashout(tempstr);
    }

    for (i = start; i != finish && (onLine()) &&
         totfnd < qwkdef.MaxPacket && outFlag == OUTOK &&
         UsedQwkRooms[room].messagesfound < qwkdef.MaxRoom; i++ ) {
        msgNo = (roomBuf.msg[i].rbmsgNo & S_MSG_MASK);
        /*
         * Now check to see if msg is in "to be read" range, OR if we are
         * reading New AND the message is marked as SKIPPED (only happens in
         * Mail).  Note at the moment we're not going to worry about net
         * mode -- we don't use this loop for sending Mail, although we do
         * for other rooms.
         */
        if (
                (msgNo >= lowLim && highLim >= msgNo)
         ) {
            noStop = LOADIT;
            if (findMessage(roomBuf.msg[i].rbmsgLoc, msgNo, TRUE) &&
                OkToSend()) {
                noStop=NORMAL;
                UsedQwkRooms[room].messagesfound++;
                if (msgNo > MsgRead)
                    MsgRead = msgNo;
                totfnd++;
                memset(&qwkhead, ' ', 128);
                sprintf(tempstr, "/\n /%c/", 227);
                qwkglobalreplace(msgBuf->mbtext, tempstr);
                sprintf(tempstr, "/\n/%c/", ' ');
                qwkglobalreplace(msgBuf->mbtext, tempstr);
                length = strlen(msgBuf->mbtext);
                length = (length / 79)+length;
                blocks = (length / 128)+2;
                qwkhead.status = ' ';
                itoa(totfnd, qwkhead.number, 10);
                tempcheck = strlen(qwkhead.number);
                qwkhead.number[tempcheck]=' ';
                qwkReadDate(msgBuf->mbdate, qwkhead.date);
                tempcheck = strlen(qwkhead.date);
                qwkhead.date[tempcheck]=' ';
                qwkReadTime(msgBuf->mbtime, qwkhead.time);
                tempcheck = strlen(qwkhead.time);
                qwkhead.time[tempcheck]=' ';
                if (!msgBuf->mbto[0])
                    strcpy(qwkhead.to, "ALL");
                else
                    strncpy(qwkhead.to, strupr(msgBuf->mbto), 25);
                tempcheck = strlen(qwkhead.to);
                qwkhead.to[tempcheck]=' ';
                if (strcmpi(msgBuf->mbto, logBuf.lbname) == SAMESTRING)
                    UsedQwkRooms[room].messagestoyou++;
                if (!roomBuf.rbflags.ANON) {
                    strcpy(qwkhead.from, strupr(msgBuf->mbauth));
                    tempcheck = strlen(qwkhead.from);
                    qwkhead.from[tempcheck]=' ';
                }
                strncpy(qwkhead.subject, msgBuf->mbSubj, 25);
                memset(qwkhead.password, ' ', 12);
                itoa(UsedQwkRooms[room].messagesfound, qwkhead.ref_no, 10);
                tempcheck = strlen(qwkhead.ref_no);
                qwkhead.ref_no[tempcheck]=' ';
                itoa(blocks, qwkhead.size, 10);
                tempcheck = strlen(qwkhead.size);
                qwkhead.size[tempcheck]=' ';
                qwkhead.active = 225;
                qwkhead.conf = room;
                qwkhead.logical_msgno = totfnd;
                qwkhead.nettag= ' ';
                currentsector++;
                fwrite(&qwkhead, 128, 1, qwkmsgfd);
                t.f[0] = (float) currentsector;
                sign = t.uc[3] / 0x80;
                exp = ((t.ui[1] >> 7) - 0x7f + 0x81) & 0xff;
                t.ui[1] = (t.ui[1] & 0x7f) | (sign << 7) | (exp << 8);
                fwrite(&t.f[0], 4, 1, qwkindfd);
                fwrite(0,1,1,qwkindfd);
                if (strcmpi(msgBuf->mbto, logBuf.lbname) == SAMESTRING) {
                    fwrite(&t.f[0], 4, 1, qwkpersfd);
                    fwrite(0,1,1,qwkpersfd);
                }
                mp = msgBuf->mbtext;
                chrcnt = 0;
                for (h = 1; h < blocks; h++) {
                    memset(qwkbuf, ' ', 128);
                    for (j=0; j < 128; j++) {
                        if (*mp != NULL) {
                            if (*mp == nwlcp)
                                chrcnt=0;
                            if (j < 128) {
                                qwkbuf[j]=*mp;
                                chrcnt++;
                                mp++;
                            }
                            if (chrcnt==79) {
                                tc=j;
                                do {
                                   if (tc >= 0 && qwkbuf[tc] == ' ') {
                                       qwkbuf[tc]=227;
                                       chrcnt = 0;
                                   }
                                   else {
                                       chrcnt--;
                                       tc--;
                                   }
                                   if (chrcnt == 45) {
                                       j++;
                                       qwkbuf[j]=227;
                                       chrcnt=0;
                                   }
                                } while (chrcnt > 44);
                            }
                        } else {
                            qwkbuf[j]=227;
                            break;
                        }
                    }
                    fwrite(qwkbuf, 128, 1, qwkmsgfd);
                    currentsector++;
                }
            }
        }
    }
    fclose(qwkindfd);
    noStop=NORMAL;
    if (UsedQwkRooms[room].messagesfound == 0) {
        sprintf(tempstr, "%03d.ndx", room);
        makeSysName(tempname, tempstr, &cfg.QWKWorkArea);
        unlink(tempname);
    } else {
        totconf++;
        QwkRooms[room].CurrentLastMessageNo = MsgRead;
    }
}

/************************************************************************/
/*      OkToSend() Checks the current message to make sure it is a      */
/*      selected message to be sent.                                    */
/************************************************************************/
char OkToSend()
{
/*    if ((strcmpi(msgBuf->mbauth, logBuf.lbname)==SAMESTRING) && qwkdef.YourOwn)
        return TRUE;  */
    if ((strcmpi(msgBuf->mbauth, logBuf.lbname) == SAMESTRING) &&
         !qwkdef.YourOwn)
        return FALSE;
    return TRUE;
}

/************************************************************************/
/*      menus() Main menu routine.                                      */
/************************************************************************/
void menus()
{
    char letter, done = FALSE;

    do {
        outFlag = OUTOK;
        mPrintf("\n \n Asgard-86 THOR Offline Message Door (%s)\n ", VERSION);
        mPrintf("[D]ownload QWK packet\n ");
        mPrintf("[U]pload REP packet\n ");
        mPrintf("[C]onfigure your settings\n ");
        mPrintf("[H]elp with THOR\n ");
        mPrintf("[G]oodbye, hang up\n ");
        mPrintf("[Q]uit back to %s ", cfg.codeBuf + cfg.nodeTitle);
        letter = toUpper(iChar());
        mPrintf("\n ");
        switch (letter) {
            case 'C': QWKUserConfig();              break;
            case 'D': CreateQWKPacket();          break;
            case 'U': InputREPPacket();          break;
            case 'H': tutorial(FALSE, "qwkmain.hlp", TRUE, FALSE); break;
            case 'G': HangUp(TRUE);              break;
            case 'Q': done = TRUE;               break;
            default: done = TRUE;                break;
        }
        if (!done) writeSysTab();
    } while (!done);
}

/************************************************************************/
/* globalreplace()                                                      */
/* Replace the first delimited string with the second delimited         */
/* string.  An optional repeat count precedes the 1st delimiter.        */
/* If a repeat count isn't given, it is assumed to be MAXTEXT+1.        */
/* The delimiter may be any character except a numeric character.       */
/************************************************************************/
void qwkglobalreplace(char *buf, char *qwkold)
{
#define STRINGSIZE  (4*SECTSIZE)

        char    string[STRINGSIZE], *p, *bufend, delim, *old, *new;
        int     i, cnt, oldlen, newlen, diff;

        if (!*qwkold)
            return;             /* Quit if no string */

        cnt = (int) atoi(qwkold);
        if (!cnt)
            cnt = strlen(buf);
        p = qwkold;
        while (isdigit(*p))       /* Skip over the count if any */
          p++;

        delim = *p;
        new = strchr(old=++p, delim) + 1;
        p = strchr(new, delim);
        new[-1]=                  /* Terminate old string */
        *p = 0;                   /* Terminate new string */
        newlen = strlen(new);
        oldlen = strlen(old);
        diff = newlen - oldlen;

        if (!*old)                /* keep dolts from entering a */
            return;               /* NULL as the search string. */

        for (p = buf, i = cnt; bufend = buf+strlen(buf), p < bufend && i &&
            onLine() && !outFlag; p += newlen, i--) {
            p = matchString(p, old, bufend, TRUE, TRUE);
            if (!p)
                break;
            if (newlen > oldlen && diff >= (MAXTEXT - strlen(buf) - 1)) {
/*                mPrintf(" Buffer Full\n ");  */
                break;
            }
            crtColumn = 1;
/*            mPrintf("%4d\b\b\b\b", cnt-i);  */
            replace(p, new, oldlen, newlen);
        }
        outFlag = OUTOK;
/*        mPrintf(" Replaced %d", cnt -i);
        doCR();                           */
}

/************************************************************************/
/* qwkReadDate()                                                        */
/*                                                                      */
/* This function interprets the citadel date string and returns a       */
/* QWK formatted date string.                                           */
/************************************************************************/
int qwkReadDate(char *date, char *datestr)
{
    int rover, found;
    int   year, month, day, hours, minutes, seconds, milli;
    label mon;

    if (!date[0]) {
        year = 90;
        month = 1;
        day = 1;
    } else {
        if (!isdigit(date[0])) {
            year = 90;
            month = 1;
            day = 1;
        } else {
            year = atoi(date);
            while (isdigit(*date)) date++;

            for (rover = 0; isalpha(*date); date++, rover++)
                mon[rover] = toUpper(*date);

            mon[rover] = 0;

            if (rover == 0) {
                year = 90;
                month = 1;
                day = 1;
            } else {
                for (found = rover = 0; rover < NumElems(MonthTab); rover++)
                    if (strncmp(mon, MonthTab[rover], strLen(mon)) == SAMESTRING) {
                        found++;
                        month = rover + 1;
                    }

                if (found != 1)
                    month = 1;

                if ((day = atoi(date)) == 0)
                    day = 1;
            }
        }
    }
    sprintf(datestr, "%02d-%02d-%02d", month, day, year);
    return;
}

/************************************************************************/
/* qwkReadTime()                                                        */
/*                                                                      */
/* This function interprets the citadel time string and returns a       */
/* QWK formatted time string.                                           */
/************************************************************************/
int qwkReadTime(char *time, char *timestr)
{
    int rover, found;
    int   month, day, hours, minutes, seconds, milli;
    label mon;

    if (!time[0]) {
        hours = 00;
        minutes = 00;
    } else {
        if (!isdigit(time[0])) {
            hours = 00;
            minutes = 00;
        } else {
            hours = atoi(time);
            while (isdigit(*time)) time++;
            time++;
            minutes = atoi(time);
            while (isdigit(*time)) time++;

            if (strchr(time,'P') || strchr(time, 'p'))
                hours += 12;
        }
    }
    sprintf(timestr, "%02d:%02d", hours, minutes);
    return;
}

/************************************************************************/
/* RepReadDate()                                                        */
/*                                                                      */
/* This function interprets the QWK REP date string and returns a       */
/* Citadel formatted date string.                                       */
/************************************************************************/
int RepReadDate(char *date, char *datestr)
{
    char  *monthTab[13] = {"", "Jan", "Feb", "Mar",
                           "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep",
                           "Oct", "Nov", "Dec" };
    int rover, found;
    int   year, month, day, hours, minutes, seconds, milli;
    label mon;

    if (!date[0]) {
        year = 0;
        month = 0;
        day = 0;
    } else {
        if (!isdigit(date[0])) {
            year = 0;
            month = 0;
            day = 0;
        } else {
            month = atoi(date);
            while (isdigit(*date)) date++;
            date++;

            day = atoi(date);
            while (isdigit(*date)) date++;
            date++;

            year = atoi(date);
            while (isdigit(*date)) date++;
            date++;
        }
    }
    if (year !=0 && month !=0 && day != 0)
        sprintf(datestr, "%02d%3s%02d", year, monthTab[month-1], day);
    else
        datestr[0]=NULL;
    return;
}

/************************************************************************/
/* RepReadTime()                                                        */
/*                                                                      */
/* This function interprets the QWK REP time string and returns a       */
/* Citadel formatted time string.                                       */
/************************************************************************/
int RepReadTime(char *time, char *timestr)

{
    int rover, found;
    int   month, day, hours, minutes, seconds, milli;
    label mon;

    if (!time[0]) {
        hours = 00;
        minutes = 00;
    } else {
        if (!isdigit(time[0])) {
            hours = 00;
            minutes = 00;
        } else {
            hours = atoi(time);
            while (isdigit(*time)) time++;
            time++;
            minutes = atoi(time);
            while (isdigit(*time)) time++;
        }
    }
    if (hours != 0 && minutes != 0)
        sprintf(timestr, "%02d:%02d %s", (hours > 12) ? hours-12 : hours,
                minutes, (hours > 12) ? "pm" : "am" );
    else
        timestr[0]=NULL;
    return;
}

/************************************************************************/
/* ResetRooms()                                                         */
/*                                                                      */
/* This function is a menu system for selecting and reseting message    */
/* pointers for QWK packets.                                            */
/************************************************************************/
void ResetRooms()
{
    char done = FALSE;
    char letter, letter1;
    int rover;
    long sessioncheck, offset;
    outFlag = OUTOK;
    do {
        mPrintf("\n THOR QWK Mail Reader Message Reset menu:\n ");
        mPrintf("[0] Reset to 0 (all new)\n ");
        mPrintf("[1] Reset to newest number (all old)\n ");
        mPrintf("[2] Reset to beginning of previous call\n ");
        mPrintf("[3] Reset to last read pointers from BBS\n ");
        mPrintf("[4] Reset to newest number - inputed valued\n ");
/*        mPrintf("[5] Upload %s.PTR file\n ", (cfg.codeBuf + cfg.nodeTitle));  */
        mPrintf("[H]elp with message reset\n ");
        mPrintf("[Q]uit THOR QWK Mail Reader (c) reset menu\n ");
        sessioncheck=chkTimeSince(USER_SESSION) - InChatTime;
        if (sessionLimit > 0)
            mPrintf("You have been on for %lu minutes, and have %lu minutes left.\n ",
            (sessioncheck / 60), (((sessionLimit * 60) - sessioncheck) / 60));
        mPrintf("\n RESET MENU [0 1 2 3 4 H Q]: ");
        letter1 = toUpper(iChar());
        mPrintf("\n ");
        switch (letter1) {
            case '0':
                for (rover = 0; rover < MAXROOMS; rover++) {
                    QwkRooms[rover].LastMessageNo = 0l;
                }
                break;
            case '1':
                for (rover = 0; rover < MAXROOMS; rover++) {
                    QwkRooms[rover].LastMessageNo = cfg.newest;
                }
                break;
            case '2':
                for (rover = 0; rover < MAXROOMS; rover++) {
                    QwkRooms[rover].LastMessageNo =
                    QwkRooms[rover].OldLastMessageNo;
                }
                break;
            case '3':
                for (rover = 0; rover < MAXROOMS; rover++) {
                    QwkRooms[rover].LastMessageNo =
                     logBuf.lbvisit[logBuf.lbgen[rover] & CALLMASK];
                }
                break;
            case '4':
                offset = getNumber("Number of message from last message: ", 1, cfg.MsgsPerrm);
                for (rover = 0; rover < MAXROOMS; rover++) {
                    QwkRooms[rover].LastMessageNo =
                        roomTab[rover].rtlastMessage - offset;
                }
                break;
            case '5':
                mPrintf("not active at this time\n ");
                break;
            case 'Q': done = TRUE;               break;
            case 'H':
            default:
                tutorial(FALSE, "qwkreset.hlp", TRUE, FALSE);
                break;
        }
    } while (!done);
    return;
}

/************************************************************************/
/* ArchQWK()                                                         */
/*                                                                      */
/* This function will archive all files from the QWKworkarea using      */
/* whatever archiving method the BBS supports and the user has          */
/* selected.  It will then return the size of the resulting file.       */
/************************************************************************/
long ArchQWK()
{
    FILE *fbuf;
    char tempStr[20], tempstr[30];
    int check;
    long fileSize = 0l;

    sprintf(tempStr, "%s.qwk",(cfg.codeBuf + cfg.nodeTitle));
    makeSysName(tempname, tempStr, &cfg.QWKWorkArea);
    strcpy(tempstr, "*.*");
    makeSysName(tempFile, tempstr, &cfg.QWKWorkArea);
    Compress(qwkdef.Archiver, tempFile, tempname);
    if ((fbuf = safeopen(tempname, READ_ANY)) != NULL) {
        totalBytes(&fileSize, fbuf);
        fclose(fbuf);
    }
/*    printf("ArchQWK C1: %u, %s\n", fileSize, tempname);
    iChar();  */
    return fileSize;
}

/************************************************************************/
/* InputREPPacket()                                                     */
/*                                                                      */
/* This function receive a file from the user into th QWLworkarea, and  */
/* then un-archive it using the method the user has selected in user    */
/* configuration.  And then it will call the import routine to import   */
/* the message into the message base.                                   */
/************************************************************************/
void InputREPPacket()
{
    char tempstr[30], tempstr2[30], tempstr3[20];
    int success;

    sprintf(tempstr, "%s.rep",(cfg.codeBuf+cfg.nodeTitle));
    sprintf(tempstr3, "%s.msg",(cfg.codeBuf+cfg.nodeTitle));
    mPrintf("Start your %s upload of %s now\n ",
                   FindProtoName(qwkdef.UpProtocol), tempstr);
    strcpy(tempstr2, (cfg.codeBuf + cfg.QWKWorkArea.saDirname));
    success = strlen(tempstr2);
    tempstr2[success-1]=NULL;
    if (chdir(tempstr2) == BAD_DIR)
        printf("chdir failed!\n");
    FileTransStat = FL_START;
    upLoad(qwkdef.UpProtocol, tempstr, FALSE);
    if (qwkdef.Hangafterupload)
        HangUp(TRUE);
    if (FileTransStat==FL_SUCCESS) {
        QWKDeCompress(qwkdef.Archiver, tempstr, tempstr3);
        homeSpace();
        ImportQWKPacket();
        strcpy(tempstr2, (cfg.codeBuf + cfg.QWKWorkArea.saDirname));
        success = strlen(tempstr2);
        tempstr2[success-1]=NULL;
        if (chdir(tempstr2) == BAD_DIR)
            printf("chdir failed!\n");
    }
    unlink(tempstr);
    unlink(tempstr3);
    homeSpace();
    return;
}

/************************************************************************/
/* ImportQWKPacket()                                                    */
/*                                                                      */
/* This function reads the files from the REP packet and places the     */
/* messages in the proper rooms in the message base.                    */
/************************************************************************/
void ImportQWKPacket()
{
    struct qwkheader qwkhead;
    int rover, length, chrcnt, blocks, tempcheck, tc;
    SYS_FILE tempname;
    char *mp;
    char nwlcp= 227;
    char endofmess = 225;
    int      i,h,j, start, finish, increment, MsgCount = 0, result, savemess;
    MSG_NUMBER lowLim, highLim, msgNo;
    char       pulled, PEUsed = FALSE, LoopIt, tempstr[50];
    char      *pc, allUpper;
    extern SListBase BadWords;
    extern char BadMessages[];

    sprintf(tempstr, "%s.msg",(cfg.codeBuf+cfg.nodeTitle));
    makeSysName(tempname, tempstr, &cfg.QWKWorkArea);
    if ((qwkmsgfd=fopen(tempname, "rb")) == NULL) {
        mPrintf("Cannot open MSG file from REP packet!\n ");
        return;
/*        crashout(tempstr);  */
    }
    fread(qwkbuf, sizeof qwkbuf, 1, qwkmsgfd);
    tempcheck = 1;
    do {
        memset(&qwkhead, ' ', 128);
        tempcheck = fread(&qwkhead, sizeof qwkhead, 1, qwkmsgfd);
        if (tempcheck == 1) {
            blocks = atoi(qwkhead.size);
            rover = qwkhead.conf;
            savemess = TRUE;
            if (rover < MAXROOMS && rover >= 0)
                getRoom(rover);
            else {
                rover = LOBBY;
                getRoom(LOBBY);
                savemess = FALSE;
            }
            if (!QwkRooms[rover].Selected)
                savemess = FALSE;
            ZeroMsgBuffer(msgBuf);
            /*  This is were we fill in the header information for the
             *  incoming message, making sure to use the real name
             *  or the handle name from logbuf depending on the room's
             *  fido aware status flag.   We also need to get the room
             *  number out of the qwkhead.conf area, make sure the date
             *  is in Asgard format and setup special checks for the mail
             *  room.  */
            strCpy(msgBuf->mbroom, roomBuf.rbname);
            strcpy(msgBuf->mbauth, logBuf.lbname);
            setMisc();
            if (roomBuf.rbflags.FIDO) {
                if (logBuf.lbflags.FidoWrite)
                    strcpy(msgBuf->mbauth, logBuf.lbRealname);
                else
                    savemess=FALSE;
                memset(msgBuf->mbto, NULL, 26);
                strncpy(msgBuf->mbto, qwkhead.to, 25);
                memset(msgBuf->mbSubj, NULL, 73);
                strncpy(msgBuf->mbSubj, qwkhead.subject, 25);
            }
            strcpy(msgBuf->mbroom, roomBuf.rbname);
            if (rover == MAILROOM) {
                memset(msgBuf->mbto, NULL, 26);
                strncpy(msgBuf->mbto, qwkhead.to, 25);
                CleanEnd(msgBuf->mbto);
                if (!getRecipient())
                    savemess=FALSE;
            }
            memset(tempstr, NULL, 20);
            strncpy(tempstr, qwkhead.date, 8);
            RepReadDate(tempstr, msgBuf->mbdate);
            memset(tempstr, NULL, 20);
            strncpy(tempstr, qwkhead.time, 5);
            RepReadTime(tempstr, msgBuf->mbtime);
            chrcnt=0;
            for (h = 1; h < blocks; h++) {
                memset(qwkbuf, ' ', 128);
                tempcheck = fread(qwkbuf, sizeof qwkbuf, 1, qwkmsgfd);
                for (j = 0; j < 128; j++) {
                    if (qwkbuf[j] != nwlcp)
                        msgBuf->mbtext[chrcnt++] = qwkbuf[j];
                    else {
                        msgBuf->mbtext[chrcnt++] = NEWLINE;
                        msgBuf->mbtext[chrcnt++] = ' ';
                    }
                    if (chrcnt > 7449) {
                        msgBuf->mbtext[chrcnt++] = NULL;
                        CleanEnd(msgBuf->mbtext);
                        if (cfg.BoolFlags.NetScanBad) {
                            if (rover != MAILROOM && SearchList(&BadWords, msgBuf->mbtext) != NULL) {
                                savemess=FALSE;
                                DiscardMessage("discard");
/*                                sPrintf(msgBuf->mbtext,
                                  "QWK message from %s @%s in %s discarded for decency reasons.",
                                  msgBuf->mbauth, msgBuf->mboname,
                                  (roomExists(msgBuf->mbroom)) ?
                                  formRoom(roomExists(msgBuf->mbroom), FALSE, FALSE) :
                                  msgBuf->mbroom);
                                netResult(msgBuf->mbtext);  */
                            }
                        }
                        if (savemess) {
                            for (pc=msgBuf->mbtext, allUpper=TRUE;   *pc && allUpper;  pc++) {
                                if (toUpper(*pc) != *pc)   allUpper = FALSE;
                            }
                            if (allUpper)   fakeFullCase(msgBuf->mbtext);
                            putMessage(&logBuf);
                        }
                        chrcnt =0;
                        memset(msgBuf->mbtext, NULL, MAXTEXT);
                    }
                }
            }
            msgBuf->mbtext[chrcnt++] = NULL;
            CleanEnd(msgBuf->mbtext);
            if (cfg.BoolFlags.NetScanBad) {
                if (rover != MAILROOM && SearchList(&BadWords, msgBuf->mbtext) != NULL) {
                    savemess=FALSE;
                    DiscardMessage("discard");
/*                    sPrintf(msgBuf->mbtext,
                      "QWK message from %s @%s in %s discarded for decency reasons.",
                      msgBuf->mbauth, msgBuf->mboname,
                      (roomExists(msgBuf->mbroom)) ?
                      formRoom(roomExists(msgBuf->mbroom), FALSE, FALSE) :
                      msgBuf->mbroom);
                    netResult(msgBuf->mbtext);  */
                    savemess=FALSE;
                }
            }
            if (savemess) {
                for (pc=msgBuf->mbtext, allUpper=TRUE;   *pc && allUpper;  pc++) {
                    if (toUpper(*pc) != *pc)   allUpper = FALSE;
                }
                if (allUpper)   fakeFullCase(msgBuf->mbtext);
                putMessage(&logBuf);
            }
        } else
            fclose(qwkmsgfd);
    } while (tempcheck == 1);
    fclose(qwkmsgfd);
    return;
}

/************************************************************************/
/* CleanUpPacket()                                                      */
/*                                                                      */
/* This function deletes all files created in the QWKworkarea after     */
/* a packet is processed, good or bad.                                  */
/************************************************************************/
void CleanUpPacket()
{
    char tempstr[30], tempstr2[100];
    int rover;

    strcpy(tempstr2, (cfg.codeBuf + cfg.QWKWorkArea.saDirname));
    rover = strlen(tempstr2);
    tempstr2[rover-1]=NULL;
/*            printf("%s\n", tempstr2);  */
    if (chdir(tempstr2) == BAD_DIR)
        printf("chdir failed!\n");
/*            printf("%s\n", getcwd(NULL, 100));
            iChar();                              */
    getcwd(tempstr2, 100);
    printf("Current Directory: %s\n ", tempstr2);
    for (rover = 0; rover < MAXROOMS; rover++) {
        if (UsedQwkRooms[rover].messagesfound != 0) {
            sprintf(tempstr, "%03d.ndx", rover);
            unlink(tempstr);
        }
    }
    strcpy(tempstr, "personal.ndx");
    unlink(tempstr);
    sprintf(tempstr, "messages.dat");
    unlink(tempstr);
    sprintf(tempstr, "control.dat");
    unlink(tempstr);
    sprintf(tempstr, "%s.qwk",(cfg.codeBuf + cfg.nodeTitle));
    unlink(tempstr);
}

