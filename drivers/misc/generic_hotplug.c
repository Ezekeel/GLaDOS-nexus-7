/* drivers/misc/generic_hotplug.c
 *
 * Copyright 2013  Ezekeel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/workqueue.h>
#include <linux/earlysuspend.h>
#include <linux/cpu.h>

#define GENERIC_HOTPLUG_VERSION 1

#define STARTUP_DELAY 60000

#define DEFAULT_THRESHOLD_SINGLE_CORE 40
#define DEFAULT_THRESHOLD_MULTI_CORE  60

#define DEFAULT_POLL_INTERVAL_SINGLE_CORE  100 
#define DEFAULT_POLL_INTERVAL_MULTI_CORE  1000

#define DEFAULT_ALLOWED_MISSES 1

#define MODE_SINGLE_CORE 0
#define MODE_MULTI_CORE  1

static int threshold_single_core = DEFAULT_THRESHOLD_SINGLE_CORE, threshold_multi_core = DEFAULT_THRESHOLD_MULTI_CORE,
    poll_interval_single_core = DEFAULT_POLL_INTERVAL_SINGLE_CORE, poll_interval_multi_core = DEFAULT_POLL_INTERVAL_MULTI_CORE,
    current_mode, allowed_misses = DEFAULT_ALLOWED_MISSES, num_misses = 0;

static struct delayed_work generic_hotplug_work;

extern unsigned int report_load_at_max_freq(void);

static ssize_t generic_hotplug_threshold_single_core_read(struct device * dev, struct device_attribute * attr, char * buf) {
    return sprintf(buf, "%d\n", threshold_single_core);
}

static ssize_t generic_hotplug_threshold_single_core_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size) {
    int data;

    if (sscanf(buf, "%d\n", &data) == 1 && data >= 0 && data <= 100) {
	if (data != threshold_single_core)
	    threshold_single_core = min(data, threshold_multi_core);
    } else {
	pr_info("GENERIC_HOTPLUG invalid input\n"); 
    }
	    
    return size;
}

static ssize_t generic_hotplug_threshold_multi_core_read(struct device * dev, struct device_attribute * attr, char * buf) {
    return sprintf(buf, "%d\n", threshold_multi_core);
}

static ssize_t generic_hotplug_threshold_multi_core_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size) {
    int data;

    if (sscanf(buf, "%d\n", &data) == 1 && data >= 0 && data <= 100) {
	if (data != threshold_multi_core)
	    threshold_multi_core = max(data, threshold_single_core);
    } else {
	pr_info("GENERIC_HOTPLUG invalid input\n"); 
    }
	    
    return size;
}

static ssize_t generic_hotplug_poll_interval_single_core_read(struct device * dev, struct device_attribute * attr, char * buf) {
    return sprintf(buf, "%d\n", poll_interval_single_core);
}

static ssize_t generic_hotplug_poll_interval_single_core_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size) {
    int data;

    if (sscanf(buf, "%d\n", &data) == 1 && data >= 0) {
	if (data != poll_interval_single_core)
	    poll_interval_single_core = data;
    } else {
	pr_info("GENERIC_HOTPLUG invalid input\n"); 
    }
	    
    return size;
}

static ssize_t generic_hotplug_poll_interval_multi_core_read(struct device * dev, struct device_attribute * attr, char * buf) {
    return sprintf(buf, "%d\n", poll_interval_multi_core);
}

static ssize_t generic_hotplug_poll_interval_multi_core_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size) {
    int data;

    if (sscanf(buf, "%d\n", &data) == 1 && data >= 0) {
	if (data != poll_interval_multi_core)
	    poll_interval_multi_core = data;
    } else {
	pr_info("GENERIC_HOTPLUG invalid input\n"); 
    }
	    
    return size;
}

static ssize_t generic_hotplug_allowed_misses_read(struct device * dev, struct device_attribute * attr, char * buf) {
    return sprintf(buf, "%d\n", allowed_misses);
}

static ssize_t generic_hotplug_allowed_misses_write(struct device * dev, struct device_attribute * attr, const char * buf, size_t size) {
    int data;

    if (sscanf(buf, "%d\n", &data) == 1 && data >= 0) {
	if (data != allowed_misses)
	    allowed_misses = data;
    } else {
	pr_info("GENERIC_HOTPLUG invalid input\n"); 
    }
	    
    return size;
}

static ssize_t generic_hotplug_version(struct device * dev, struct device_attribute * attr, char * buf) {
    return sprintf(buf, "%u\n", GENERIC_HOTPLUG_VERSION);
}

static DEVICE_ATTR(threshold_single_core, S_IRUGO | S_IWUGO, generic_hotplug_threshold_single_core_read, generic_hotplug_threshold_single_core_write);
static DEVICE_ATTR(threshold_multi_core, S_IRUGO | S_IWUGO, generic_hotplug_threshold_multi_core_read, generic_hotplug_threshold_multi_core_write);
static DEVICE_ATTR(poll_interval_single_core, S_IRUGO | S_IWUGO, generic_hotplug_poll_interval_single_core_read, generic_hotplug_poll_interval_single_core_write);
static DEVICE_ATTR(poll_interval_multi_core, S_IRUGO | S_IWUGO, generic_hotplug_poll_interval_multi_core_read, generic_hotplug_poll_interval_multi_core_write);
static DEVICE_ATTR(allowed_misses, S_IRUGO | S_IWUGO, generic_hotplug_allowed_misses_read, generic_hotplug_allowed_misses_write);
static DEVICE_ATTR(version, S_IRUGO , generic_hotplug_version, NULL);

static struct attribute *generic_hotplug_attributes[] = 
    {
	&dev_attr_threshold_single_core.attr,
	&dev_attr_threshold_multi_core.attr,
	&dev_attr_poll_interval_single_core.attr,
	&dev_attr_poll_interval_multi_core.attr,
	&dev_attr_allowed_misses.attr,
	&dev_attr_version.attr,
	NULL
    };

static struct attribute_group generic_hotplug_group = 
    {
	.attrs  = generic_hotplug_attributes,
    };

static struct miscdevice generic_hotplug_device = 
    {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "generic_hotplug",
    };

static void single_core_mode(void) {
    int cpu;

    for_each_online_cpu(cpu) {
	if (cpu != 0)
	    cpu_down(cpu);
    }

    current_mode = MODE_SINGLE_CORE;
    num_misses = 0;
}

static void multi_core_mode(void) {
    int cpu;

    for_each_possible_cpu(cpu) {
	if (cpu != 0 && !cpu_online(cpu))
	    cpu_up(cpu);
    }

    current_mode = MODE_MULTI_CORE;
}

static void decide_generic_hotplug(struct work_struct * work) {
    int load = report_load_at_max_freq();

    if (current_mode == MODE_SINGLE_CORE) {
	if (load > threshold_multi_core) {
	    num_misses++;

	    if (num_misses > allowed_misses)
		multi_core_mode();
	} else if (num_misses > 0) {
	    num_misses--;
	}
    } else if (current_mode == MODE_MULTI_CORE) {
	if (load < threshold_single_core) {
	    single_core_mode();
	}
    }

    if (current_mode == MODE_SINGLE_CORE)
	schedule_delayed_work_on(0, &generic_hotplug_work, msecs_to_jiffies(poll_interval_single_core));
    else if (current_mode == MODE_MULTI_CORE)
	schedule_delayed_work_on(0, &generic_hotplug_work, msecs_to_jiffies(poll_interval_multi_core));
}

static void generic_hotplug_early_suspend(struct early_suspend * handler) {
    cancel_delayed_work_sync(&generic_hotplug_work);

    single_core_mode();
}

static void generic_hotplug_late_resume(struct early_suspend * handler) {
    multi_core_mode();

    schedule_delayed_work_on(0, &generic_hotplug_work, msecs_to_jiffies(poll_interval_multi_core));
}

static struct early_suspend generic_hotplug_suspend =
{
    .suspend = generic_hotplug_early_suspend,
    .resume = generic_hotplug_late_resume,
    .level = EARLY_SUSPEND_LEVEL_DISABLE_FB,
};

void disable_auto_hotplug(void) {
    cancel_delayed_work_sync(&generic_hotplug_work);

    multi_core_mode();
}

static int __init generic_hotplug_init(void) {
    int ret;

    pr_info("%s misc_register(%s)\n", __FUNCTION__, generic_hotplug_device.name);

    ret = misc_register(&generic_hotplug_device);

    if (ret) {
	pr_err("%s misc_register(%s) fail\n", __FUNCTION__, generic_hotplug_device.name);
	return 1;
    }

    if (sysfs_create_group(&generic_hotplug_device.this_device->kobj, &generic_hotplug_group) < 0) {
	pr_err("%s sysfs_create_group fail\n", __FUNCTION__);
	pr_err("Failed to create sysfs group for device (%s)!\n", generic_hotplug_device.name);
    }

    INIT_DELAYED_WORK(&generic_hotplug_work, decide_generic_hotplug);

    multi_core_mode();

    register_early_suspend(&generic_hotplug_suspend);

    schedule_delayed_work_on(0, &generic_hotplug_work, msecs_to_jiffies(STARTUP_DELAY));

    return 0;
}

device_initcall(generic_hotplug_init);
