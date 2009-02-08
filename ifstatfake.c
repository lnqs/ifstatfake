/**
 * Copyright (C) 2008 by Simon Schönfeld
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 **/

/**
 * This file implements a linux kernel-module to fake the statics of
 * the network-devices via /proc-filesystem
 *
 * This can be done with commands written to /proc/ifstatfake, following
 * the syntax:
 * [IFACE] [rx|tx] [FIELD] [=|+|-][AMOUNT]
 *
 * A shellscript with the name 'ifstatfake.sh' is included.
 * It utilises the procfile to provide a more comfortable interface
 * to this module and contains more information about the syntax.
 **/

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <asm/uaccess.h>
#include <linux/netdevice.h>
#include <net/sock.h>

#define CMD_BUF_SIZE 64 /* The maximum lenght of a single command */

/* enum for the opertion to be done (set the value, add or substract
 * from it) */
enum operation {
	SET,
	ADD,
	SUB
};

/* Used by parse_cmd to remember which data for the command was already set */
enum cmd_attr {
	NONE,
	DEV,
	RX_TX,
	FIELD,
	OP
};

/* rx or tx? */
enum rx_tx {
	RX,
	TX
};

/* the value to change */
enum field {
	BYTES,
	PACKETS,
	ERRS,
	DROP,
	FIFO,
	FRAME,
	COMPRESSED,
	MULTICAST,
	COLLS,
	CARRIER
};

/* Commands are parsed in this struct before being executed */
struct command {
	struct net_device* dev;
	int rx_tx;
	int field;
	int operation;
	unsigned long amount;
};

/* Errors which may occur while parsing and executing a command */
enum error {
	ERR_TOO_MUCH_ARGS,
	ERR_NOT_ENOUGH_ARGS,
	ERR_NO_SUCH_DEVICE,
	ERR_INVALID_RX_TX,
	ERR_INVALID_FIELD,
	ERR_INVALID_OP
};

/**
 * error - Prints an error-msg and returns best fitting syscall-errorcode
 * for a module-error
 *
 * @err The error as in the error-enum
 *
 * Returns the a syscall-errorcode for the modules error
 **/
static int error(int err)
{
	switch(err) {
		case ERR_TOO_MUCH_ARGS:
			printk(KERN_WARNING "ifstatfake: Too much arguments given\n");
			return -E2BIG;
		case ERR_NOT_ENOUGH_ARGS:
			printk(KERN_WARNING "ifstatfake: Not enough arguments given\n");
			return -EINVAL;
		case ERR_NO_SUCH_DEVICE:
			printk(KERN_WARNING "ifstatfake: No such device\n");
			return -EINVAL;
		case ERR_INVALID_RX_TX:
			printk(KERN_WARNING "ifstatfake: Invalid rx/tx-flag\n");
			return -EINVAL;
		case ERR_INVALID_FIELD:
			printk(KERN_WARNING "ifstatfake: Invalid field-argument\n");
			return -EINVAL;
		case ERR_INVALID_OP:
			printk(KERN_WARNING "ifstatfake: Invalid operation-argument\n");
			return -EINVAL;
	}

	return 0;
}

/**
 * exec_cmd - executes a command
 *
 * @cmd command-struct describing the command to be executed
 **/
static void exec_cmd(struct command* cmd)
{
	/* First we retrieve a pointer to the var to change */
	struct net_device_stats* stats = cmd->dev->get_stats(cmd->dev);
	unsigned long* pval = 0;

	switch(cmd->field) {
		case BYTES:
			if(cmd->rx_tx == RX)
				pval = &stats->rx_bytes;
			else
				pval = &stats->tx_bytes;
			break;
		case PACKETS:
			if(cmd->rx_tx == RX)
				pval = &stats->rx_packets;
			else
				pval = &stats->tx_packets;
			break;
		case ERRS:
			if(cmd->rx_tx == RX)
				pval = &stats->rx_errors;
			else
				pval = &stats->tx_errors;
			break;
		case DROP:
			if(cmd->rx_tx == RX)
				pval = &stats->rx_dropped;
			else
				pval = &stats->tx_dropped;
			break;
		case FIFO:
			if(cmd->rx_tx == RX)
				pval = &stats->rx_fifo_errors;
			else
				pval = &stats->tx_fifo_errors;
			break;
		case FRAME:
			pval = &stats->rx_frame_errors;
			break;
		case COMPRESSED:
			if(cmd->rx_tx == RX)
				pval = &stats->rx_compressed;
			else
				pval = &stats->tx_compressed;
			break;
		case MULTICAST:
			pval = &stats->multicast;
			break;
		case COLLS:
			pval = &stats->collisions;
			break;
		case CARRIER:
			pval = &stats->tx_carrier_errors;
			break;
	}

	/* And change it's value based on the "operation" */
	switch(cmd->operation) {
		case SET:
			*pval = cmd->amount;
			break;
		case ADD:
			*pval += cmd->amount;
			break;
		case SUB:
			*pval -= cmd->amount;
			break;
	}
}

/**
 * set_attr - sets a field in a command-struct from a string
 *
 * @cmd Pointer to the command to write to
 * @str String with data to set
 * @attr Attribute to set as in cmd_attr-enum
 *
 * Returns 0 on success and < 0 if a problem occurs
 **/
static int set_attr(struct command* cmd, const char* str, int attr)
{
	switch(attr) {
		case NONE:
			break;
		case DEV:
			cmd->dev = dev_get_by_name(str);
			if(!cmd->dev)
				return error(ERR_NO_SUCH_DEVICE);

			break;
		case RX_TX:
			if(!strcmp(str, "rx"))
				cmd->rx_tx = RX;
			else if(!strcmp(str, "tx"))
				cmd->rx_tx = TX;
			else 
				return error(ERR_INVALID_RX_TX);

			break;
		case FIELD:
			if(!strcmp("bytes", str))
				cmd->field = BYTES;
			else if(!strcmp("packets", str))
				cmd->field = PACKETS;
			else if(!strcmp("errs", str))
				cmd->field = ERRS;
			else if(!strcmp("drop", str))
				cmd->field = DROP;
			else if(!strcmp("fifo", str))
				cmd->field = FIFO;
			else if(!strcmp("frame", str) && cmd->rx_tx == RX)
				cmd->field = FRAME;
			else if(!strcmp("compressed", str))
				cmd->field = COMPRESSED;
			else if(!strcmp("multicast", str) && cmd->rx_tx == RX)
				cmd->field = MULTICAST;
			else if(!strcmp("colls", str) && cmd->rx_tx == TX)
				cmd->field = COLLS;
			else if(!strcmp("carrier", str) && cmd->rx_tx == TX)
				cmd->field = CARRIER;
			else
				return error(ERR_INVALID_FIELD);
			break;
		case OP:
			if(str[0] == '=')
				cmd->operation = SET;
			else if(str[0] == '+')
				cmd->operation = ADD;
			else if(str[0] == '-')
				cmd->operation = SUB;
			else
				return error(ERR_INVALID_OP);

			cmd->amount = simple_strtoul(str + 1, 0, 10);
			break;
	}

	return 0;
}

/**
 * parse_cmd - parses a command written to /proc/ifstatfake
 *
 * @str The commandline to parse
 * @cmd command-struct to write to
 *
 * Returns a value < 0 if something went wrong. In this case, a errormsg
 * is written as warning to syslog and written to /proc/ifstatfake
 **/
static int parse_cmd(const char* str, struct command* cmd)
{
	int ret;
	int is = 0;
	int ib = 0;
	char buf[CMD_BUF_SIZE];
	int attr = NONE;
	char c;

	/* We iterate through the characters - there may be faster ways,
	 * but the manpage of strsep says "don't use it" and split_argv
	 * isn't supported by all kernels, so we do it this way */
	while((c = str[is]) != '\0') {
		is++;

		if(isspace(c)) {
			/* It's a whitespace, start a new argument */
			buf[ib] = '\0';

			if(!strlen(buf)) /* omit empty */
				continue;

			ib = 0;
			attr++;

			ret = set_attr(cmd, buf, attr);
			if(ret)
				return ret;
		} else {
			/* We append it */
			buf[ib] = c;
			ib++;
		}

		if(attr > OP) /* Check if there aren't too much args */
			return error(ERR_TOO_MUCH_ARGS);
	}

	if(attr != OP) /* Everything set? */
		return error(ERR_NOT_ENOUGH_ARGS);

	return 0;
}

/**
 * ifstatfake_proc_write - called when something is written to the
 * ifstatfake procfile to handle the command.
 **/
static int ifstatfake_proc_write(struct file* file, const char* buffer, unsigned long count, void* data)
{
	/* The buffer is in userspace, we have to copy it to kernelspace.
	 *
	 * If the string is longer than 64 characters, we silently ignore
	 * everything > 64, since a valid command won't be that long and
	 * it would be much additional code to handle multiple calls of
	 * this function to exec a single action
	 */
	int ret;
	char kbuf[CMD_BUF_SIZE];
	int len = count;
	struct command cmd = { 0, };

	if(len > ARRAY_SIZE(kbuf) - 1)
		len = ARRAY_SIZE(kbuf) - 1;
	if(copy_from_user(kbuf, buffer, len))
		return -EFAULT;
	kbuf[len] = '\0';

	/* In parse_cmd below we access the list of netdev-drivers,
	so we lock the network-subsystem */
	rtnl_lock();

	ret = parse_cmd(kbuf, &cmd);
	if(ret)
	{
		rtnl_unlock();
		return ret;
	}
	
	exec_cmd(&cmd);

	rtnl_unlock();

	return count;
}

/**
 * ifstatfake_init - the init-funtion of the ifstatfake-module
 *
 * Returns -ENOMEM if the procfile couldn't be created, 0 otherwise
 **/
static int __init ifstatfake_init(void)
{
	struct proc_dir_entry* proc_entry;

	printk(KERN_INFO "ifstatfake loading\n");

	/* Create the proc-file */
	proc_entry = create_proc_entry("net/ifstatfake", 0440, 0);
	if(!proc_entry) {
		printk(KERN_ERR "failed to create ifstatfake-procfile");
		return -ENOMEM;
	}

	proc_entry->write_proc = ifstatfake_proc_write;
	proc_entry->owner = THIS_MODULE;

	return 0;
}

/**
 * ifstatfake_exit - frees ressources allocated by ifstatfake
 **/
static void __exit ifstatfake_exit(void)
{
	printk(KERN_INFO "ifstatfake unloading\n");
	remove_proc_entry("net/ifstatfake", 0);
	
	return;
}

module_init(ifstatfake_init);
module_exit(ifstatfake_exit);

MODULE_AUTHOR("Simon Schönfeld <simon.schoenfeld@web.de>");
MODULE_DESCRIPTION("Allows faking of the network-device-statics");
MODULE_LICENSE("GPL");

