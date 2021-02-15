#!/usr/bin/perl -w
#
# qodem - Qodem Terminal Emulator
#
# Written 2003-2019 by Autumn Lamonte
#
# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to the
# public domain worldwide. This software is distributed without any
# warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication
# along with this software. If not, see
# <http://creativecommons.org/publicdomain/zero/1.0/>.


# Script to convert a SyncTERM phonebook file to a Qodem phonebook file.
#
# This only converts:
#     address
#     port
#     connection method: telnet, ssh, rlogin, and command line only
#     username
#     password
#
# Tested against SyncTERM 1.0.

use strict;

my $line;
my $in_filename;
my $out_filename;

if ($#ARGV < 1) {
    print STDERR "Usage: convert_syncterm.pl { input filename } { output filename }\n";
    exit -1;
}

$in_filename = $ARGV[0];
$out_filename = $ARGV[1];

print "Read from: $in_filename\n";
print "Write to: $out_filename\n";

open(my $in_file, "<", $in_filename);
open(my $out_file, ">>", $out_filename);
print $out_file "# Qodem Phonebook Generated From SyncTERM file $in_filename\n";

# Initial state.
my $name = "";
my $address = "";
my $port = "";
my $method = "";
my $username ="";
my $password = "";
my $emulation = "ANSI";
my $codepage = "CP437";
my $times_on = "0";
my $first = 1;

# Trim left and right whitespace.
sub trim {
	my $s = shift;
	$s =~ s/^\s+|\s+$//g;
	return $s
};

# Write one full phonebook entry out.
sub write_entry {
	print $out_file "[entry]\n";
	print $out_file "name=$name\n";
	print $out_file "address=$address\n";
	print $out_file "port=$port\n";
	print $out_file "method=$method\n";
	print $out_file "emulation=$emulation\n";
	print $out_file "codepage=$codepage\n";
	print $out_file "username=$username\n";
	print $out_file "password=$password\n";
	print $out_file "times_on=${times_on}\n";
	print $out_file "last_call=0\n";
	print $out_file "keybindings_filename=\n";
	print $out_file "\n";
};

while ($line = <$in_file>) {
	chomp($line);
	$line = trim($line);

	if ($line =~ /^\[(.*)\]$/) {
		if ($first == 1) {
			$first = 0;
		}
		else {
			write_entry();
		}
		# Capture name, and reset the rest of the fields.
		$name = $1;
		$address = "";
		$port = "";
		$method = "";
		$username ="";
		$password = "";
		$codepage = "CP437";
		$times_on = "0";
		next;
	}

	if ($line =~ /^(.*)=(.*)$/) {
		my $left = trim($1);
		my $right = trim($2);
		if ($left eq "Address") {
			$address = $right;
		} elsif ($left eq "Port") {
			$port = $right;
		} elsif ($left eq "UserName") {
			$username = $right;
		} elsif ($left eq "Password") {
			$password = $right;
		} elsif ($left eq "TotalCalls") {
			$times_on = $right;
		} elsif ($left eq "ConnectionType") {
			if ($right eq "Shell") {
				$method = "CMDLINE";
			} elsif ($right eq "Telnet") {
				$method = "TELNET";
			} elsif ($right eq "RLogin") {
				$method = "RLOGIN";
			} elsif ($right eq "SSH") {
				$method = "SSH";
			}
		}
	}
}
# Reading is finished.
close($in_file);

# Emit the last entry.
if ($first == 0) {
	write_entry();
}

close($out_file);
