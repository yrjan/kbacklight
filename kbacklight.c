/* kbacklight - controlling display backlight through sysfs
 * Copyright (C) 2017 Yrjan Skrimstad
 * License: GPLv2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <libudev.h>

/* TODO: this could preferable pick a device more specifically instead of
 * randomly picking the "highest ranked" device. Note that it picks following
 * the order given here:
 * https://www.kernel.org/doc/Documentation/ABI/stable/sysfs-class-backlight*/
struct udev_device *get_backlight_device(struct udev *udev)
{
	struct udev_enumerate *enumerate;
	struct udev_list_entry *cur_entry = NULL, *first_entry = NULL;
	struct udev_device *cur_device = NULL, *pref_device = NULL;
	const char *pref_type = "";

	if (!(enumerate = udev_enumerate_new(udev))) {
		return NULL;
	}

	if (udev_enumerate_add_match_subsystem(enumerate, "backlight") < 0) {
		goto get_backlight_device_exit;
	}

	if (udev_enumerate_scan_devices(enumerate) < 0) {
		goto get_backlight_device_exit;
	}

	first_entry = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(cur_entry, first_entry) {
		const char *path;
		const char *type;

		path = udev_list_entry_get_name(cur_entry);
		cur_device = udev_device_new_from_syspath(udev, path);
		type = udev_device_get_sysattr_value(cur_device, "type");

		if (!strcmp(type, "firmware") ||
		    (!strcmp(type, "platform") && strcmp(pref_type, "firmware")) ||
		    (strcmp(pref_type, "firmware") && strcmp(pref_type, "platform"))) {
			udev_device_unref(pref_device);
			pref_device = cur_device;
			pref_type = type;
		}

		if (cur_device && cur_device != pref_device) {
			udev_device_unref(cur_device);
		}
	}

get_backlight_device_exit:
	udev_enumerate_unref(enumerate);
	return pref_device;
}


/* Note: this is specific to this subsystem and will return -1 on failure as
 * that's NOT an expected brightness. Don't use this for too random things. */
int get_sysattr_int(struct udev_device *device, const char *sysattr)
{
	const char *str;
	str = udev_device_get_sysattr_value(device, sysattr);
	if (!str) {
		return -1;
	}
	return strtol(str, NULL, 10);
}


bool set_sysattr_int(struct udev_device *device, const char *sysattr, int value)
{
	char str[4];
	sprintf(str, "%d", value);
	if (udev_device_set_sysattr_value(device, sysattr, str) < 0) {
		fprintf(stderr, "Failed to set %s.\n", sysattr);
		return false;
	}
	return true;
}


void print_usage(char *cmd)
{
	printf("Usage: %s [[+-][PERCENT]]\n"
	       "Add to, subtract from or set backlight level in PERCENT.\n"
	       "If no options given, the program reports current backlight level"
	       " in percent.\n", cmd);
}


int main(int argc, char *argv[])
{
	struct udev *udev;
	struct udev_device *device;
	bool get = false;
	bool add = false;
	bool set = false;
	bool subtract = false;
	int value, brightness, max_brightness, percent;

	if (argc == 1) {
		get = true;
	} else {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			print_usage(argv[0]);
			exit(EXIT_SUCCESS);
		} else if (argv[1][0] == '+') {
			add = true;
			value = strtol(&argv[1][1], NULL, 10);
		} else if (argv[1][0] == '-') {
			subtract = true;
			value = strtol(&argv[1][1], NULL, 10);
		} else if (isdigit(argv[1][0])) {
			set = true;
			value = strtol(argv[1], NULL, 10);
		} else {
			fprintf(stderr, "Unknown argument: %s\n", argv[1]);
			exit(EXIT_FAILURE);
		}
	}

	udev = udev_new();
	if (!udev) {
		fprintf(stderr, "udev object creation failed.\n");
		exit(EXIT_FAILURE);
	}

	device = get_backlight_device(udev);
	if (!device) {
		fprintf(stderr, "Failed to find backlight device.\n");
		exit(EXIT_FAILURE);
	}

	brightness = get_sysattr_int(device, "brightness");
	max_brightness = get_sysattr_int(device, "max_brightness");

	if (get) {
		percent = 100 * brightness / max_brightness;
		printf("%d%%\n", percent);
	} else if (add) {
		int new_value = brightness + (max_brightness * value / 100);
		if (new_value > max_brightness) {
			new_value = max_brightness;
		}
		if (!set_sysattr_int(device, "brightness", new_value)) {
			exit(EXIT_FAILURE);
		}
	} else if (subtract) {
		int new_value = brightness - (max_brightness * value / 100);
		if (new_value < 0) {
			new_value = 0;
		}
		if (!set_sysattr_int(device, "brightness", new_value)) {
			exit(EXIT_FAILURE);
		}
	} else if (set) {
		int new_value = max_brightness * value / 100;
		if (new_value < 0) {
			new_value = 0;
		} else if (new_value > max_brightness) {
			new_value = max_brightness;
		}
		if (!set_sysattr_int(device, "brightness", new_value)) {
			exit(EXIT_FAILURE);
		}
	}

	udev_device_unref(device);
	udev_unref(udev);
}
