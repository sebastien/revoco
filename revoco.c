/*
 * Simple hack to control the wheel of Logitech's MX-Revolution mouse.
 *
 * Requires hiddev.
 *
 * Written November 2006 by E. Toernig's bonobo - no copyrights.
 * Cleanup by Petteri Räty <betelgeuse@gentoo.org>
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
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>

// hiddev and the headers it requires
#include <asm/types.h>
#include <linux/hiddev.h>

#define streq(a,b)	(strcmp((a), (b)) == 0)
#define strneq(a,b,c)	(strncmp((a), (b), (c)) == 0)

#define LOGITECH	0x046d

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
		    if (dinfo.product == (short)MX_REVOLUTION)
			return fd;
	    close(fd);
	}
    }
    return -1;
}

static void
close_dev(int fd)
{
    close(fd);
}

static void
put_id10(int fd, int b1, int b2, int b3)
{
    struct hiddev_usage_ref_multi uref;
    struct hiddev_report_info rinfo;

    uref.uref.report_type = HID_REPORT_TYPE_OUTPUT;
    uref.uref.report_id = 0x10;
    uref.uref.field_index = 0;
    uref.uref.usage_index = 0;
    uref.num_values = 6;
    uref.values[0] = 0x01;	// ET: No idea what the first three values
    uref.values[1] = 0x80;	// mean.  The SetPoint software sends them ...
    uref.values[2] = 0x56;
    uref.values[3] = b1;
    uref.values[4] = b2;
    uref.values[5] = b3;
    if (ioctl(fd, HIDIOCSUSAGES, &uref) == -1)
	fatal("HIDIOCSUSAGES: %s", strerror(errno));

    rinfo.report_type = HID_REPORT_TYPE_OUTPUT;
    rinfo.report_id = 0x10;
    rinfo.num_fields = 1;
    if (ioctl(fd, HIDIOCSREPORT, &rinfo) == -1)
	fatal("HIDIOCSREPORT: %s", strerror(errno));
}

static void
get_id10(int fd, unsigned char *buf)
{
    struct hiddev_usage_ref_multi uref;
    struct hiddev_report_info rinfo;
    int i;

    rinfo.report_type = HID_REPORT_TYPE_INPUT;
    rinfo.report_id = 0x10;
    rinfo.num_fields = 1;
    if (ioctl(fd, HIDIOCGREPORT, &rinfo) == -1)
	fatal("HIDIOCGREPORT: %s", strerror(errno));

    uref.uref.report_type = HID_REPORT_TYPE_INPUT;
    uref.uref.report_id = 0x10;
    uref.uref.field_index = 0;
    uref.uref.usage_index = 0;
    uref.num_values = 6;
    if (ioctl(fd, HIDIOCGUSAGES, &uref) == -1)
	fatal("HIDIOCGUSAGES: %s", strerror(errno));
    for (i = 0; i < 6; ++i)
	buf[i] = uref.values[i];
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

static void
threeargs(char *str, int *arg1, int *arg2, int *arg3, int def, int min, int max)
{
    char *p = str;

    p = onearg(p, '=', arg1, def, min, max);
    p = onearg(p, ',', arg2, def, min, max);
    p = onearg(p, ',', arg3, def, min, max);
    if (*p)
	fatal("malformed argument `%s'", str);
}

static void
configure(int handle, int argc, char **argv)
{
    int i, arg1, arg2, arg3;

    for (i = 1; i < argc; ++i) {
	int perm = 0x80;
	char *cmd = argv[i];

	if (strneq(cmd, "temp-", 5))
	    perm = 0, cmd += 5;

	if (streq(cmd, "free"))
	{
	    put_id10(handle, perm + 1, 0, 0);
	}
	else if (streq(cmd, "click"))
	{
	    put_id10(handle, perm + 2, 0, 0);
	}
	else if (strneq(cmd, "manual", 6))
	{
	    twoargs(cmd + 6, &arg1, &arg2, 0, 0, 15);
	    put_id10(handle, perm + 7, arg1 * 16 + arg2, 0);
	}
	else if (strneq(cmd, "auto", 4))
	{
	    twoargs(cmd + 4, &arg1, &arg2, 0, 0, 50);
	    put_id10(handle, perm + 5, arg1, arg2);
	}
	else if (strneq(argv[i], "soft-free", 9))
	{
	    twoargs(argv[i] + 9, &arg1, &arg2, 0, 0, 255);
	    put_id10(handle, 3, arg1, arg2);
	}
	else if (strneq(argv[i], "soft-click", 10))
	{
	    twoargs(argv[i] + 10, &arg1, &arg2, 0, 0, 255);
	    put_id10(handle, 4, arg1, arg2);
	}
	else if (strneq(argv[i], "raw", 3))
	{
	    threeargs(argv[i] + 3, &arg1, &arg2, &arg3, 0, 0, 255);
	    put_id10(handle, arg1, arg2, arg3);
	}
	else if (streq(argv[i], "query"))
	{
	    unsigned char buf[6];
	    int j;

	    get_id10(handle, buf);
	    if (buf[0] == 0x01 && buf[1] == 0x80 && buf[2] == 0x56)
	    {
		printf("current mode: ");
		switch (buf[3])
		{
		    case 0x00:	printf("status good");		break;
		    case 0x01:	printf("free");			break;
		    case 0x02:	printf("click");		break;
		    case 0x03:	printf("soft-free");		break;
		    case 0x04:	printf("soft-click");		break;

		    case 0x05:	printf("auto=%d", buf[4]);
				if (buf[4] != buf[5])
				    printf(",%d", buf[5]);
				break;

		    case 0x07:	printf("manual=%d,%d", buf[4] >> 4,
							     buf[4] & 0x0f);
				break;

		    case 0x08:	printf("manual=%d", buf[4]);
				break;

		    default:	printf("unknown %02x %02x %02x",
							buf[3], buf[4], buf[5]);
				break;
		}
		printf("\n");
	    }
	    else
	    {
		printf("query reply:");
		for (j = 0; j < 6; ++j)
		    printf(" %02x", buf[j]);
		printf("\n");
	    }
	}
	else
	    fatal("unknown option `%s'", argv[i]);
    }
}

static void
usage(void)
{
    printf("Revoco v%.1f - Change the wheel behaviour of "
				    "Logitech's MX-Revolution mouse.\n\n", VERSION);
    printf("Usage:\n");
    printf("  revoco free                      free spinning mode\n");
    printf("  revoco click                     click-to-click mode\n");
    printf("  revoco manual[=button[,button]]  manual mode change via button\n");
    printf("  revoco auto[=speed[,speed]]      automatic mode change (up, down)\n");
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
	fatal("No Logitech MX-Revolution (%04x:%04x) found. %d",
						    LOGITECH, MX_REVOLUTION, errno);

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

    configure(handle, argc, argv);

    close_dev(handle);
    exit(0);
}
