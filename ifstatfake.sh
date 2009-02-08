#!/bin/bash

##
# Copyright (C) 2008 by Simon Schönfeld
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
##

SELF="`basename $0`"

# Prints the usage
function print_usage()
{
	echo "'$SELF', a tool for faking the kernel network-driver-statistics" >> /dev/stderr
	echo "" >> /dev/stderr
	echo "Usage: $SELF [--help|--version] IFACE rx|tx FIELD AMOUNT" >> /dev/stderr
	echo "" >> /dev/stderr
	echo "Options:" >> /dev/stderr
	echo "  -h, --help, help        Show this help" >> /dev/stderr
	echo "  -v, --version, version  Prints version info" >> /dev/stderr
	echo "  IFACE                   The name of the interface to change the statistics," >> /dev/stderr
	echo "                          i.e. eth0" >> /dev/stderr

	echo "  rx, tx                  Sets whether the statistics in context of received"  >> /dev/stderr
	echo "                          (rx) or sent (tx) data should be changed" >> /dev/stderr
	echo "  FIELD                   Which field should be changed. Possible values are:" >> /dev/stderr
	echo "                            - bytes" >> /dev/stderr
	echo "                            - packets" >> /dev/stderr
	echo "                            - errs" >> /dev/stderr
	echo "                            - drop" >> /dev/stderr
	echo "                            - fifo" >> /dev/stderr
	echo "                            - frame (only with 'rx' set)" >> /dev/stderr
	echo "                            - compressed" >> /dev/stderr
	echo "                            - multicast (only with 'rx' set)" >> /dev/stderr
	echo "                            - colls (only with 'tx' set)" >> /dev/stderr
	echo "                            - carrier (only with 'tx' set)" >> /dev/stderr
	echo "  AMOUNT                  A number the FIELD should be set to or a number to be" >> /dev/stderr
	echo "                          added or substracted from the current value with a" >> /dev/stderr
	echo "                          trailing '=', '+' or '-' to specify if the value" >> /dev/stderr
	echo "                          should be set, added or substracted" >> /dev/stderr
	echo "" >> /dev/stderr
	echo "This script only provides a more comforable interface to the ifstatfake-kernel-" >> /dev/stderr
	echo "module." >> /dev/stderr
	echo "If the module is not loaded, this script will try to load it." >> /dev/stderr
	echo "First it'll look in the current directory for it and insmod it, if found." >> /dev/stderr
	echo "If not, it'll try a modprobe for it." >> /dev/stderr
	echo "If the module is loaded by this script, it'll be unloaded automatically." >> /dev/stderr
	echo "" >> /dev/stderr
	echo "Report bugs to Simon Schönfeld <simon.schoenfeld@web.de>" >> /dev/stderr
}

function print_version()
{
	echo "$SELF 0.1" >> /dev/stderr
	echo "Copyright © 2008 Simon Schönfeld" >> /dev/stderr
	echo "This is free software.  You may redistribute copies of it under the terms of" >> /dev/stderr
	echo "the GNU General Public License <http://www.gnu.org/licenses/gpl.html>." >> /dev/stderr
	echo "There is NO WARRANTY, to the extent permitted by law." >> /dev/stderr
	echo "" >> /dev/stderr
	echo "Written by Simon Schönfeld" >> /dev/stderr
}

# Returns whether a command exists or not and prints an error if not
function cmd_available()
{
	if ( type $1 &> /dev/null ); then
		return 0;
	else
		echo "$SELF: Required program '$1' not found in path" >> /dev/stderr
		return 1;
	fi
}

# "--help" requested?
if [[ $1 == "-h" || $1 == "--help" || $1 == "help" ]]; then
	print_usage
	exit 0;
fi

# "--version" requested?
if [[ $1 == "-v" || $1 == "--version" || $1 == "version" ]]; then
	print_version
	exit 0;
fi

# Are we root?
if [[ `whoami` != "root" ]]; then
	echo "$SELF: You've to be root to run this script" >> /dev/stderr
	exit 1;
fi

# We need exactly 4 arguments
if [[ $# != 4 ]]; then
	print_usage
	exit 2;
fi

# Check for required commands
if ( ! cmd_available ifconfig ) || ( ! cmd_available lsmod ) || ( ! cmd_available insmod ) \
		|| ( ! cmd_available rmmod ) || ( ! cmd_available modprobe ); then
	exit 3;
fi

# Parse and validate arguments
IFACE=$1
if ( ! ifconfig "$IFACE" &> /dev/null ); then
	echo "$SELF: Invalid interface '$IFACE'" >> /dev/stderr
	exit 4;
fi

RX_TX=$2
if [[ $RX_TX != "rx" && $RX_TX != "tx" ]]; then
	echo "$SELF: Either 'rx' or 'tx' must be set" >> /dev/stderr
	exit 5;
fi

FIELD=$3
if [[ $FIELD != "packets" && $FIELD != "errs" && $FIELD != "drop" && $FIELD != "fifo" \
		&& $FIELD != "frame" && $FIELD != "compressed" && $FIELD != "multicast" \
		&& $FIELD != "colls" && $FIELD != "carrier" ]]; then
	echo "$SELF: Invalid FIELD '$FIELD'" >> /dev/stderr
	exit 6;
fi

if [[ $RX_TX == "tx" ]] && [[ $FIELD == "frame" || $FIELD == "multicast" ]]; then
	echo "$SELF: '$FIELD' isn't allowed with 'tx' set" >> /dev/stderr
	exit 7;
fi

if [[ $RX_TX == "rx" ]] && [[ $FIELD == "colls" || $FIELD == "carrier" ]]; then
	echo "$SELF: '$FIELD' isn't allowed with 'tx' set" >> /dev/stderr
	exit 7;
fi

OP=${4:0:1}
if [[ $OP != "=" && $OP != "+" && $OP != "-" ]]; then
	echo "$SELF: '=', '+' or '-' must be set" >> /dev/stderr
	exit 8;
fi

AMOUNT=${4:1}
if [[ "x$AMOUNT" == "x" ]]; then
	echo "$SELF: A number to set/add/substract must be set" >> /dev/stderr;
	exit 9;
fi

if ( ! expr $AMOUNT + 1 &> /dev/null ); then
	echo "$SELF: AMOUNT has to be a numberic value" >> /dev/stderr
	exit 10;
fi

# Is the module loaded?
MOD_LOADED=0
if ( ! lsmod | grep ifstatfake &> /dev/null ); then
	echo "ifstatfake module not in kernel - inserting it" >> /dev/stderr
	if [[ -f ifstatfake.ko ]]; then
		insmod ifstatfake.ko;
	else
		if ( ! modprobe ifstatfake ); then
			exit 11;
		fi
	fi

	MOD_LOADED=1;
fi

echo $IFACE $RX_TX $FIELD $OP$AMOUNT >> /proc/net/ifstatfake

if [[ $MOD_LOADED == 1 ]]; then
	rmmod ifstatfake.ko;
fi

exit 0

