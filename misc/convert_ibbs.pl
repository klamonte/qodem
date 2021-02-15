#!/usr/bin/perl -w
#
# qodem - Qodem Terminal Emulator
#
# Written 2003-2021 by Autumn Lamonte ⚧ Trans Liberation Now
#
# To the extent possible under law, the author(s) have dedicated all
# copyright and related and neighboring rights to this software to the
# public domain worldwide. This software is distributed without any
# warranty.
#
# You should have received a copy of the CC0 Public Domain Dedication
# along with this software. If not, see
# <http://creativecommons.org/publicdomain/zero/1.0/>.


# Script to convert the Telnet BBS List in "short form" to a Qodem
# phonebook file.

use strict;

my $line;
my $in_filename;
my $out_filename;

if ($#ARGV < 1) {
    print STDERR "Usage: convert_ibbs.pl { input filename } { output filename }\n";
    exit -1;
}

$in_filename = $ARGV[0];
$out_filename = $ARGV[1];

print "Read from: $in_filename\n";
print "Write to: $out_filename\n";

open(my $in_file, "<", $in_filename);
open(my $out_file, ">>", $out_filename);
print $out_file "# Qodem Phonebook Generated From Telnet BBS Guide\n";
print $out_file "# http://www.telnetbbsguide.com\n";

while ($line = <$in_file>) {
    chomp($line);
    my $do_output = 0;

    my $name;
    my $address;
    my $port;
    my $method;

    if ($line =~ / \d{3}-\d{3}-\d{4}/) {
	# $line = substr($line, 0, 38);
	chomp($line);
	$do_output = 1;
	my @fields = split(' ', $line);
	$name = join(' ', @fields[0 .. $#fields-1]);
	$address = $fields[$#fields];
	$port = "";
	$method="MODEM";
    } elsif (($line =~ /^  [A-Z|a-z]/) && !($line =~ /^  Modem      BBS Name /)) {
	$do_output = 1;

	my @fields = split(' ', $line);
	$name = join(' ', @fields[0 .. $#fields - 1]);
	$address = $fields[$#fields];
	($address, $port) = split(':', $address);
	if (!defined($port)) {
	    $port = 23;
	}
	$method="TELNET";
    } elsif ($line =~ /^\* [A-Z|a-z]/) {
	$do_output = 1;
	my @fields = split(' ', $line);
	$name = join(' ', @fields[1 .. $#fields - 1]);
	$address = $fields[$#fields];
	($address, $port) = split(':', $address);
	if (!defined($port)) {
	    $port = "23";
	}
	$method="TELNET";
    }

    if ($do_output) {
	print $out_file "[entry]\n";
	print $out_file "name=$name\n";
	print $out_file "address=$address\n";
	print $out_file "port=$port\n";
	print $out_file "method=$method\n";
	print $out_file "emulation=ANSI\n";
	print $out_file "codepage=CP437\n";
	print $out_file "username=\n";
	print $out_file "password=\n";
	print $out_file "times_on=0\n";
	print $out_file "last_call=0\n";
	print $out_file "keybindings_filename=\n";
	print $out_file "\n";
    }

}
close($in_file);
close($out_file);
