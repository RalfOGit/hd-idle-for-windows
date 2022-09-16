/*
 * hd-idle.c - external disk idle daemon
 *
 * Copyright (c) 2007 Christian Mueller.
 * Copyright (c) 2022 RalfOGit
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * hd-idle is a utility program for spinning-down external disks after a period
 * of idle time. Since most external IDE disk enclosures don't support setting
 * the IDE idle timer, a program like hd-idle is required to spin down idle
 * disks automatically.
 *
 * A word of caution: hard disks don't like spinning-up too often. Laptop disks
 * are more robust in this respect than desktop disks but if you set your disks
 * to spin down after a few seconds you may damage the disk over time due to the
 * stress the spin-up causes on the spindle motor and bearings. It seems that
 * manufacturers recommend a minimum idle time of 3-5 minutes, the default in
 * hd-idle is 10 minutes.
 *
 * Please note that hd-idle can spin down any disk accessible via the SCSI
 * layer (USB, IEEE1394, ...) but it will NOT work with real SCSI disks because
 * they don't spin up automatically. Thus it's not called scsi-idle and I don't
 * recommend using it on a real SCSI system unless you have a kernel patch that
 * automatically starts the SCSI disks after receiving a sense buffer indicating
 * the disk has been stopped. Without such a patch, real SCSI disks won't start
 * again and you can as well pull the plug.
 *
 * You have been warned...
 *
 * CVS Change Log:
 * ---------------
 *
 * Changes:
 * - ported to windows api
 * - use ata commands instead of scsi commands for windows systems
 *
 * $Log: hd-idle.c,v $
 * Revision 1.8  2021/03/03 19:53:51  ralfogit
 * Version 1.06
 * ------------
 * 
 * $Log: hd-idle.c,v $
 * Revision 1.7  2014/04/06 19:53:51  cjmueller
 * Version 1.05
 * ------------
 *
 * Bugs:
 * - Allow SCSI device names with more than one character (e.g. sdaa) in case
 *   there are more than 26 SCSI targets.
 *
 * Revision 1.6  2010/12/05 19:25:51  cjmueller
 * Version 1.03
 * ------------
 *
 * Bugs
 * - Use %u in dprintf() when reporting number of reads and writes (the
 *   corresponding variable is an unsigned int).
 * - Fix example in README where the parameter "-a" was written as "-n".
 *
 * Revision 1.5  2010/11/06 15:30:04  cjmueller
 * Version 1.02
 * ------------
 *
 * Features
 * - In case the SCSI stop unit command fails with "check condition", print a
 *   hex dump of the sense buffer to stderr. This is supposed to help
 *   debugging.
 *
 * Revision 1.4  2010/02/26 14:03:44  cjmueller
 * Version 1.01
 * ------------
 *
 * Features
 * - The parameter "-a" now also supports symlinks for disk names. Thus, disks
 *   can be specified using something like /dev/disk/by-uuid/... Use "-d" to
 *   verify that the resulting disk name is what you want.
 *
 *   Please note that disk names are resolved to device nodes at startup. Also,
 *   since many entries in /dev/disk/by-xxx are actually partitions, partition
 *   numbers are automatically removed from the resulting device node.
 *
 * Bugs
 * - Not really a bug, but the disk name comparison used strstr which is a bit
 *   useless because only disks starting with "sd" and a single letter after
 *   that are currently considered. Replaced the comparison with strcmp()
 *
 * Revision 1.3  2009/11/18 20:53:17  cjmueller
 * Features
 * - New parameter "-a" to allow selecting idle timeouts for individual disks;
 *   compatibility to previous releases is maintained by having an implicit
 *   default which matches all SCSI disks
 *
 * Bugs
 * - Changed comparison operator for idle periods from '>' to '>=' to prevent
 *   adding one polling interval to idle time
 * - Changed sleep time before calling sync after updating the log file to 1s
 *   (from 3s) to accumulate fewer dirty blocks before synching. It's still
 *   a compromize but the log file is for debugging purposes, anyway. A test
 *   with fsync() was unsuccessful because the next bdflush-initiated sync
 *   still caused spin-ups.
 *
 * Revision 1.2  2007/04/23 22:14:27  cjmueller
 * Bug fixes
 * - Comment changes; no functionality changes...
 *
 * Revision 1.1.1.1  2007/04/23 21:49:43  cjmueller
 * initial import into CVS
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <errno.h>
#include <Windows.h>
#include <Ntddscsi.h>
#define sleep(x) Sleep((x)*1000)  //Sleep((x)*50)
#define sync() /*nop*/
#include <stdarg.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN64
typedef __int64 ssize_t;
#elif defined WIN32
typedef long ssize_t;
#endif

#define STAT_FILE "/proc/diskstats"
#define DEFAULT_IDLE_TIME 60

#define dprintf if (debug) printf

/* typedefs and structures */
typedef struct IDLE_TIME {
    struct IDLE_TIME  *next;
    char              *name;
    int                idle_time;
} IDLE_TIME;

typedef struct DISKSTATS {
    struct DISKSTATS  *next;
    char               name[50];
    int                idle_time;
    time_t             last_io;
    time_t             spindown;
    time_t             spinup;
    unsigned int       spun_down : 1;
    unsigned int       reads;
    unsigned int       writes;
} DISKSTATS;

/* function prototypes */
static DISKSTATS  *get_diskstats   (const char *name);
static void        spindown_disk   (const char *name);
static int         ata_check_power_mode(const char *name);
static bool        ata_set_idle_mode(const char *name);
static bool        ata_set_standby_mode(const char *name);
static char       *disk_name       (char *name);
static void        phex            (const void *p, int len, const char *fmt, ...);
extern int         getopt          (int nargc, char *const nargv[], const char *ostr);
extern char       *optarg;         /* argument associated with option */
extern int         opterr;// = 1;     /* if error message should be printed */
extern int         optind;// = 1;     /* index into parent argv vector */
extern int         optopt;         /* character checked for validity */
extern int         optreset;       /* reset getopt */


/* global/static variables */
IDLE_TIME *it_root;
DISKSTATS *ds_root;
char *logfile = "/dev/null";
int debug = 1;

/* main function */
int main(int argc, char *argv[]) {
    IDLE_TIME *it;
    int have_logfile = 0;
    int min_idle_time;
    int sleep_time;
    int opt;

    /* create default idle-time parameter entry */
    if ((it = (IDLE_TIME*)malloc(sizeof(*it))) == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    it->next = NULL;
    it->name = NULL;
    it->idle_time = DEFAULT_IDLE_TIME;
    it_root = it;

    /* process command line options */
    while ((opt = getopt(argc, argv, "t:a:i:l:dh")) != -1) {
        switch (opt) {

        case 't':
            /* just spin-down the specified disk and exit */
            spindown_disk(optarg);
            return 0;

        case 'a':
            /* add a new set of idle-time parameters for this particular disk */
            if ((it = (IDLE_TIME*)malloc(sizeof(*it))) == NULL) {
                fprintf(stderr, "out of memory\n");
                return 2;
            }
            it->name = disk_name(optarg);
            it->idle_time = DEFAULT_IDLE_TIME;
            it->next = it_root;
            it_root = it;
            break;

        case 'i':
            /* set idle-time parameters for current (or default) disk */
            it->idle_time = atoi(optarg);
            break;

        case 'l':
            logfile = optarg;
            have_logfile = 1;
            break;

        case 'd':
            debug = 1;
            break;

        case 'h':
            printf("usage: hd-idle [-t <disk>] [-a <name>] [-i <idle_time>] [-l <logfile>] [-d] [-h]\n");
            return 0;

        case ':':
            fprintf(stderr, "error: option -%c requires an argument\n", optopt);
            return 1;

        case '?':
            fprintf(stderr, "error: unknown option -%c\n", optopt);
            return 1;
        }
    }

    /* set sleep time to 1/10th of the shortest idle time */
    min_idle_time = 1 << 30;
    for (it = it_root; it != NULL; it = it->next) {
        if (it->idle_time != 0 && it->idle_time < min_idle_time) {
            min_idle_time = it->idle_time;
        }
    }
    if ((sleep_time = min_idle_time / 10) == 0) {
        sleep_time = 1;
    }
    if (sleep_time > 10) sleep_time = 10;

    /* main loop: probe for idle disks and stop them */
    for (;;) {
    DISKSTATS tmp;
    memset(&tmp, 0x00, sizeof(tmp));

    for (int i = 0; i < 255; ++i) {
        DISKSTATS *ds;
        time_t now = time(NULL);
        sprintf(tmp.name, "\\\\.\\PhysicalDrive%d", i);

        // open physical drive i  (must not set GENERIC_READ or GENERIC_WRITE, as otherwise the device will be woken up)
        HANDLE hDevice = CreateFile(tmp.name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hDevice == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            switch (error) {
            case ERROR_FILE_NOT_FOUND:
                break;  // reached end of PhysicalDriveX list
            case ERROR_ACCESS_DENIED:
                printf("probing %s: application requires admin privileges\n", tmp.name);
                break;
            }
            break;
        }

        // check if drive is already asleep, if so do not wake it up
        // this check often does not work for wd red hdds; behaviour is unclear
        BOOL fOn, r = GetDevicePowerState(hDevice, &fOn);
        if (r == 0 || fOn == FALSE) {
            dprintf("probing %s: asleep\n", tmp.name);
            /* get previous statistics for this disk */
            ds = get_diskstats(tmp.name);
            if (ds != NULL) {
                ds->spun_down = true;
            }
            CloseHandle(hDevice);
            continue;
        }

        // check if drive is a fixed drive
        char vol[sizeof(DISKSTATS::name) + 1];
        strcpy(vol, tmp.name);
        strcat(vol, "\\");
        UINT type = GetDriveTypeA(vol);
        if (type != DRIVE_FIXED) {
            switch (type) {
            case DRIVE_UNKNOWN:  	dprintf("probing %s: drive unknown\n", tmp.name); break;		// The drive type cannot be determined.
            case DRIVE_NO_ROOT_DIR: dprintf("probing %s: root path invalid\n", tmp.name); break;	// The root path is invalid; for example, there is no volume mounted at the specified path.
            case DRIVE_REMOVABLE:	dprintf("probing %s: removable media\n", tmp.name); break;		// The drive has removable media; for example, a floppy drive, thumb drive, or flash card reader.
            case DRIVE_FIXED:		dprintf("probing %s: fixed drive\n", tmp.name); break;			// The drive has fixed media; for example, a hard disk drive or an ssd drive.
            case DRIVE_REMOTE:		dprintf("probing %s: network drive\n", tmp.name); break;		// The drive is a remote(network) drive.
            case DRIVE_CDROM:		dprintf("probing %s: cdrom drive\n", tmp.name); break;			// The drive is a CD-ROM drive.
            case DRIVE_RAMDISK:		dprintf("probing %s: ramdisk\n", tmp.name); break;				// The drive is a RAM-Disk
            default:				dprintf("probing %s: unknown drive type\n", tmp.name); break;
            }
            continue;
        }

        // check ata power mode; if it wakes up your drive, just disable this part
        char *ata_power_mode_string = "";
        int ata = ata_check_power_mode(tmp.name);
        switch (ata) {
        case 0x00:  case 0x01:
            ata_power_mode_string = "standby mode";
            break;
        case 0x80:  case 0x81:  case 0x82:  case 0x83:
            ata_power_mode_string = "idle mode";
            break;
        case 0xff:
            ata_power_mode_string = "active or idle mode";
            break;
        }

        // query read and write counts
        DISK_PERFORMANCE disk_performance;
        DWORD bytesReturned = 0;
        BOOL result = DeviceIoControl(
            hDevice,                    // handle to device
            IOCTL_DISK_PERFORMANCE,     // dwIoControlCode
            NULL,                       // lpInBuffer
            0,                          // nInBufferSize
            &disk_performance,          // output buffer
            sizeof(disk_performance),   // size of output buffer
            &bytesReturned,             // number of bytes returned
            NULL                        // OVERLAPPED structure
        );
        if (result == FALSE || bytesReturned <= 0) {
            DWORD error = GetLastError();
            dprintf("probing %s: cannot query read/write counts  result %d  bytesReturned %d error 0x%dx\n", tmp.name, result, bytesReturned, error);
            CloseHandle(hDevice);
            /* if error code is "invalid function", make sure disk performance counters are enabled */
            static bool tried_diskperf = false;
            if (error == 1 && tried_diskperf == false) {
                tried_diskperf = true;
                int res = system("diskperf -YD");
            }
            continue;
        }
        CloseHandle(hDevice);

        tmp.reads = disk_performance.ReadCount;
        tmp.writes = disk_performance.WriteCount;

        /* get previous statistics for this disk */
        ds = get_diskstats(tmp.name);

        if (ds == NULL) {
            dprintf("probing %s: reads: %u, writes: %u, new disk - %s\n", tmp.name, tmp.reads, tmp.writes, ata_power_mode_string);

            /* new disk; just add it to the linked list */
            if ((ds = (DISKSTATS*)malloc(sizeof(*ds))) == NULL) {
                fprintf(stderr, "out of memory\n");
                return(2);
            }
            memcpy(ds, &tmp, sizeof(*ds));
            ds->last_io = now;
            ds->spinup = ds->last_io;
            ds->next = ds_root;
            ds_root = ds;

            /* find idle time for this disk (falling-back to default; default means
                * 'it->name == NULL' and this entry will always be the last due to the
                * way this single-linked list is built when parsing command line
                * arguments)
                */
            for (it = it_root; it != NULL; it = it->next) {
                if (it->name == NULL || !strcmp(ds->name, it->name)) {
                    ds->idle_time = it->idle_time;
                    break;
                }
            }

        }
        else if (ds->reads == tmp.reads && ds->writes == tmp.writes) {
            if (!ds->spun_down) {
                dprintf("probing %s: reads: %u, writes: %u, elapsed %llu / %d - %s\n", tmp.name, tmp.reads, tmp.writes, now - ds->last_io, ds->idle_time, ata_power_mode_string);
                /* no activity on this disk and still running */
                if (ds->idle_time != 0 && now - ds->last_io >= ds->idle_time) {
                    ata_set_standby_mode(ds->name);
                    ds->spindown = now;
                    ds->spun_down = 1;
                }
            } else {
                dprintf("probing %s: reads: %u, writes: %u, elapsed %llu / %d spun_down %u - %s\n", tmp.name, tmp.reads, tmp.writes, now - ds->last_io, ds->idle_time, ds->spun_down, ata_power_mode_string);
            }
        }
        else {
            dprintf("probing %s: reads: %u, writes: %u, elapsed %llu / %d - %s\n", tmp.name, tmp.reads, tmp.writes, now - ds->last_io, ds->idle_time, ata_power_mode_string);
            /* disk had some activity */
            if (ds->spun_down) {
                /* disk was spun down, thus it has just spun up */
                ds->spinup = now;
            }
            ds->reads = tmp.reads;
            ds->writes = tmp.writes;
            ds->last_io = now;
            ds->spun_down = 0;
        }
    }
    sleep(sleep_time);
    }

    return 0;
}


/* get DISKSTATS entry by name of disk */
static DISKSTATS *get_diskstats(const char *name)
{
    DISKSTATS *ds;

    for (ds = ds_root; ds != NULL; ds = ds->next) {
        if (!strcmp(ds->name, name)) {
            return(ds);
        }
    }

    return(NULL);
}

/* spin-down a disk */
static void spindown_disk(const char *name)
{
    DWORD dwBytesReturned = 0;
    int   iReply;
    char  io_req[6], io_repl[100];

    /* SCSI stop unit command */
    memcpy(&io_req, "\x1b\x00\x00\x00\x00\x00", 6);
    memset(&io_repl, 0x00, sizeof(io_repl));

    HANDLE hDevice = CreateFile(name,        // drive to open
        GENERIC_READ | GENERIC_WRITE,        // read and write access to the drive => set to 0 if only metadata is to be queried
        FILE_SHARE_READ | FILE_SHARE_WRITE,  // share mode           
        NULL,                                // default security attributes
        OPEN_EXISTING,                       // disposition
        0,                                   // file attributes
        NULL);                               // do not copy file attributes
    if (hDevice == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_ACCESS_DENIED:	dprintf("stop %s => failed to open device; application requires admin privileges\n", name); return;
        default:					dprintf("stop %s => failed to open device; error %d\n", name, error); return;
        }
    }

    BOOL flushed = FlushFileBuffers(hDevice);
    if (flushed == FALSE) {
        dprintf("stop %s => failed to flush file buffers / write cache\n", name);
    }

    SCSI_PASS_THROUGH s = { 0 };
    memcpy(s.Cdb, io_req, sizeof(io_req));
    s.CdbLength = sizeof(io_req);
    s.DataIn = SCSI_IOCTL_DATA_IN;
    s.TimeOutValue = 30;
    s.Length = sizeof(SCSI_PASS_THROUGH);
    s.ScsiStatus = 0x00;
    s.SenseInfoOffset = 0;
    s.SenseInfoLength = 0;
    s.DataBufferOffset = NULL;
    s.DataTransferLength = 0;

    iReply = DeviceIoControl(hDevice, IOCTL_SCSI_PASS_THROUGH_DIRECT, &s, sizeof(s), &io_repl, sizeof(io_repl), &dwBytesReturned, (LPOVERLAPPED)NULL);
    // io_repl[] : 0x38 0x00 0x00 0x00 0x00 0x00 0x06 0x00 0x01 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x1e 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x00 0x02 0x00 0x00 ...
    if (iReply == 0) {
        dprintf("stop %s => failed to pass scsi stop unit command  iReply %d\n", name, iReply);
    } else {
        dprintf("stop %s => success\n", name);
    }

    CloseHandle(hDevice);
}


static int ata_check_power_mode(const char *name)
{
    // open physical drive i  (if GENERIC_READ or GENERIC_WRITE is set, the device will be woken up)
    //HANDLE hDevice = CreateFile(name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);  
    HANDLE hDevice = CreateFile(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    //HANDLE hDevice = CreateFile(name, READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    //HANDLE hDevice = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_FILE_NOT_FOUND:
            dprintf("ata_check_power_mode(%s): ERROR_FILE_NOT_FOUND\n", name);
            break;  // PhysicalDriveX does not exist
        case ERROR_ACCESS_DENIED:
            dprintf("ata_check_power_mode(%s): ERROR_ACCESS_DENIED\n", name);
            dprintf("ata_check_power_mode(%s): application requires admin privileges\n", name);
            break;
        default:
            dprintf("ata_check_power_mode(%s): error 0x%lx\n", name, error);
        }
        return -1;
    }

    DWORD cb = 0;
    ATA_PASS_THROUGH_EX cmd = { sizeof(ATA_PASS_THROUGH_EX), 0 };
    //cmd.AtaFlags = ATA_FLAGS_DRDY_REQUIRED; /*  Require drive to be ready  */
    cmd.TimeOutValue = 3;                   /*  Arbitrary timeout (seconds)  */
    cmd.CurrentTaskFile[6] = 0xE5;          /*  "Check Power Mode" in command register  */
    if (DeviceIoControl(hDevice, IOCTL_ATA_PASS_THROUGH, &cmd, sizeof(cmd), &cmd, sizeof(cmd), &cb, 0) == 0) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_INVALID_FUNCTION:
            dprintf("ata_check_power_mode(%s): ERROR_INVALID_FUNCTION\n", name);
            break;
        case ERROR_NOT_SUPPORTED:
            dprintf("ata_check_power_mode(%s): ERROR_NOT_SUPPORTED\n", name);
            break;
        case ERROR_ACCESS_DENIED:
            dprintf("ata_check_power_mode(%s): ERROR_ACCESS_DENIED\n", name);
            dprintf("ata_check_power_mode(%s): application requires admin privileges\n", name);
            break;
        }
        return -1;
    }
    CloseHandle(hDevice);

    /*  FF in sector count register means the drive is active or idle (and therefore spinning)  */
    // 00h	Device is in Standby mode.
    // 40h  Device is in NV Cache Power Mode and the spindle is spun down or spinning down.
    // 41h  Device is in NV Cache Power Mode and the spindle is spun up or spinning up.
    // 80h  Device is in Idle mode.
    // FFh  Device is in Active mode or Idle mode.
    //dprintf("ata_check_power_mode(%s): CurrentTaskFile[0] = 0x%02x  CurrentTaskFile[1] = 0x%02x\n", name, cmd.CurrentTaskFile[0], cmd.CurrentTaskFile[1]);
    return cmd.CurrentTaskFile[1];
}


static bool ata_set_idle_mode(const char *name)
{
    // open physical drive i
    HANDLE hDevice = CreateFile(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_FILE_NOT_FOUND:
            dprintf("ata_set_idle_mode(%s): ERROR_FILE_NOT_FOUND\n", name);
            break;  // PhysicalDriveX does not exist
        case ERROR_ACCESS_DENIED:
            dprintf("ata_set_idle_mode(%s): ERROR_ACCESS_DENIED\n", name);
            dprintf("ata_set_idle_mode(%s): application requires admin privileges\n", name);
            break;
        default:
            dprintf("ata_set_idle_mode(%s): error 0x%lx\n", name, error);
        }
        return false;
    }

    DWORD cb = 0;
    ATA_PASS_THROUGH_EX cmd = { sizeof(ATA_PASS_THROUGH_EX), 0 };
    //cmd.AtaFlags = ATA_FLAGS_DRDY_REQUIRED; /*  Require drive to be ready  */
    cmd.TimeOutValue = 3;                   /*  Arbitrary timeout (seconds)  */
    cmd.CurrentTaskFile[6] = 0xE1;          /*  "IDLE IMMEDIATE" in command register */
    if (DeviceIoControl(hDevice, IOCTL_ATA_PASS_THROUGH, &cmd, sizeof(cmd), &cmd, sizeof(cmd), &cb, 0) == 0) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_INVALID_FUNCTION:
            dprintf("ata_set_idle_mode(%s): ERROR_INVALID_FUNCTION\n", name);
            break;
        case ERROR_NOT_SUPPORTED:
            dprintf("ata_set_idle_mode(%s): ERROR_NOT_SUPPORTED\n", name);
            break;
        case ERROR_ACCESS_DENIED:
            dprintf("ata_set_idle_mode(%s): ERROR_ACCESS_DENIED\n", name);
            dprintf("ata_set_idle_mode(%s): application requires admin privileges\n", name);
            break;
        }
        return false;
    }

    CloseHandle(hDevice);
    dprintf("ata_set_idle_mode(%s): SUCCESS\n", name);
    return true;
}


static bool ata_set_standby_mode(const char *name)
{
    // open physical drive i
    HANDLE hDevice = CreateFile(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDevice == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_FILE_NOT_FOUND:
            dprintf("ata_set_standby_mode(%s): ERROR_FILE_NOT_FOUND\n", name);
            break;  // PhysicalDriveX does not exist
        case ERROR_ACCESS_DENIED:
            dprintf("ata_set_standby_mode(%s): ERROR_ACCESS_DENIED\n", name);
            dprintf("ata_set_standby_mode(%s): application requires admin privileges\n", name);
            break;
        default:
            dprintf("ata_set_standby_mode(%s): error 0x%lx\n", name, error);
        }
        return false;
    }

    BOOL flushed = FlushFileBuffers(hDevice);
    if (flushed == FALSE) {
        dprintf("ata_set_standby_mode(%s): failed to flush file buffers / write cache\n", name);
    }

    DWORD cb = 0;
    ATA_PASS_THROUGH_EX cmd = { sizeof(ATA_PASS_THROUGH_EX), 0 };
    //cmd.AtaFlags = ATA_FLAGS_DRDY_REQUIRED; /*  Require drive to be ready  */
    cmd.TimeOutValue = 3;                   /*  Arbitrary timeout (seconds)  */
    cmd.CurrentTaskFile[6] = 0xE0;          /*  "STANDBY IMMEDIATE" in command register */
    if (DeviceIoControl(hDevice, IOCTL_ATA_PASS_THROUGH, &cmd, sizeof(cmd), &cmd, sizeof(cmd), &cb, 0) == 0) {
        DWORD error = GetLastError();
        switch (error) {
        case ERROR_INVALID_FUNCTION:
            dprintf("ata_set_standby_mode(%s): ERROR_INVALID_FUNCTION\n", name);
            break;
        case ERROR_NOT_SUPPORTED:
            dprintf("ata_set_standby_mode(%s): ERROR_NOT_SUPPORTED\n", name);
            break;
        case ERROR_ACCESS_DENIED:
            dprintf("ata_set_standby_mode(%s): ERROR_ACCESS_DENIED\n", name);
            dprintf("ata_set_standby_mode(%s): application requires admin privileges\n", name);
            break;
        }
        return false;
    }

    CloseHandle(hDevice);
    dprintf("ata_set_standby_mode(%s): SUCCESS\n", name);
    return true;
}


/* Resolve disk names specified as "/dev/disk/by-xxx" or some other symlink.
 * Please note that this function is only called during command line parsing
 * and hd-idle per se does not support dynamic disk additions or removals at
 * runtime.
 *
 * This might change in the future but would require some fiddling to avoid
 * needless overhead -- after all, this was designed to run on tiny embedded
 * devices, too.
 */
static char *disk_name(char *path)
{
    if (debug) {
        printf("using %s for %s\n", path, path);
    }
    return path;
}

/* print hex dump to stderr (e.g. sense buffers) */
static void phex(const void *p, int len, const char *fmt, ...)
{
    va_list va;
    const unsigned char *buf = (const unsigned char*)p;
    int pos = 0;
    int i;

    /* print header */
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);

    /* print hex block */
    while (len > 0) {
        fprintf(stderr, "%08x ", pos);

        /* print hex block */
        for (i = 0; i < 16; i++) {
            if (i < len) {
                fprintf(stderr, "%c%02x", ((i == 8) ? '-' : ' '), buf[i]);
            } else {
                fprintf(stderr, "   ");
            }
        }

        /* print ASCII block */
        fprintf(stderr, "   ");
        for (i = 0; i < ((len > 16) ? 16 : len); i++) {
            fprintf(stderr, "%c", (buf[i] >= 32 && buf[i] < 128) ? buf[i] : '.');
        }
        fprintf(stderr, "\n");

        pos += 16;
        buf += 16;
        len -= 16;
    }
}
