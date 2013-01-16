/*
 * Simple hack to control the wheel of Logitech's MX-Revolution mouse.
 *
 * Requires hiddev.
 *
 * Written November 2006 by E. Toernig's bonobo - no copyrights.
 *
 * Contact: Edgar Toernig <froese@gmx.de>
 *
 * Discovered commands:
 * (all numbers in hex, FS=free-spinning mode, CC=click-to-click mode):
 *   6 byte commands send with report ID 10:
 *   01 80 56 z1 00 00	immediate FS
 *   01 80 56 z2 00 00	immediate CC
 *   01 80 56 03 00 00	FS when wheel is moved
 *   01 80 56 04 00 00	CC when wheel is moved
 *   01 80 56 z5 xx yy	CC and switch to FS when wheel is rotated at given
 *			speed; xx = up-speed, yy = down-speed
 *			(speed in something like clicks per second, 1-50,
 *			 0 = previously set speed)
 *   01 80 56 06 00 00	?
 *   01 80 56 z7 xy 00	FS with button x, CC with button y.
 *   01 80 56 z8 0x 00	toggle FS/CC with button x; same result as 07 xx 00.
 *
 * If z=0 switch temporary, if z=8 set as default after powerup.
 *
 * Button numbers:
 *   0 previously set button
 *   1 left button	(can't be used for mode changes)
 *   2 right button	(can't be used for mode changes)
 *   3 middle (wheel) button
 *   4 rear thumb button
 *   5 front thumb button
 *   6 find button
 *   7 wheel left tilt
 *   8 wheel right tilt
 *   9 side wheel forward
 *  11 side wheel backward
 *  13 side wheel pressed
 *
 * Many thanks to Andreas Schneider <anschneider@suse.de> who found
 * the codes to query the battery level and to initiate a reconnect.
 *
 * Christophe THOMAS <oxygen77@free.fr> told me, how to make revoco
 * work with the MX-5500 combo.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#define streq(a,b)	(strcmp((a), (b)) == 0)
#define strneq(a,b,c)	(strncmp((a), (b), (c)) == 0)

#define LOGITECH	0x046d
#define MX_REVOLUTION	0xc51a	// version RR41.01_B0025
#define MX_REVOLUTION2	0xc525	// version RQR02.00_B0020
#define MX_5500		0xc71c	// keyboard/mouse combo - experimental

static int first_byte;

/*** extracted from hiddev.h ***/

typedef signed short s16;
typedef signed int s32;
typedef unsigned int u32;

struct hiddev_devinfo {
	u32 bustype;
	u32 busnum;
	u32 devnum;
	u32 ifnum;
	s16 vendor;
	s16 product;
	s16 version;
	u32 num_applications;
};

struct hiddev_report_info {
	u32 report_type;
	u32 report_id;
	u32 num_fields;
};

#define HID_REPORT_TYPE_INPUT	1
#define HID_REPORT_TYPE_OUTPUT	2
#define HID_REPORT_TYPE_FEATURE	3

struct hiddev_usage_ref {
	u32 report_type;
	u32 report_id;
	u32 field_index;
	u32 usage_index;
	u32 usage_code;
	s32 value;
};

struct hiddev_usage_ref_multi {
	struct hiddev_usage_ref uref;
	u32 num_values;
	s32 values[1024];
};

#define HIDIOCGDEVINFO		_IOR('H', 0x03, struct hiddev_devinfo)
#define HIDIOCINITREPORT	_IO('H', 0x05)
#define HIDIOCGREPORT		_IOW('H', 0x07, struct hiddev_report_info)
#define HIDIOCSREPORT		_IOW('H', 0x08, struct hiddev_report_info)
#define HIDIOCGUSAGES		_IOWR('H', 0x13, struct hiddev_usage_ref_multi)
#define HIDIOCSUSAGES		_IOW('H', 0x14, struct hiddev_usage_ref_multi)
#define HIDIOCGFLAG		_IOR('H', 0x0E, int)
#define HIDIOCSFLAG		_IOW('H', 0x0F, int)

#define HIDDEV_FLAG_UREF	0x1
#define HIDDEV_FLAG_REPORT	0x2
#define HIDDEV_FLAGS		0x3

/*** end hiddev.h ***/


static void
fatal(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    fprintf(stderr, "revoco: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static int
wait_for_input(int fd, int timeout)
{
    fd_set fds;
    struct timeval tv, *tvp;;

    tvp = 0;
    if (timeout >= 0)
    {
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = timeout % 1000 * 1000;
	tvp = &tv;
    }

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    return select(fd + 1, &fds, NULL, NULL, tvp);
}

static void
wait_report(int fd, int timeout)
{
    struct hiddev_usage_ref uref;

    if (wait_for_input(fd, timeout) > 0)
	while (read(fd, &uref, sizeof(uref)) > 0)
	    ;
}

static int
open_dev(char *path)
{
    char buf[128];
    int i, fd;
    struct hiddev_devinfo dinfo;

    for (i = 0; i < 16; ++i)
    {
	sprintf(buf, path, i);
	fd = open(buf, O_RDWR);
	if (fd >= 0)
	{
	    if (ioctl(fd, HIDIOCGDEVINFO, &dinfo) == 0)
		if (dinfo.vendor == (short)LOGITECH)
		{
		    first_byte = 1;
		    if (dinfo.product == (short)MX_REVOLUTION)
			return fd;
		    if (dinfo.product == (short)MX_REVOLUTION2)
			return fd;
		    if (dinfo.product == (short)MX_5500)
		    {
			printf("note: MX-5500 support is experimental\n");
			first_byte = 2;
			return fd;
		    }
		}
	    close(fd);
	}
    }
    return -1;
}

static void
init_dev(int fd)
{
    int flag = HIDDEV_FLAG_UREF | HIDDEV_FLAG_REPORT;

    if (fcntl(fd, F_SETFL, O_RDWR | O_NONBLOCK) == -1)
	printf("fcntl(O_NONBLOCK): %s\n", strerror(errno));
    if (ioctl(fd, HIDIOCSFLAG, &flag) == -1)
	printf("HIDIOCSFLAG: %s\n", strerror(errno));
}

static void
close_dev(int fd)
{
    close(fd);
}

static void
send_report(int fd, int id, const int *buf, int n)
{
    struct hiddev_usage_ref_multi uref;
    struct hiddev_report_info rinfo;
    int i;

    uref.uref.report_type = HID_REPORT_TYPE_OUTPUT;
    uref.uref.report_id = id;
    uref.uref.field_index = 0;
    uref.uref.usage_index = 0;
    uref.num_values = n;
    for (i = 0; i < n; ++i)
	uref.values[i] = buf[i];
    if (ioctl(fd, HIDIOCSUSAGES, &uref) == -1)
	fatal("send report %02x/%d, HIDIOCSUSAGES: %s", id, n, strerror(errno));

    rinfo.report_type = HID_REPORT_TYPE_OUTPUT;
    rinfo.report_id = id;
    rinfo.num_fields = 1;
    if (ioctl(fd, HIDIOCSREPORT, &rinfo) == -1)
	fatal("send report %02x/%d, HIDIOCSREPORT: %s", id, n, strerror(errno));

    wait_report(fd, 3000);
}

static void
query_report(int fd, int id, int *buf, int n)
{
    struct hiddev_usage_ref_multi uref;
    struct hiddev_report_info rinfo;
    int i;

    rinfo.report_type = HID_REPORT_TYPE_INPUT;
    rinfo.report_id = id;
    rinfo.num_fields = 1;
    if (ioctl(fd, HIDIOCGREPORT, &rinfo) == -1)
	fatal("query report %02x/%d, HIDIOCGREPORT: %s", id, n, strerror(errno));

    wait_report(fd, 3000);

    uref.uref.report_type = HID_REPORT_TYPE_INPUT;
    uref.uref.report_id = id;
    uref.uref.field_index = 0;
    uref.uref.usage_index = 0;
    uref.num_values = n;
    if (ioctl(fd, HIDIOCGUSAGES, &uref) == -1)
	fatal("query report %02x/%d, HIDIOCGUSAGES: %s", id, n, strerror(errno));
    for (i = 0; i < n; ++i)
	buf[i] = uref.values[i];
}

static void
mx_cmd(int fd, int b1, int b2, int b3)
{
    int buf[6] = { first_byte, 0x80, 0x56, b1, b2, b3 };

    send_report(fd, 0x10, buf, 6);
}

static int
mx_query(int fd, int b1, int *res)
{
    int buf[6] = { first_byte, 0x81, b1, 0, 0, 0 };

    send_report(fd, 0x10, buf, 6);
    res[0] = -1;
    query_report(fd, 0x10, res, 6);

    if (res[0] != 0x01 || res[1] != 0x81 || res[2] != b1)
    {
	printf("bad answer (%02x %02x %02x...)\n", res[0], res[1], res[2]);
	return 0;
    }
    return 1;
}

static char *
onearg(char *str, char prefix, int *arg, int def, int min, int max)
{
    char *end;
    long n;

    *arg = def;

    if (*str == '\0')
	return str;

    if (*str != prefix)
	fatal("bad argument `%s': `%c' expected", str, prefix);

    n = strtol(++str, &end, 0);
    if (str != end)
    {
	*arg = n;
	if (n < min || n > max)
	    fatal("argument `%.*s' out of range (%d-%d)", end - str, str,
								    min, max);
    }
    return end;
}

static void
twoargs(char *str, int *arg1, int *arg2, int def, int min, int max)
{
    char *p = str;

    p = onearg(p, '=', arg1, def, min, max);
    p = onearg(p, ',', arg2, *arg1, min, max);
    if (*p)
	fatal("malformed argument `%s'", str);
}

static int
nargs(char *str, int *buf, int n, int def, int min, int max)
{
    char *p = str;
    int i = 0, del = '=';

    while (n--)
    {
	if (*p)
	    i++;
	p = onearg(p, del, buf++, def, min, max);
	del = ',';
    }
    if (*p)
	fatal("malformed argument `%s'", str);
    return i;
}

static void
configure(int handle, int argc, char **argv)
{
    int i, arg1, arg2;

    for (i = 1; i < argc; ++i) {
	int perm = 0x80;
	char *cmd = argv[i];

	if (strneq(cmd, "temp-", 5))
	    perm = 0, cmd += 5;

	if (streq(cmd, "free"))
	{
	    mx_cmd(handle, perm + 1, 0, 0);
	}
	else if (streq(cmd, "click"))
	{
	    mx_cmd(handle, perm + 2, 0, 0);
	}
	else if (strneq(cmd, "manual", 6))
	{
	    twoargs(cmd + 6, &arg1, &arg2, 0, 0, 15);
	    if (arg1 != arg2)
		mx_cmd(handle, perm + 7, arg1 * 16 + arg2, 0);
	    else
		mx_cmd(handle, perm + 8, arg1, 0);
	}
	else if (strneq(cmd, "auto", 4))
	{
	    twoargs(cmd + 4, &arg1, &arg2, 0, 0, 50);
	    mx_cmd(handle, perm + 5, arg1, arg2);
	}
	else if (strneq(argv[i], "soft-free", 9))
	{
	    twoargs(argv[i] + 9, &arg1, &arg2, 0, 0, 255);
	    mx_cmd(handle, 3, arg1, arg2);
	}
	else if (strneq(argv[i], "soft-click", 10))
	{
	    twoargs(argv[i] + 10, &arg1, &arg2, 0, 0, 255);
	    mx_cmd(handle, 4, arg1, arg2);
	}
	else if (strneq(argv[i], "reconnect", 9))
	{
	    static const int cmd[] = { 0xff, 0x80, 0xb2, 1, 0, 0 };

	    twoargs(argv[i] + 9, &arg1, &arg2, 0, 0, 255);
	    send_report(handle, 0x10, cmd, 6);
	    printf("Reconnection initiated\n");
	    printf(" - Turn off the mouse\n");
	    printf(" - Press and hold the left mouse button\n");
	    printf(" - Turn on the mouse\n");
	    printf(" - Press the right button 5 times\n");
	    printf(" - Release the left mouse button\n");
	    wait_report(handle, 60000);
	}
	else if (strneq(argv[i], "mode", 4))
	{
	    int buf[6];

	    if (mx_query(handle, 0x08, buf))
	    {
		if (buf[5] & 1)
		    printf("click-by-click\n");
		else
		    printf("free spinning\n");
	    }
	}
	else if (strneq(argv[i], "battery", 7))
	{
	    int buf[6];

	    if (mx_query(handle, 0x0d, buf))
	    {
		char str[32], *st;

		switch (buf[5])
		{
		    case 0x30:	st = "running on battery";	break;
		    case 0x50:	st = "charging";		break;
		    case 0x90:	st = "fully charged";		break;
		    default:	sprintf(st = str, "status %02x", buf[5]);
		}
		printf("battery level %d%%, %s\n", buf[3], st);
	    }
	}
    /*** debug commands ***/
	else if (strneq(argv[i], "raw", 3))
	{
	    int buf[256], n;

	    n = nargs(argv[i] + 3, buf, 256, 0, 0, 255);
	    send_report(handle, buf[0], buf+1, n-1);
	}
	else if (strneq(argv[i], "query", 5))
	{
	    int buf[256], j;

	    twoargs(argv[i] + 5, &arg1, &arg2, -1, 0, 255);
	    if (arg1 == -1)
		arg1 = 0x10, arg2 = 6;
	    query_report(handle, arg1, buf, arg2);

	    printf("report %02x:", arg1);
	    for (j = 0; j < arg2; ++j)
		printf(" %02x", buf[j]);
	    printf("\n");
	}
	else if (strneq(argv[i], "dump", 4))
	{
	    twoargs(cmd + 4, &arg1, &arg2, 3, -1, 24*60*60);
	    if (arg1 > 0)
		arg1 *= 1000;
	    while (wait_for_input(handle, arg1) > 0)
	    {
		struct hiddev_usage_ref uref;

		if (read(handle, &uref, sizeof(uref)) == sizeof(uref))
		    printf("read: type=%u, id=%u, field=%08x, usage=%08x,"
				 " code=%08x, value=%u\n",
			uref.report_type, uref.report_id, uref.field_index,
			uref.usage_index, uref.usage_code, uref.value);
	    }
	}
	else if (strneq(argv[i], "sleep", 5))
	{
	    twoargs(argv[i] + 5, &arg1, &arg2, 1, 0, 255);
	    sleep(arg1);
	}
	else
	    fatal("unknown option `%s'", argv[i]);
    }
}

static void
usage(void)
{
    printf("Revoco v"VERSION" - Change the wheel behaviour of "
				    "Logitech's MX-Revolution mouse.\n\n");
    printf("Usage:\n");
    printf("  revoco free                      free spinning mode\n");
    printf("  revoco click                     click-to-click mode\n");
    printf("  revoco manual[=button[,button]]  manual mode change via button\n");
    printf("  revoco auto[=speed[,speed]]      automatic mode change (up, down)\n");
    printf("  revoco battery                   query battery status\n");
    printf("  revoco mode                      query scroll wheel mode\n");
    printf("  revoco reconnect                 initiate reconnection\n");
    printf("\n");
    printf("Prefixing the mode with 'temp-' (i.e. temp-free) switches the mode\n");
    printf("temporarily, otherwise it becomes the default mode after power up.\n");
    printf("\n");
    printf("Button numbers:\n");
    printf("  0 previously set button   7 wheel left tilt\n");
    printf("  3 middle (wheel button)   8 wheel right tilt\n");
    printf("  4 rear thumb button       9 thumb wheel forward\n");
    printf("  5 front thumb button     11 thumb wheel backward\n");
    printf("  6 find button            13 thumb wheel pressed\n");
    printf("\n");
    exit(0);
}

static void
trouble_shooting(void)
{
    char *path;
    int fd;

    fd = open(path = "/dev/hiddev0", O_RDWR);
    if (fd == -1 && errno == ENOENT)
	fd = open(path = "/dev/usb/hiddev0", O_RDWR);

    if (fd != -1)
	fatal("No Logitech MX-Revolution (%04x:%04x or %04x:%04x) found.",
						    LOGITECH, MX_REVOLUTION,
						    LOGITECH, MX_REVOLUTION2);

    if (errno == EPERM || errno == EACCES)
	fatal("No permission to access hiddev (%s-15)\n"
	      "Try 'sudo revoco ...'", path);

    fatal("Hiddev kernel driver not found.  Check with 'dmesg | grep hiddev'\n"
  "whether it is present in the kernel.  If it is, make sure that the device\n"
  "nodes (either /dev/usb/hiddev0-15 or /dev/hiddev0-15) are present.  You\n"
  "can create them with\n"
  "\n"
  "\tmkdir /dev/usb\n"
  "\tmknod /dev/usb/hiddev0 c 180 96\n"
  "\tmknod /dev/usb/hiddev1 c 180 97\n\t...\n"
  "\n"
  "or better by adding a rule to the udev database in\n"
  "/etc/udev/rules.d/10-local.rules\n"
  "\n"
  "\tBUS=\"usb\", KERNEL=\"hiddev[0-9]*\", NAME=\"usb/%k\", MODE=\"660\"\n");
}

int
main(int argc, char **argv)
{
    int handle;

    if (argc < 2)
	usage();
    if (argc > 1 && (streq(argv[1], "-h") || streq(argv[1], "--help")))
	usage();

    handle = open_dev("/dev/usb/hiddev%d");
    if (handle == -1)
	handle = open_dev("/dev/hiddev%d");
    if (handle == -1)
	trouble_shooting();

    init_dev(handle);

    configure(handle, argc, argv);

    close_dev(handle);
    exit(0);
}
