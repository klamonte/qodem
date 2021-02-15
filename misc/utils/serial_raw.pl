#!/usr/bin/perl -w

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

# ----------------------------------------------------------------------------
# Pipe the current console to a serial port.
#
# USAGE:  serial_raw.pl
#
# ----------------------------------------------------------------------------
# Initial state --------------------------------------------------------------
# ----------------------------------------------------------------------------

# I like strict
use strict;
use POSIX;

# Local variables ------------------------------------------------------------

# ----------------------------------------------------------------------------
# Functions ------------------------------------------------------------------
# ----------------------------------------------------------------------------

# Get one character from the remote side
#
# @args   none
# $return ch
sub get_remote_char() {
	my ($rin, $nfd) = ('', undef);
	vec($rin, fileno(PORT), 1) = 1;
	$nfd = select($rin, undef, undef, 0);
	if ($nfd > 0) {
		my $ch = '';
		sysread(PORT, $ch, 1);
		return $ch;
	}
	return undef;
};

# Get one character from the keyboard
#
# @args   none
# $return ch
sub get_keystroke() {
	# Don't block
	my ($rin, $nfd) = ('', undef);
	vec($rin, fileno(STDIN), 1) = 1;
	$nfd = select($rin, undef, undef, 0);
	if ($nfd > 0) {
		my $ch = '';
		sysread(STDIN, $ch, 1);
		return $ch;
	}
	return undef;
};

# ----------------------------------------------------------------------------
# Execution ------------------------------------------------------------------
# ----------------------------------------------------------------------------

# Tell print() to flush its output.
select((select(STDOUT), $| = 1)[0]);
select((select(STDERR), $| = 1)[0]);

my $serial_port = $ARGV[0];
my $serial_speed = $ARGV[1];

if (!defined($serial_port) || !defined($serial_speed)) {
	print STDERR "USAGE: serial_raw.pl port speed\n";
	exit 99;
}

# Taken from perlfaq5
# my ($term, $oterm, $echo, $noecho, $fd_stdin);
# $fd_stdin = fileno(STDIN);
# $term     = POSIX::Termios->new();
# $term->getattr($fd_stdin);
# $oterm    = $term->getlflag();
# $echo     = ECHO | ECHOK | ICANON;
# $noecho   = $oterm & ~$echo;
# $term->setlflag($oterm);
# $term->setcc(VTIME, 0);
# $term->setattr($fd_stdin, TCSANOW);

my $cmdline = "stty -F /dev/tty raw";
system($cmdline);
$cmdline = "stty -F /dev/tty ignbrk time 5 -onlcr -iexten -echo -echoe -echoctl -echoke";
system($cmdline);

$cmdline = "stty -F $serial_port ispeed $serial_speed ospeed $serial_speed raw";
system($cmdline);
$cmdline = "stty -F $serial_port ignbrk time 5 -onlcr -iexten -echo -echoe -echoctl -echoke";
system($cmdline);

# Open the serial port
if (!sysopen(PORT, "$serial_port", O_RDWR | O_NONBLOCK)) {
	print STDERR "Unable to open $serial_port: $!";
	return 12;
}

# Flush output to it
select((select(PORT), $| = 1)[0]);

# Pass all bytes through
while (1) {
	my $local_byte = get_keystroke();
	if (defined($local_byte)) {
		print PORT $local_byte;
	}
	my $remote_byte = get_remote_char;
	if (defined($remote_byte)) {
		print STDOUT $remote_byte;
	}
}

# ----------------------------------------------------------------------------
# Exit -----------------------------------------------------------------------
# ----------------------------------------------------------------------------

# Never happens.  You have to explicitly kill me.
exit 0;
