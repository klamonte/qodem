The Qodem Project Work Log
==========================

February 15, 2021

Little has happened to Qodem, but a ton has happened in the real
world, and in my life.

I am transgender.  Neat!  More specifically, I am a trans woman.  And
I am transitioning, and yes it takes forEVER, and no I won't talk much
more about it other than my name is Autumn and my pronouns are
she/her.  I would rename my GitHub and GitLab usernames from klamonte
to something else, but there are enough forks out there and longevity
that I see little reason to push that inconvenience to everyone else.

But cool bonus: I get a symbol from Unicode now! ⚧ I get a flag too,
but a lot of operating systems aren't there yet.  And it isn't in my
Emacs yet either, bummer. :-( But let's see if GitHub will show it in
a browser, here goes: 🏳⚧

For whatever reason (OK, we all know the reason) the political
conservative universe has decided that in response to Biden's
commitment to follow the fucking law, trans women like me are just the
worst sort of people.  (They don't care about trans men one way or the
other apparently.)  We are simultaneously too masculine to pass as
women in the bathroom, yet also so feminine that we easily fool
straight cis men into the bedroom.  (Which for me is quite gross.
Never wanted to date men before, certainly don't want to touch them
now.)  Anyway, long story short: fuck TERFs because at this point they
are really just the same old racists on the wrong side of history as
always.  As they say: scratch a transphobe, find a racist.  While we
are here I may as well also get out:

Black Lives Matter.  Black Trans Lives Matter too.

I've added my personal informal trademark -- a trans heart flag -- to
the info screen.  If that isn't workable for a specific distro out
there, you can set Q_NO_TRANS_HEART and get the old screen without it.
My "politics" such as they are will remain confined to the worklog and
commit messages, rather than end-user visible stuff.  And after this
little mass rename of myself probably not much coming anyway.  (Though
it is galling to call my personal right to keep working for companies
I want to help out, and be treated like a normal human being by the
medical industry, mere "politics".  Welcome to
not-straight-cis-manhood I suppose.)

I've been collecting up minor bugs and a couple neat minor features
over the last few years.  I will be releasing a basic source tarball
sometime soonish.  It won't be as thoroughly tested as 1.0.0 was,
simply because I lack the time to do all that.  But Qodem is pretty
mature, and should mostly be from here on out just keeping up with the
compiler changes.  Crossing fingers.

June 11, 2017

Thanks to Ryan de Laplante, Qodem can now dial actual BBSes!  I needed
to do two major things to get it working, and afterwards I find a
little more still.  The old buoy flush_serial_port() code is still
causing lost bytes immediately after connection, and that has to go.
flush_serial_port() made a lot of sense for the embedded buoys, but it
doesn't belong here at all.

The final release date is now set: June 18, 2017.  This is the final
week.

June 2, 2017

I have been tackling a dialup failure a user reported, using a null
modem cable to simulate both ends of the link.  I discovered that
close() on a serial port could fail to flush the buffer, so added
tcdrain() to qodem_write() when sync is true to cover that case.  It
was needed because it appears that the modem the user is using is
either slow to assert or doesn't use at all DCD, so I added a new
option ignore_dcd and needed to make hangup_modem() rely on the hangup
string (+++ATH0) rather than just drop DTR.

Respun all the builds, and then noticed that I had the same %s baud
crash in host mode connection string, so have to do it all over
again.  Sigh...

May 27, 2017

I've got the release build VMs up and running again.  My main laptop
can build everything except the Mac binary, but I've got a new Mac
Mini (Sierra) to do that now.  I need to check the man page and help
text, update the BBS list, and sweep the README and index.html.  Might
get some of that wrapped tomorrow.

I've got 38 days until July 4, but need to release before June 22 so
about 25 days left on the clock to 1.0.0.  So I will get the latest
BBS list and spin the final build on Windows first, and then keep
going down the TGR list.  I probably won't hit every item on that
list, but even so this will be the most stable release since 0.3.2.

And one that I will be content being the last release for a very long
while.  It is now time to put in writing my feelings on a few
matters...

-----

Qodem has taken a very long effort, and I feel that I am reaching the
finish line just in time for the last staff member to be leaving the
stadium and turning out the lights.  Or perhaps one could say I am
putting the finishing touches on one of the coolest transoceanic
sailing ships in the world, at a time when no one has enough vacation
time from work to actually travel in it.

The programming world has become all about Optional<T>, functional
programming, and HTML/CSS/JS on the backend.  The Internet of Things
(Internet of Shit) is taking over seemingly everywhere.  Just today I
had to spend more money for a house thermostat that didn't require
wifi or a twelve-page manual to program it.  On the Linux side,
systemd ... well, I didn't have any real problems with init; nor did I
have a need for PulseAudio so that videophone calls could seamlessly
transition from speaker to USB or bluetooth headphones, I was doing
alright with alsa.  In fact, I could be pretty content with KDE 3.x,
Debian Sarge, and Java 1.7.  Or on the other side Windows 2000, Office
XP, and Firefox 3.x.

Modern computing isn't what I like anymore.  I don't want to HAVE to
be connected to the Internet or my game won't run, and I don't want my
motherboard to REQUIRE some unauditable "security processor" to have
backdoor access to everything behind my operating system's back when I
AM connected to the Internet.  I don't want my IDE to have to go
online to pull a zillion dependencies just to get Hello World running
on whatever today's latest fashionable GUI framework.  And flat icon
themes, just fucking yuck.

Last year I moved houses, and I took the unusual step of not moving my
broadband service with me.  My wife has one smartphone which we share
for email, weather, and light browsing; I have a dumbphone for texting
and voice calls.  I do my basic news headlines from work, and we go to
the local library a few hours each week to do bills and (for me) sync
with github.  It all reminds me more than a little of BBS life.  I
PLAN my time to be online now, it doesn't follow me everywhere.  I
notice all the other things that are not being done while I am online,
because online means not at home now and things like dishes and
yardwork and reading physical books is not happening when one is
clicking away at web sites.  Sometimes I really look forward to a few
hours at the library, accessing the sites my work filters off.  (Note
to industrial manufacturing companies stuck on the Microsoft stack:
filtering out open source sites is NOT helping you be more
competitive.)

But this is also a curious inversion of the BBS era: I am the one
offline, choosing NOT to sink time into conversing with people all
over the world, while everyone else in high school^W^W the world is
caught up with the latest crazes from gossip magazines^W^W
television^W movies^W Facebook / Reddit / WhatsApp / Snapchat /
Instagram / Twitter / whatever.  Professional programmers are
investing millions of lines of code into Hadoop cluster managers,
Docker container managers, CSS layout engines, and stupendously huge
thick client JavaScript libraries, while I putt putt in my hobby hours
working on bleeding-edge technologies circa 1993: file transfer
protocols, text-based user interface, a compiler targeting DOS, and a
chemical process simulator.  My biggest insights come not from
programming or engineering sites/blogs but from dead-tree textbooks
straight out of the 1980's that I pick up at used bookstores.

But unlike the BBS era, the world will never come to understand what I
like about MY stuff.  They are headed off to the singularity/cyberpunk
futures of _Rainbows End_ and _Idoru_: near-total VR immersion and
augmented reality.  What I want -- computers that work as I (and no
one else) tell them to, computers that make ME a bigger thinking
entity, programming languages that favor stability and teachability to
others over syntax fashions and IDE glitz -- will never become the
focus again.  There is no money in it.  And there never were, and
never will be enough people with the capability to do this work that
will pay for the infrastructure needed to sustain it.  Why would any
business approve an engineer's request for a $10k POWER system that
behaves as a computer should when there are $1k ARM/X86 spy-boxes for
sale in every grocery and office supply store?  The money instead is
in turning computers into telescreens, windows for other entities to
peer into one's life to slice and dice and carefully measure the
masses of humanity.  To find out their dreams in order to manipulate
them to support the existing social order, to discover their talents
and passions in order to nudge them into more profitable avenues, and
as a last resort to collect blackmail should they decide to enter
politics.  The Internet pioneers of the IETF, IANA, and everybody else
thought they were creating a fertile breeding ground for anyone to
become the next Craigslist / Reddit / Amazon / Google / Netflix, but
what they have actually brought about is the Great Firewall and the
Snowden revelations.

I am glad that Qodem has reached this point, that it is in my mind
"complete."  But I am also sad that in the time it took to get here
the world abandoned the general-purpose computing concept which tools
like Qodem are intended to support.

After Qodem is released, I will be taking a step back from programming
in general.  Some things I've learned will still be published,
someday: Jermit for the file transfer protocols, Jexer for Xterm- and
Unicode-isms.  Bug fixes will be worked on, but at a more sedate pace.


-----


March 11, 2017

The Great Regression (TGR) finally begins.  Only about 8 years after I
first put together the list, and about 40% of the total code lines
ago.

This one is going to be quite a bit different that anything else Qodem
has undergone.  If I'm lucky, the only thing that will be updating
between git commits is this log and the TODO list, showing more tests
passing.  If I'm less lucky, then I will encounter bugs that have to
be addressed before the final 1.0.0 release.

Release target is 47 days away, there are about 150 items on the test
checklist.  Some items like file transfers will get knocked down
quickly: I am confident that they work, and it will be just a couple
hours to try all of them, and 10% of the total list is done.  But some
like the Phonebook revise screen are several hours just for one line
in the checklist.  I can't possibly test all of these combinations
against all the supported platforms, so I will divide these up but put
the majority of the effort on Win32.  Serial ports and scripts will
need to be tested on both platforms though.

Along the way I also need to slow down.  Part of me wants to just
#define RELEASE, spin the builds, and off we go, and then I can put
Qodem down for some months (or years if there are no real bugs).  But
this particular release is a bit more significant than the others, and
I need to really put it through its paces.  This will be a slow burn,
yet needs to be put on the back burner too because it is about to be
spring in my new house and I need to get some real life going.  If it
turns out that I miss the April 27 date, then the next date out will
be July 4.

...

I finally found the X11 selection bug that has been killing me for the
last two years.  Somehow Xlib is sending a mouse event with button
number 0x7F00, which crashed a dereference (button should be 0-5).
Adding the check seems to fix things.  I'm going to get this pushed
out tomorrow as the new-and-improved 1.0.0.

March 10, 2017

Borland has some serious problems with colors when 64-bit chtype is
being used.  I don't have much time or interest in figuring it out,
because I am more surprised that it worked in the first place.  I
added CHTYPE_32 to the win32a and qodem builds and all the problems
suddenly vanish.

Fixed the Windows blink to match the behavior of X11.

March 9, 2017

My Win32 double-width support introduced a memory smash, got that
fixed now I hope.  Also got a color icon for the X11 version.

March 5, 2017

I have started using Qodem as my daily driver under X11 again.
Naturally I am finding lots of small glitchy things again: when my
default color has bold then LOTS of other stuff was bold too; Jexer
was relying on Xterm's erroneous behavior of bold'ing the background
color for its color picker; Shift-Tab wasn't being picked up (x11.c
didn't have the keysym in its table); and I noticed that scrollback
page-up/down was actually skipping a row (and has been doing so for
probably 12 years now).  So let's get those fixed now that I'm seeing
them.

It is weird though, seeing Linux for real using the DOS color palette.
That bold + reverse behavior of Xterm has been with me so long that
seeing CGA bold behavior is really driving home the "vintage" feel.

Also, Qodem has formally entered freeze for 1.0.0.  It's been a long
14 years, but we are finally just about here now.

   2186    5925   55727 source/ansi.c
     81     357    2454 source/ansi.h
    510    1635   13740 source/atascii.c
     68     266    1965 source/atascii.h
    931    2337   24274 source/avatar.c
     56     186    1498 source/avatar.h
   2189   10906   92406 source/codepage.c
    302     985    8063 source/codepage.h
   1761   12781   87397 source/colors.c
    282     852    6396 source/colors.h
    541    1711   12417 source/common.c
    337    1375    9532 source/common.h
     80     392    2317 source/config.h
   3918   10799  127105 source/console.c
    179     664    4661 source/console.h
   1006    2784   28552 source/dialer.c
    132     385    2880 source/dialer.h
   1424    3858   37112 source/emulation.c
    222     856    6681 source/emulation.h
    594    1862   16176 source/field.c
    276     851    5934 source/field.h
   3948   10729  130751 source/forms.c
    252    1409    9131 source/forms.h
   2891   13407  107235 source/help.c
     71     242    2095 source/help.h
   2914    7841   80967 source/host.c
    125     439    3177 source/host.h
   2422    8134   72032 source/input.c
    274     910    8069 source/input.h
   6719   19954  195548 source/kermit.c
     84     381    2659 source/kermit.h
   3833   10079  113312 source/keyboard.c
     80     275    2152 source/keyboard.h
   2805    7515   81521 source/modem.c
    291     973    7384 source/modem.h
   1431    4604   42483 source/music.c
     99     381    2805 source/music.h
   5055   15911  146646 source/netclient.c
    334    1469    8964 source/netclient.h
   2184    8771   77352 source/options.c
    255     750    7223 source/options.h
   1271    4444   39157 source/petscii.c
     77     318    2290 source/petscii.h
   8600   23536  282950 source/phonebook.c
    230     774    6132 source/phonebook.h
   2343    6660   74078 source/protocols.c
    290    1044    8075 source/protocols.h
     74     290    2151 source/qcurses.h
   3776   11952  119319 source/qodem.c
    783    2744   19188 source/qodem.h
   1552    6487   47347 source/screen.c
    575    3120   19515 source/screen.h
   1748    4970   49830 source/script.c
    176     606    4308 source/script.h
   3082    9978   94928 source/scrollback.c
    406    1705   11359 source/scrollback.h
    456     968   11711 source/states.c
    166     540    4573 source/states.h
   2401    7365   71622 source/translate.c
    150     580    4411 source/translate.h
   7770   22233  235670 source/vt100.c
    153     650    4532 source/vt100.h
    887    2124   21305 source/vt52.c
     73     282    2060 source/vt52.h
   3104    9913   88958 source/xmodem.c
    114     554    3908 source/xmodem.h
   5611   16942  164502 source/zmodem.c
     87     402    2824 source/zmodem.h
  99097  317122 3045496 total

March 3, 2017

We are down to the last feature before 1.0.0: Win32 double width
support.  And I even have several hours available this weekend between
coughing fits to make the attempt.

... later ...

We have double-width in Win32, and it was amazingly easier than X11
thanks to StretchBlt().

February 26, 2017

Qodem has many more options for portability now.  --config lets you
bypass all of the standard qodemrc locations and just load a specific
configuration file.  --create-config can put a qodemrc wherever you
want.  --dotqodem-dir lets you change where ~/.qodem is.  And
--read-only disables scripts, saves, downloads, and similar writes.
Between all of these, one can easily create a shell/batch that runs
qodem to either a) be a passive client to other systems, or b) run
entirely off a USB stick.

February 23, 2017

I ran across a version of Qmodem I have amazingly enough never seen
before: Qmodem 4.5 Lite.  It has a compile date of 01/01/1992, as
compared to Qmodem 5.0's date of 01/06/1992.  It appears to be almost
identical to 5.0, but without script support or the Alt-M mailer
command.  But nicely enough it also doesn't have an installer: drop
the files into a directory and off you go.  I must be getting old,
because at age 15 I would never have wanted the "stripped down"
version, yet here 25 (!) years later I'm thinking 4.5 Lite might be
the single best version in the collection.

February 18, 2017

We are closing in on PETSCII.  It is fascinating reading about and
playing with C64 via VICE.  I often wish I was just a few years older
and could have done the 8-bit computer thing as a teenager.  (Even
better would have been graduating with my CompSci degree in the early
1990's rather than 2000, my whole life would have so much better
financially.  Ah well, at least I won't be taken by surprise by the
re-rise of the new feudalism.)  This afternoon I'll try a couple C64
BBSes.  If it goes well then I can move over to ATASCII.

February 17, 2017

I asked the great Internet what PETSCII font makes the most sense, and
the answer is C64 Pro Mono from STYLE.  So I will be using that as my
standard baseline.  Interestingly though, it appears that xterm
recognizes that font as a double-width font anyway, and fails to
"double the double", opting instead to put spaces in between each
glyph -- the same thing Qodem does when it does not assume its
terminal can do double-width.  So my PETSCII emulation needs to
support both 40-column-with-a-good-C64-font (which means NOT setting
double-width) and 40-column-with-a-narrow-non-C64-font (which means
setting double-width).  Fun fun.

February 12, 2017

Double-width/height appear to be working on X11 now.  I had to add a
cache for performance to not totally suck, but it is there now and
will be a model for the Win32 version.

November 10, 2016

Whew, there has been a decently large code sweep.  The control keys
keyboard handling now passes control characters down to the
emulations.  40-column support is stubbed in the X11 backend, it has
more glitches than working parts but it is a start.  Gonna pack this
into a commit and then push it up.

November 6, 2016

Tim has been playing with the code and found several interesting items
to be looked at.

First, SGR 38/48 are causing crappy output on XTERM/X_UTF8 due the
awful broken-ECMA48-ness of those sequences.  I've opted to recognize
them for XTERM but silently discard them because I cannot easily do
direct RGB on all my backends.  Keeping a faithful VT100/VT220 however
requires that SGR 38/48 result in blinking off-color text.

Second, Tim put together an ATASCII branch and exposed some issues in
my keyboard handling of backspace and enter.  The general problem is
handling of control characters that cannot be directly sent to the
other side as is.  He chose to remap C_CR to Q_KEY_ENTER and ^H to
Q_KEY_BACKSPACE in keyboard_handler(), and removed the Q_KEY_ENTER
cases in terminfo_keystroke() and bound_keyboard_keystroke().  It's an
interesting find, which will help on PETSCII too.  C_CR can become
Q_KEY_ENTER pretty easily (after changing notify_prompt_form_long() to
match '\r' with Q_KEY_ENTER), but ^H and Q_KEY_BACKSPACE will need
some deeper thought.  (These remappings should also be done in
qodem_win_getch(), not keyboard_handler().)

On ^H, we have several competing issues: backspace toggle (Alt-2);
VT220/Linux/xterm mapping the actual backspace key to DEL which I then
remap to KEY_BACKSPACE (and I just noticed the VT220 terminfo is
wrong, it has kbs=^H when the VT220 manual defines that key as DEL);
and the occasional need to send ^H to the remote side (which can also
be accomplished with Alt Key Code).  Looking closer, I am already
mapping ^H to KEY_BACKSPACE on PDcurses because it never uses
KEY_BACKSPACE.  So the best thing here might be just to shove
everything into KEY_BACKSPACE and eliminate all use of 0x08 in the
forms code.

To really finish this up though, I also need to look at 0x09 (TAB) and
0x1B (ESCAPE).  Those are being passed through directly too, and
KEY_ESCAPE is actually a bit awful in that the emulations are looking
at it rather than C_ESC.

...It's later, and I think most of the keyboard handling is better.
Control characters "belong" to the emulations, except for any embedded
in keyboard macros (which will transmit raw to the other side).  I
found a couple assertion fails along the way to fix.

Gonna test this a bit more on some systems but it is close to being
ready for the rest of PETSCII and ATASCII.  When I get around to
PETSCII/ATASCII limitations in the README / help I will need to note
that carat control characters (e.g. keyboard macros, modem, split
screen) are sent raw to the other side, not processed through the
emulation.

October 28, 2016

I believe I found the hang on exit and have that fixed.  I also
noticed that SDL_CloseAudio() will sometimes hang too, I think it was
bumping heads with my background movie audio because it went away
afterwards.  Weird.

I have stubs in place now for PETSCII as an emulation.  On
ncurses/xterm it uses double-width 40 columns, which is very slick.  I
discovered that my xterm double-column trick was screwing up
non-console screens (input boxes, switching states, etc.) so added
logic to clear the double-width for those transitions.

Next up is taking the weekend off, and then looking into double-width
on Win32/X11.  I would REALLY like to get PETSCII looking nice on
Win32, I think it would be cool as hell.

October 27, 2016

It's been a productive day so far.  My Win32 and Linux dev
environments are back up and running.  I found some crazy behavior
between Qodem and inetutils-telnetd where telnetd was switching back
to ASCII and then also sending a bunch of C0 where no one else does:
NUL's and SOH (0x01) in places.  Qodem was definitely doing the wrong
thing with turning every CR into CRLF in post_keystroke() (but only
for ASCII telnet), so I got that fixed.  I also ran it against clang,
eliminating most of the compile issues seen on recent OSX (issue #39).

The C64 / PETSCII request (#41) sounds like it would be really fun to
do.  I've grabbed a copy of CGTerm and other reference materials, and
will be poking away at it off and on.  It will need to be its own
emulation (it's far more than a codepage), and I haven't added one of
those in a long time so that will be nice too.

Time to wrap today's work and get this pushed out.

October 26, 2016

Back in business!  I am in the new house and out of the old storage
units.  I still don't have my workstation set up, but have found time
to visit the library to borrow their Internet and keep moving along.

Today is a rather neat day.  Qodem just merged its first pull request
from Github. :-)  Thank you to Tim Hentenaar for your bug fixes.

September 16, 2016

I am still alive, and this project is not dead.  My summer has been
slammed with moving to a new house and several overtime work projects,
leaving this one no extra time.  I am hoping that I can put more time
into Qodem in about another two months.  Until then, I am still
looking at issues and do have plans to clear them all before releasing
1.0.

April 30, 2016

I merged current 1.0.0-dev back into master yesterday and put some dev
snapshots (source and Windows installer) on the downloads page.  Still
working on the 1.0.0-dev branch for a few more items and will re-sync
to master about every week or two.

My favorite new feature is "xterm mode", activated by the --xterm
command line flag.  It makes Qodem startup and behave very similarly
to an actual X terminal: no banner, no phonebook, no networking, no
serial port: just a local shell and then Qodem exits when the shell
does.  Yet we still have scrollback, capture, zmodem/kermit, keyboard
macros, and all the other goodies.  Combined with support for window
title changes and Shift-PgUp entering scrollback, and qodem-x11 will
soon be my new permanent X terminal.  I can finally eat my own dog
food 100% of the time.  Scary, but exciting.  (I'm already noticing
some latency that will be driving me bananas.  So that will get fixed
soon too.)

In more personal matters, we are cancelling our home broadband
Internet connection soon.  I intend to spend a good deal of time this
summer outside in the yard and further about town.  I can still do
Qodem development offline, and I can check github and personal email
during my day job, but commits will probably come in about every week
or two.

April 28, 2016

Well, that's all the bugs I currently know about cleared off the
queue.  Still have those two TODOs in the code, but we are in a nice
place.  ncurses keyboard handling is much more sophisticated now,
parsing modifier xterm sequences and also passing them through
XTERM/X_UTF8 to the other side.  The host mode bugs are basically
ironed out, though you can still get a message messed up if chat mode
is used, however now you can re-view the message before saving it.
If miniupnpc is already on the system then autoconf has a decent
change of finding and using it.

I just need to keep banging on it for another couple months and then
release 1.0.0 in July.

April 24, 2016

I've gotten a few bug reports come in, which is really nice.  Bumping
the dev version number to 1.0.0 (woo!) now.  I've decided to disable
cryptlib by default in configure.

April 21, 2016

I am down to two remaining TODOs in the code: verifying that the comm
timeouts in Windows serial port are OK at various baud rates, and
implementing a secondary strategy to map Unicode back to 8-bit.  DMG
is building for OSX, Qodem does run in Terminal.app (a little ugly,
but better than nothing), cleaned up the ChangeLog, and moved the last
two host mode bugs to the git tracker.

Some final testing for a few days, and then I will start spinning
builds.  1.0beta is coming out within a week.

[...later...]

I lied, in a good way.  1.0beta is releasing TONIGHT!

For nostalgia's sake (last done in May 2004), here is the 'wc' output:

   2168    5894   55343 ansi.c
     81     357    2454 ansi.h
    920    2308   23849 avatar.c
     56     186    1498 avatar.h
   2173   10876   92040 codepage.c
    292     946    7715 codepage.h
   1742   12687   87996 colors.c
    282     852    6396 colors.h
    522    1664   12060 common.c
    336    1373    9494 common.h
   3592    9752  114804 console.c
    179     664    4661 console.h
    956    2661   26958 dialer.c
    132     385    2880 dialer.h
   1313    3522   33423 emulation.c
    220     846    6569 emulation.h
    594    1866   16201 field.c
    276     851    5934 field.h
   3952   10719  130633 forms.c
    252    1409    9131 forms.h
   2825   13064  104528 help.c
     71     242    2095 help.h
   2813    7559   78087 host.c
    125     439    3177 host.h
   1866    5438   51069 input.c
    266     886    7835 input.h
   6704   19900  194876 kermit.c
     84     381    2659 kermit.h
   3798    9966  111823 keyboard.c
     80     275    2152 keyboard.h
   2803    7512   81465 modem.c
    291     973    7384 modem.h
   1421    4569   42226 music.c
     99     381    2805 music.h
   4971   15596  145009 netclient.c
    329    1456    8877 netclient.h
   2082    8042   71440 options.c
    231     697    6643 options.h
   8473   23026  278234 phonebook.c
    229     772    6105 phonebook.h
   2343    6660   74073 protocols.c
    290    1044    8075 protocols.h
     74     290    2151 qcurses.h
   3205   10156  103155 qodem.c
    699    2464   17267 qodem.h
   1346    5921   42253 screen.c
    571    3080   19295 screen.h
   1724    4906   49173 script.c
    176     606    4308 script.h
   2969    9604   91148 scrollback.c
    406    1705   11359 scrollback.h
    450     961   11610 states.c
    166     540    4573 states.h
   2373    7235   70606 translate.c
    150     580    4411 translate.h
   7479   21150  224054 vt100.c
    152     639    4451 vt100.h
    885    2121   21297 vt52.c
     73     282    2060 vt52.h
   3088    9855   88262 xmodem.c
    114     554    3908 xmodem.h
   5611   16942  164502 zmodem.c
     87     402    2824 zmodem.h
  94030  298689 2883343 total

April 20, 2016

We are getting veeery close.  Updated the BBS list to the current
month, fixed more codepage/widechar things, eliminated most of the
remaining TODOs (by way of documentation), and updated the man pages
and help text.

April 19, 2016

Both translation UI's appear to be complete.  Preliminary testing is
good, Unicode only in/out and back-converting Unicode to 8-bit are
both working.

April 18, 2016

More of the translate system stubbed in.  8-bit translates are working
again.  Stubs are in place for the Unicode-to-8-bit back-mapping
also.

April 9, 2016

I have stubbed in new phonebook fields for 8-bit and Unicode translate
tables.  Along the way I have also broken existing 8-bit translation:
qodem no longer knows what translate.tbl is.  But this is a
significant start.  Next up is making the 8-bit table editor aware of
the filename, then putting in something similar to the keyboard
switcher: there will be default mappings (which do nothing) and then
per-phonebook entry mappings.

After that is the Unicode editor, which I have an idea for.  Basically
the same editor as the 8-bit, except in 4 columns of 16 rows and using
hex notation.  Add a couple shortcuts to 'F'ind the key (input) or the
value (output) so people don't have to page through 64-1024 pages to
find the code of interest, and the editor is mostly done.

Gonna put this in git for now.

March 24, 2016

Just tried the latest cryptlib to see if it fixed the cipher
negotiation failure with modern OpenSSH, and it did not.  This one
will have to wait until after 1.0beta.  This royally sucks for the
Windows version though. :-(

I fixed the compile errors in miniupnpc, now it is alive again on OSX.

So where I am at right now is that I could conceivably call this
1.0beta today.  The new translate table stuff is a good idea, but it
could also be put off to 1.1 and few people would be impacted.

I am going to respin the Windows build and take a couple weeks off.
This has been a fantastic push and I feel pretty great about where it
is.  Now I need to give myself some more me time outside work and rest
up for the final leg next month.

March 23, 2016

The RPM build is working again.  I had to use Fedora 22 rather than 23
due to 23 crashing under VirtualBox (other have seen this too).
Worked out pretty well, it was almost longer getting Fedora running
than it was testing the two builds.

March 21, 2016

I announced my intention to release 1.0beta to the world a few days
ago, and happily enough I got a couple bug reports to immediately work
on.  I believe both are addressed now, but we will see what the users
see on their end.

While investigating IVT (http://home.planet.nl/~ruurdb/IVT.HTM), a
terminal with a really impressive vttest score, I noticed that it had
a feature to automatically remember passwords on logins.  This sounds
super convenient, and a real perk for a terminal to do the kinds of
things we normally think of in web browsers, but it left me feeling a
little weirded out.  Which was weird, because I normally see cool
features that are harder for me to see how to do and want to try to
crack them.  But saving passwords kind of freaked me out.  Maybe it
was also because I was playing with Arch Linux and their shell
completions are a lot more that I am used to.

But it led me to a new philosophy statement for Qodem: Qodem does not
try to be aware of semantic meaning or context on the wire.  The
reason I want this is because I don't want Qodem to act
"independently" of the user, I don't ever want to watch Qodem do
things I'm not expecting it to.  (The only exception I make is Zmodem
and Kermit autostart, but even these have safeguards against trashing
data.)  So it's OK for a script to take control -- there is the nice
status bar indicator and the split screen for its stderr -- and it's
OK for the user to press a function key to automate some things.  But
it's not OK for Qodem to act in some way other than "be a terminal" to
the data it sees from the remote side.

This eliminates three features from the wishlist: autotrigger macros,
Intellisense-style completion window, and parsing the current command
line to do ... something.

March 18, 2016

Resurrected the debian builds.  Still need to test the RPM builds
though, not sure if I want to use CentOS, Fedora, or other for that.
I also added the Telnet BBS guide to the Windows install, though it
only works for the installing user.

I am getting very close to 1.0beta indeed.  I feel like I am now
gathering all these pieces strewn across thirteen years of work:
builds, features, documentation, icons, and ... well, "feelings" I
suppose.  It's time to abandon features that I never really needed and
tie up the remaining loose ends.

1.0beta now has its final deadline: Qodem's 13th birthday, April 27,
2016.

March 17, 2016

I think host mode can answer the phone now.  I can't verify this
because I don't have a phone line, but the structure is all there and
maybe it will work.

March 14, 2016

We are getting somewhere finally.  I fixed several minor bugs,
including the blinking text not working in Windows bug.  The help text
title is working, the Windows build is now in Release mode so it
doesn't assert a debug warning on ReactOS, I've updated some
documentation, and the initial phonebook entries now include several
interesting systems.

I discovered that Big-5 encoding can hose the UTF-8 decoder making
ptt.cc a no-go for Qodem at the moment.  (...sigh...) Well, I can't in
good faith release 1.0 without supporting the largest BBS in human
history now can I?  So basic/obsolete Big5 to Unicode will be coming
in.

March 6, 2016

More bug fixes.  Now I know why mouse motion events aren't working in
PDCurses: turns out PDCurses doesn't support them in the first place.
That eliminated two of the Windows issues I was counting (drag to
select and no motion).  XTERM now clears the screen and does DECRC/SC
for alternate screen selection, which is somewhat better but not quite
as nice as a true xterm.  My list is now down to two things: modem
support for host mode, and the new translate tables design.  The
latter will be saved to do later in April (my wife will be working an
evening job so I can dedicate several 4-hour chunks to it).  Modem
host mode will probably come in sometime in the next couple weeks: I
can simulate the modem using my other PC and a null modem cable.

I like yesterday's Windows release so much that I am going to put it
out as a "beta preview".  (God I sound like Google.  Whatever.)  This
is really a nice spot function wise, just need to keep finding and
banging on bugs.

March 4, 2016

The Windows build is starting to feel a lot more solid now.  I found a
nice font that works now (UniVGA converted to VGAMedium.ttf for use in
ReactOS), it looks awesome.  Added a quick sync script to do the
unix2dos conversions for the README.md, ChangeLog, etc., and a cleaner
Windows-only README file at the end of the install.  I resynced all
the code, recompiled everything on VC6, and immediately ssh'd to a
Linux box with CryptLib working and everything.  I also finally found
the reverse video bug on Borland XTERM emulation: turns out the 16-bit
chtype makes an expression like "(q_current_color & ~Q_A_PROTECT)"
become 0 (because A_PROTECT is A_NORMAL which is define'd as
(chtype)0), which inverts to black-on-black.  Something I put in to
honor "back color erase" a long time ago.  Well, fixed now.

I was also pretty surprised when I finally got into it how generally
simple Windows serial port programming is compared to termios.  It's a
lot less "flexible", but for handling PC-to-modem and PC-to-PC
situations it seems more than adequate.  There will be some work to
test what's there, especially as I don't have a real phone line
anymore (though I do have a modem), but it's all stubbed in anyway.

I am so tempted just to call it soon and release 1.0beta, buuuuttt...
I need to get the translate tables re-worked, and find and fix a few
more of the Windows and host mode bugs.  I'm hoping to do it soon
though, maybe this July 4th.

Time to commit what I've got and get to bed.

March 3, 2016

I just stubbed in the InnoSetup install.  Very quick and easy, I like
it.

March 1, 2016

I was able to communicate at 19200 between a Win2k VM and a Linux
server using a USB-to-serial adapter on the Windows side and a true
serial port on the Linux machine.  I proceeded to send a file over
Zmodem at about 95% efficiency, using essentially no CPU on the Win2k
side.  Neat, really neat. :-)

Still have some serial port details to clean up, but this is a rather
huge milestone.

September 18, 2015

NO MORE MUCKING WITH CRYPTLIB, YAY!

...whew...

All of the features I need are in for SSH now.

This is actually a pretty important moment: all of the features that
were present before the Great Refactoring are available again: UPnP,
SDL, SSH, and X11.  But now it all runs on Windows with two different
compilers, and on Linux has both the simple Makefile.generic and an
autoconf configure script.

Speaking of X11, I really need to deliver a positive shout out to
William McBrine.  I reported the client hang bug for PDCurses back in
2012, and within a few months there was a fix waiting in the thread
(which is now in PDCurses CVS head).  It was all of a two-line fix,
but now qodem lives again in glorious Terminus font X11 frame.  I
really really appreciate William's work: thanks very much!

I'll need to take a break for a bit due to real life intruding, but
this is a nice place to be indeed.  From here on out I can focus more
on my own code and less on infrastructure stuff.

September 17, 2015

Just finished putting in known_hosts support.  I exposed a bug in
cryptlib: when you ask for CRYPT_SESSINFO_SERVER_FINGERPRINT_SHA1 it
gives you MD5, but when you set CRYPT_SESSINFO_SERVER_FINGERPRINT_SHA1
to check it later it expects SHA1.  Yeah, annoying.  I've changed it
in session/ssh2_cli.c and it seems to work OK without breaking the
host SSH server code.

I am very much looking forward to wrapping up the last couple cryptlib
things and then never touching that library again.  Of course that's a
fantasy: I'll need to retrofit my changes to its new version when it
gets around to supporting ED22519, Salsa20, ChaCha20, and Poly1305 to
talk to modern OpenSSH servers...sigh...

September 16, 2015

I brought in the old autoconf build, stripped down and cleaned up for
the new directory structure.  Along the way I had to make a
GNUmakefile for cryptlib to wrap its makefile, and a new Makefile for
PDCurses.  Interestingly, I also finally have the answer for why I was
getting the "relocation error" regarding ttytype[]: SDL pulls in
ncurses at link time which has a name collision.

So now I am staring at a bug I never got fixed before: the X11 process
part (using libXt) of PDCurses hangs during its setup, so qodem-x11
shows up as a blank white screen.  I guess it's time to learn enough
Xt now to fix PDCurses.

September 3, 2015

On that last commit a few days ago I reached a major milestone but
didn't have time to write on it.  Under VC6 and Windows 7 (haven't
tested XP), cryptlib appears to be fully functional: ssh client and
host are working.  This is really big, because cryptlib was (let's be
honest) a freaking bear to get even marginally working under Borland,
and now that I have a working version I can either figure out how
cryptlib broke under Borland or just ship a VC6 binary as the qodem
release.  I will probably end up using VC6 as the release binary, but
I do want to support Borland anyway even if it means that ssh is
broken.

I'm stubbing in support for the last third-party piece, miniupnpc.
While I'm here we'll update to the latest version 1.9 which has some
serious fixes compared to the old 1.4.  Once this is working on
Windows and Linux I'll be back to my old ways of fixing bugs and
testing.

August 26, 2015

I've got a VC6 qodem built with all of the same functionality
(theoretically) as the Borland 5.02 version.  Along the way I fixed a
lot of compiler warnings, mostly these where implicit casts and
signed/unsigned things in function signatures.

So now I have cryptlib running under its native VC6 compiler, and it's
still broken.  This time it doesn't look like it's corrupting memory
though, it is providing the same error message for both client and
server: -11 "Network subsystem not available".  For the server it
doesn this on setting the network socket, and for the client it does
it when activating the session.  Yay for more consistent behavior.

Alright, time to pack it in for a bit and get to bed.

August 22, 2015

Just a short update tonight, I'm pretty beat.  I've been beating my
head up on cryptlib under Borland 5.02.  Let's just say that cryptlib
is excessively difficult to debug when it is having trouble, due to
its kernel dispatch type design: you have to run over and over again
through the krnlSendMessage() framework just to find the right spot in
the backend that is barfing, and then continually recompile the entire
project to see what USE_ flag was missing.

I am now down to the client is sort of working but I don't know why,
because it is breaking Borland's tracing and breakpoints as soon as I
activate the SSH session.  There are other strange behaviors: the
entire app locks if I redirect stdout or stderr to a file, my DLOG()
stops emitting output (through the program keeps running) after the
activated session, and the server socket behaves as though it is
non-blocking despite me never calling ioctlsocket() to make it
non-blocking (and the listening socket is also non-blocking).  So I am
left believing that I have left a memory corruption bug in it
somewhere.  Time for the next turn: getting it running under Visual
C++ 6 so that I can compile the cryptlib unit tests and see if the
small changes I have made to cryptlib are failing any of its tests.
If they don't fail, then I go further and just start debugging in VC
until I see what it is that is different between VC and Borland.

But Borland C has proven that cryptlib is possible, and despite all
the pain of my debugging I sort of have a better idea how cryptlib is
put together now.  If I do manage to get this working then I will
submit a patch back to Gutmann.  (I also have some ideas on how to
expose the SSH terminal features, and maybe that will be generalizable
enough to be worth doing to mainline cryptlib, we will see.)

So I'm off to commit a qodem that barely compiles in Visual C++ 6.
Fortunately it caught a lot of my C89 errors which will make it easier
to run on other older platforms, yay.

August 10, 2015

I think I have settled onto the Unicode-vs-8-bit design issues in my
head now.  I wanted to think about mapping keyboard wchar_t's to 8-bit
codepage values but couldn't get past the translate tables behavior.
The way they work just doesn't make sense in the bigger picture, and
in fact the Qmodem(tm) translate tables feature seems like it probably
solved one very specific problem but was just left in place rather
than properly "finished", with these major problems:

  * It is a global setting rather than a per-dial-entry setting.  If
    one set it up for say EBCDIC then you would need to have a whole
    separate Qmodem(tm) install just for those EBCDIC systems.

  * It is unclear what program states it is supposed to operate on.
    ASCII transfers have an option to use it or not, but not the other
    protocols.  Again, this seems like a one-off "translate from
    EBCDIC to ASCII for text only" type feature.

Here is what I think needs to happen: the translate tables function
needs to be "finished" and expanded to make sense in a Unicode world
too.  I see the need for two kinds of translate tables: 8-bit that
operates on the bytes sent to and from the wire, and Unicode that
operates on the code points sent to the screen and received from the
keyboard.  Add a few special cases to handle the non-terminal-mode
states (host mode, downloads, UI screens) plus DEBUG emulation and
this can all fit together neatly.  Core philosophy statements:

1. We do not record in capture file or user data files the result of a
   destructive codeset transformation.  So: raw capture records the
   bytes before the 8-bit input translate table is applied,
   normal/text capture records the code points before the input
   Unicode translate table is applied, and data files record the code
   points from the keyboard before the output Unicode translate table
   is applied.  The only places where destructive transformations may
   occur are in the scrollback and screen output functions.  This also
   means that scrollback capture and screen dump are permitted to have
   different code points than capture file.

2. The translate tables are optional, and operate as a per-dial-entry
   function.  This will mean a new phonebook entry field.

3. If a Unicode code point does not have an 8-bit character to map to,
   the '?' will be used in its place.  This is consistent with Windows
   OS behavior and browser conventions.  Due to #1 though the user
   files (phonebook, macros) will still have the original Unicode
   data, and selecting a different codepage might make it show up.  In
   terminal mode this means that '?' will be sent to the remote side
   for un-mappable code points.

4. DEBUG emulation targets what goes on the wire.  Unicode from the
   keyboard will be encoded as UTF-8, but show up in the hex display
   as the 2-6 bytes being sent across the wire (using the global
   default codepage) rather than a wchar_t.  This will be the default
   behavior, as UTF-8 is the future.  However, I suppose I will also
   provide an option to have DEBUG encode to the 8-bit codepage, with
   the same caveat that un-mappable code points are sent to the remote
   side as '?'.

5. There will be an option for a single global default codepage.  This
   will be used to render the C0 and 8-bit region of DEBUG and TTY and
   the translate tables glyphs when q_status.codepage is DEC.
   (Further detail: qodem will never support Unicode for emulations
   that do not have a clean method of handling it.  The Unicode
   standard says that it is in essence a wire protocol so could be
   used anywhere.  However Avatar can potentially use 8-bit bytes as
   part of the protocol by way of sending raw keyboard scan codes,
   ANSI.SYS has waaaay too much BBS-era CP437 use to permit it to do
   UTF-8 too, and VT52 formally specifies itself as 7-bit.  Until
   something like a UTF-8-aware Wyse60 emulation is added, practically
   speaking the only Unicode emulations are also DEC emulations.)

6. The translate tables only apply to terminal mode, scripts, and host
   mode, EXCEPT for ASCII file transfers.  So one can use the 8-bit
   table to break UTF-8 encoding and decoding, emulation parsing, and
   Zmodem/Kermit autostart.

7. ASCII transfers will use the codepage of the current connection,
   whether that is 8-bit or UTF-8, both when receiving and sending
   files.  Another special case: if ASCII transfers are using an 8-bit
   codepage, then they will read the file as binary and send it
   directly on with no UTF-8 conversion.  In other words ASCII
   transfers CANNOT destructively convert maybe-valid Unicode into
   '?'.

8. Host mode will have an option to use either the global default
   codepage or UTF-8.  Messages will be stored in UTF-8 regardless.

9. The phonebook entry fields will display in the specific entry
   codepage.  Keyboard macros will display in the current connection
   codepage.  This will mean codepage awareness in fieldsets.

Wow, there are a lot of details here.  But the net result will be
really nice in the end.  This will deliver features like the
following:

* Support for new 8-bit codepages (e.g. EBCDIC) can be added by the
  user with a new 8-bit translate table.  qodem will ship with an
  EBCDIC table to provide an example.

* Support for "pseudo-canonicalizing" Unicode code points, for example
  converting all of the arrow forms to a single arrow, smushing all of
  the smiley face emoticons into a single smiley face, or converting
  all of the things that look like an umlauted vowel into an
  ISO-8859-1 code point.  This will also be used internally for the
  keyboard-to-8-bit-codepage conversion, by first converting to the
  same code points in the existing codepages tables and then emitting
  the index of that code point to the wire.

* The ability to convert an 8-bit codepage to Unicode, for example
  turning Wingdings into emoticons.

The other nice thing is that by defining the data pipeline and noting
where the conversions occur, it should hopefully be straightforward to
get qodem to do "the right thing".  If you want to operate on bytes,
use the 8-bit table; if you want to make glyphs look right on screen,
use the Unicode table.  There is some overlap here: the Unicode tables
could be used to do the same glyph-to-byte conversion that the byte
tables do, but along the way would be pushing code points that are
SUPPOSED to be ISO-8859-1 and pretending they are some other codepage
-- the UI I will provide would make that ugly to work with indeed.

OK, let's make this a roadmap and get that into git.  It will be maybe
a couple weeks at least before I can really crank on it, but I won't
call 1.0beta done until this is in.

August 8, 2015

I've been slammed with a major project at work the last four weeks,
leaving little time to dedicate to qodem.  However it is not forgotten
or fully idle, even now.

A user submitted some bug reports on github, which is very nice to
see.  I've addressed one of them already (up/down wrapping around in
the codepage pick dialog in phonebook), but the other significant one
is a much bigger issue, namely how should we handle keyboard input to
non-Unicode hosts?  This really drives deep into the "Output" part
(with respect to the user) of the whole I/O paradigm, and will impact
at the least: capture file, keyboard macros screen, local echo,
keystrokes themselves, and translate tables.  So let's get going on
defining the questions that need good answers:

1. In what order should the following operations happen to a byte
   received from recv(): process through translate table, written to
   capture file (raw), written to capture file (normal/html),
   converted to Unicode, modem parity handling, displayed on screen?

2. In what order should the following operations happen to a Unicode
   keystroke received from wgetch(): process through translate table,
   local echo, local echo (DEBUG), modem parity handling, convert to
   UTF-8?

3. Same as #2, but regarding a wchar from compose_key() ?  Should
   compose key bypass translate table mapping?  Also, Compose Key is
   more commonly known as Alt Code, it should be renamed anyway.

4. Should keyboard macros be aware of codepage?  If yes, how?  Each
   filename has both emulation and codepage?  Or: macros are encoded
   as Unicode always but converted in post_keystroke() to current
   remote codepage?  Should the keyboard macro editing screen show
   8-bit glyphs (ISO-8859-1) as the current codepage instead?

5. Parity processing can already hose file transfers and Zmodem/Kermit
   autostart, by definition.  Should translate table be able to do the
   same?  If yes, then it can also break UTF-8 encoding.

6. Should translate table behave differently for Unicode hosts?
   Should it be capable of breaking UTF-8 encoding?

7. What should happen when a Unicode code point that can match the
   glyph for an 8-bit codepage comes in from the keyboard?  Should it
   be mapped to the 8-bit glyph equivalent (e.g. perform the reverse
   function of codepage_map_char())?  If so, should that 8-bit char
   have multiple Unicode code points backmap to it (e.g. the gazillion
   forms of left arrow become 0x1B)?

8. What should happen when a code point comes from the keyboard that
   has more bits than any available mappable 8-bit char?  Mask the
   higher bits off (and then do what?)?  Ignore the character?  Warn
   the user?

9. Should the username and password fields as rendered by the
   phonebook UI be codepage aware?

10. Should the translate table UI screen be codepage aware?

I think this is a reasonable start to the questions.  In answering
these, what I want is for users to have sane defaults for both the
classic BBS users and the modern Xterm users.

I could do the same underlying assumption as Qmodem(tm) did: the
codepage is a system default setting, make that codepage really easy
to use and the rest of the codepages possible even if not as easy.
This could be done via a default codepage option (which would be
CP437) that controls the UI screens, and then backmap keystrokes into
remote codepage in the terminal.  The keyboard macros and translate
table screens would use the default codepage, and the
username/password in phonebook would use the phonebook entry codepage.

The other extreme is to make codepage really visible everywhere.
Maybe have a current UI codepage that is switchable any time in all
codepage-aware screens.  That would be cool to put in something like
the TradeWars CIM code in a macro in CP437 (using compose key) and
then seeing how that looks in other codepages including ISO-8859-1.

Let's let this noodle for a bit.  I'm already seeing that I need to
rename compose_key() and create two different codepage_backmap_char()
functions (one for rendering a wchar_t to UI and one for sending a
unsigned char to the remote side).

June 12, 2015

I've got cryptlib compiling under BC502, though I had to put in some
fugly workarounds here and there.  Since my qodem is currently using
the CppCompiler I also needed to extern "C" in all my headers so that
cryptlib can call back into qodem.  The code is still not quite
working, but I'll go ahead and commit at this point.

June 11, 2015

Fixed a lot of one-off things between Avatar, Avatar SGR, and Avatar
ANSI fallback modes.  Also moved the hardcode references to the
scripts directory and the stderr fifo out to the options file.  Along
the way I think I just fixed the scripts quicklearn bug, we'll see if
it comes back.

June 3, 2015

Took a week off for various personal things.  Now it's time to look at
bringing back cryptlib.

OK, I have a basic SSH client working again.  cryptlib still needs
some patches to handle environment variables and terminal resize, but
I think I can see how to do that.  It will be somewhat ugly, breaking
a lot of cryptlib's internal API, but good enough for me.

I've updated the licensing language to credit cryptlib, and the readme
to note that qodem effectively becomes GPLv3 (Sleepycat license) when
cryptlib is used but stays BSD/MIT when not.  This will have the
interesting effect that the Windows binary will actually be Sleepycat
while the non-Windows builds will be BSD.  Oh well, anyone is free to
rip out the cryptlib calls in netclient if they wish: qodem itself
remains public domain.

May 26, 2015

Linux is merged with vt100.  Xterm has some screen artifacts, not sure
why but I'll find it eventually.

Break time.

    AS OF TODAY, MAY 26, 2015, THE SOURCE TO QODEM IS DEDICATED TO THE
    PUBLIC DOMAIN.



May 25, 2015

vt100 code sweep is complete.  Still need to merge linux.c into it,
but probably won't get to that today.  We've got squirrels in the
attic to evict first.  Tomorrow will be a vacation day and I will need
some not-computer-time.

But overall we've made a HUGE push in the last four weeks.  Most of
qodem's technical debt is now paid off.  We are nearing the point
where I finally get to write all-new code again, oh that will feel
nice.

May 24, 2015

There goes netclient.  So the last thing is combining LINUX and XTERM
into vt100, and then the code sweep is finished.

In other news, I figured out that serial port support on Windows is a
lot easier than TAPI.  It's getting rolled into this release, and
Q_NO_SERIAL is getting removed.  This means that Qodem 1.0 will be
feature-complete against Qmodem 5.0.

May 23, 2015

We are ALMOST DONE with the code sweep.  All that remains is zmodem,
netclient, and combining up linux and vt100.  Of course these bits are
still about 20% of the total codebase, but who is counting?  Oh right,
I am: with the style change to 80 columns, the raw line count is past
91 kloc, even though the true code lines count (via ohcount) is 56
kloc and essentially the same as before the code sweep, which makes
sense.  On to zmodem now...

...whew!  Zmodem is so huge!  Kermit was quite big too.  OK, let's
breathe now, goodnight!

May 15, 2015

Code .... sweeep .... continues.  Ugh, this is taking forever.

In other news, I am contemplating a rather large departure for qodem:
licensing the 1.0beta as MIT or BSD license.  Let's get honest: I
don't have anywhere near as much time as I used to for this project
(or ANY coding project).  Knowing now the scope of it, I will likely
never have a period in my life like the 2003-2006 time span in which I
could do this again.  Nor will I have another job involving RS-232
where qodem becomes absolutely indispensable.  Someday I will need to
be completely off of it, and I feel like GPLv2+ would be just another
barrier to someone out there wanting to poke at qodem in the future.
Given how few people are interested in this kind of stuff, more
barriers just means Qmodem's concepts die that much faster.

GPLv2+ was a decision made back when I was all gung-ho on "Free
Software is totally different than Open Source" and "GPL is why Linux
took off and the BSDs languished".  I don't believe much of that
anymore.  Apache and nginx are examples of commercially valuable
platforms that are BSD-ish yet still going strong in the community
edition.  clang/llvm are doing fine too.  It wasn't GPL that made
Linux work, it was the leadership and community and being at the right
place at the right time.

It looks like my libraries remain OK with BSD licensing, so long as a)
the changes I made to compile my libc bits for Borland are deemed
public domain (I just added text to that effect in the CREDITS file),
and b) the source to 1.0beta is made available.  This looks good to
me.

I'm not going to pull the trigger quite yet, I need to let it sit in
my mind for a couple weeks.  If I do it there's no going back: this
would be giving the entire world -- including the sleaziest
"entrepreneurs" I kept running into in Boston -- my largest single
piece of work.  But I also know that programming is not my long-term
professional future anymore.  Things are different, and I think it's
time to take this last apron string off this baby.

May 14, 2015

Code sweep continues.  23 files to go...ugh.  But on the bright side
it is causing my eyes to bleed a lot less when browsing the code.

May 12, 2015

Code sweep continues.  Debug emulation is now refactored into
emulation.c since it is so small.  This has been bothering me for some
time, I guess it was seeing that debug.c that had nothing to do with
debugging.  Which prompted me to also decide on eliminating all of the
DEBUG_X #ifdefs in favor a simpler dlogprintf() type macro.  This will
reduce line count, de-clutter a lot of source, adds a timestamp, and
unifies all the different sources into one output log.

May 10, 2015

The headers code sweep is finished.  I found several loose ends for
later, they are now in the TODO.  I'll commit this now, but I'm sure I
broke the Borland build in qodem_write().  We'll check that later on.

May 8, 2015

On the code sweep, we are almost halfway through the headers.  I've
been cleaning up as I go along.  I took out some never-called
functions in field.c, made dialer's functions return void since no one
cares, moved dialer_get_term/lang over to emulation, and finally nixed
status.h is gone (it's had that for a long time coming).  Noticed a
different kind of failed-connection bug so fixed that -- basically I
was assuming that if DNS lookup succeeded then socket() and connect()
would succeed, but connect() will die immediately if the network isn't
available.  Checked both Linux and Windows builds, interestingly
enough Linux returns EINPROGRESS for the same condition Winsock
returns EWOULDBLOCK.

This sweep will take longer than I thought.  I've also got some real
life catching up to me again, and tomorrow need to spend a
not-computer-day.  I'll just have to keep chipping away at it over the
next couple weeks.

May 7, 2015

I've got a GNU indent profile that is pretty close to what I want, so
I am starting the code sweep now.  This will take some time, but
hopefully will be done by the end of the weekend.  After that I think
comes a regular Borland-based build and an announcement.

It would be fantastic if I could find someone out there with an actual
modem who could help me get host-mode modem support working.

May 6, 2015

The libz/libgpg_error/libgcrypt/libssh2 build path on Windows really
sucks.  I'm trying CryptLib again instead.  On the downside, this will
make things take longer to 1.0beta, but on the upside we get the SSH
host server back.  I remember now why I moved away from it: tty
terminal resizes couldn't be passed through its session interface.
Others have encountered the same, but they have also patched
cryptlib's ssh2 to pass more reasonable values.  I can put in some
custom peeking from cryptlib into qodem values if necessary to get
what I need.

May 5, 2015

More bug fixes.  Crashes eliminated in uploads, keyboard macros,
hangup, and host mode.  In general it's looking better, but it does
highlight another class of bug: calls to POSIX API's that can return
EBADF's for the most part will crash on BC5.

May 3, 2015

More bug fixes, this time in net_connect_finish() calling
dial_success() without setting pending to false, resulting in the
program state being stuck in Q_STATE_DIALER and thus missing the
beginning of input (in this case the login banner) until the user
pressed a key.  Sometimes.  (I've thought about refactoring all the
q_dial_method and telnet_read()/telnet_write()/etc calls into an OO
struct with function pointers, the way putty does it, but until I get
another several ways to connect it doesn't buy me anything new except
another round of testing.)

The Windows display also looks a lot better now thanks to switching to
Classic Console font and gamma-correcting the CGA RGB values.  This
font loses out on DEC special graphics characters though.  I'm very
tempted to learn enough font stuff to add those glyphs from
Glass_TTY_VT220 to round it out.

May 1, 2015

Lots of semi-minor fixes today.  The options are now sorted in a much
more logical fashion.  Also my colors are finally correct (I hope!),
turns out I was mapping all these nice bold-as-bright colors but not
actually using them, so the "brown" that I saw was entirely an xterm
thing and not a qodem thing.  This also makes the screen API quite a
bit simpler.  I wasted a couple hours not remembering -- and having to
dig into the PDCurses source to see it again -- that init_pair takes a
number between 0 and 1000.  Oh well.  A long day and I think the
codebase is actually smaller.  Woo.

April 30, 2015

I found a number of small glitchy things in the Borland build to fix.
I'm surprised that host mode worked in Linux at all actually, as I was
calling freeaddrinfo() right BEFORE referencing it.  Yay for Winsock
giving me an error I could trace.  I also had stupid logic in
set_nonblock() that was made the net listen socket blocking, fixed
that and now I can telnet to a qodem host.

Next up is figuring out why I sometimes get completely stuck in
Windows.  It seems like switching out of foreground and back is doing
it.  My guess is that I am missing a message of some kind in
qodem_wgetch().  (It's nice having breakpoints in my editor again, I
do like Borland.)  After that comes building the stack for SSH
support, and then a huge slew of other things.

April 28, 2015

Well!  Three years eh?  Life has had its way with me.  About six
months after the last posting we started planning a move to another
state with a new job and career track.  Roughly ten months after that
was interviewing to return to my normal (non-programming) career track
and another cross-country move.  Six months later was moving yet again
into a house.  This is actually one of the longer times I've been at a
stable address, so I finally have some mental space available to get
back into the groove.

In the last three years I have done a few interesting side projects
that are coming back around to benefit qodem.  I ported kermit to D,
and found some actual bugs in my byte encoding which I ported back to
the C code.  I also built a windowing system like Turbo Vision, first
in D and then done better in Java: it's over at
https://github.com/klamonte/jexer .  As part of that project I have a
very clean VT100/VT220/XTERM emulator, basically the grandchild of
qterminal, and it's quite nice actually.  I've also been picking off
small bugs off-and-on, driven by a very long-running TradeWars 2002
game.  (Let me also say that having over a HUNDRED TRILLION credits in
TradeWars kind of ruins the game: I don't think a helper can even
spend it faster than the interest builds, even with buying 100,000
figs at a time and dumping them ASAP.)  I also had a job of doing
packaging for both deb and rpm, and have learned a lot more about
autoconf than I ever wanted to know before.

Perhaps most important though is the use of git and github, these are
really a game-changer for open source projects.

So a couple weeks ago I got saddled with Office 2013 including Visio
2013 at work.  Long story short: they all suck ass.  Visio in
particular had so many new bugs that it killed my engineering
workflows.  It was bad enough that I decided to put together a Win2k
VM with Office XP and Visio 2002, just so that I could have some kind
of get-out-of-jail key to get work done.  Plus Win2k is soooo much
faster to load than anything since, it's ridiculous; even my little
Acer Aspire One netbook can run Win2k almost as fast as my high-end
desktops back in my IBM days.

I noticed that the Win2k VM I had setup a few years ago had Borland
C++ 5.02 already on it.  I really miss Borland C++, I had picked up
this copy in college and had some very fond memories of writing a
DirectX game with it.  It also happens to be capable of making
executables that will run on Windows 98.  Wouldn't it be really nice
if qodem could run on Windows 98, maybe even Windows 95, and every
version of Windows since?

In the last three days I've got qodem compiling on Borland 5.02.  And
today I was able to telnet to a localhost Synchronet node in glorious
ANSI color.  The new code repository is on github:
https://github.com/klamonte/qodem .

I'm doing it: we're getting the 1.0beta release this summer.  Windows
is becoming a first-class qodem build, with a fully-Windows-hosted
toolchain.  We'll start with Borland, and possibly a Visual C++ build
too if I can obtain a copy of 6.0.  The autoconf build is getting
re-written to be much simpler, and both the X11 PDCurses and mingw32
builds are going away.  Our primary targets are ncurses, and the
Win32a PDCurses build.

The code is on github and I would be thrilled to get pull requests.

May 30, 2012

I've been fixing enough bugs the last week that I went ahead and
version-bumped CVS to 1.0beta.  It will be another month at least
before I put this version out on Sourceforge, as I'd like to wait and
see if any other bug reports come in.  I'll probably do the same as
last time though: put a development download link for a while, then
maybe 3-6 months later roll it out as a full release.

A big part of the next release will be generating finalized
documentation and more thorough testing of each feature.  This is a
long slog but it's been about 6 years since I last did it.  I know
things like Zmodem downloads work well because I use them all the
time, but what about things like "compose key + split screen" or "find
in scrollback": I hardly use them so there could be some interesting
gremlins lurking.

This week I've been focusing on crash/segfault bugs, of which there
were a surprising number in the phonebook dialer which got exposed
when I routed the network connections through it.  OTOH, fixing those
also makes the modem dialer more stable which is good too.  I've also
finally found and fixed some FreeBSD bugs from a while back, one of
which would have gotten other users ("\e[;H" as the very first ANSI
code) and another was just a brain fart in one branch of Xwcsdup()
that might have gotten other non-glibc platforms.

I finally sifted through my old BBS logins.  Some boards I hadn't
connected to in almost two years.  About half are gone now, either the
board itself vanished or my username did.  Time soon to bring in
another board or two for a nice TradeWars game.

May 19, 2012

Today is release day!

Last night I switched to the win32a build and it looked very nice.
given that the only feature still out there is host mode modem support
for the original development plan, and everything else seems to be
working, I'm ready to cut here and get this out there.  The version is
1.0alpha, with a total code size right near 80,000 LOC, or 37% larger
than 0.3.2.  This has by far been the most significant code push of
qodem's entire history.

Noticed that Zmodem downloads were corrupting files thanks to Windows'
default behavior of corrupting line endings, fixed the fopen() calls
to pass 'b' in both Zmodem and Kermit and that looks good.

The only thing I have now is to upload the files to Sourceforge and
post the update on Freshmeat.  But lunch first...

RELEASED!!!

May 18, 2012

In an interesting twist, network connections now go through the
"modem" dialer.  This allows redialing through network systems, and
also is a lot prettier.  Getting the code there exposed some other
glitches that are now fixed.

Added the modem host strings and put modem lights (RI, CTS, DTR, DCD)
on the alternate status line.  Changed the hangup sequence to drop DTR
first before sending the hangup string.  I can't test it though, I
need a phone line.  Beyond testing, the only thing left on modem
support is host mode picking up the phone.  Not bad.  In fact, that is
that LAST actual feature between this place and 1.0 alpha.

Finally, I tracked down the scripting bugs: besides some conflicts
between script_start() and script_quicklearn(), there was a race
between script stdout and stderr: if stderr closed first, I was never
going beyond in script_process_data() to read from stdout and
eventually call script_stop().

May 17, 2012

The known_hosts logic is working.  It took some extra work to pull the
OLD MD5 hash of the server key in known_hosts so I could ask the user
if they wanted to overwrite it.  I had to add a base64 decoder (public
domain libb64.sf.net) and include libgcrypto for MD5.  It works out
since I'm using the same crypto as libssh2.  It also didn't help that
libssh2's examples did not cover actually adding a key to known_hosts,
I had to read their source to see that I could pass NULLs into the
comment and salt fields for libssh2_knownhost_addc().  But it's
working now.

I also just added the password login to host mode, which is really
nice.  While testing the protocols I noticed that Zmodem was the only
one actually working, which is nice because it will guide me to
getting X/Ymodem and Kermit going now.

.....aaaand with this commit I broke the 80,000 LOC milestone: 80159
lines.

[Later...]

Time for bed.  I finally got the protocols working through host mode.
It was a bit strange but makes sense now: when host mode switched to
file transfer modes, the checks in qodem.c to call telnet_read() /
telnet_write() were failing because we weren't "dialed out".  Adding
the q_host_active flag made it call telnet_X() and socket_X() as
appropriate.  And now we have Xmodem and Ymodem going through, in
addition to the Zmodem and Kermit going through earlier.  There was
also a sequence bug in Ymodem's 0 block response to NAK to fix, and
Kermit's NAK to the initial Send-Init.  With these changes plus the
password, host mode is shaped up very nicely indeed.

May 16, 2012

A very old bug in Zmodem is now fixed (and it was absurdly easy):
after 15 consecutive bad header blocks, the transfer aborts.  I also
put in a notification of a TCP connection because otherwise it looks
like a hang when it's just waiting to get the socket.

Kermit RESEND support appears to be working now.  The protocol
documentation at ftp://www.columbia.edu/kermit helped a great deal.

I have about 80% of the known_hosts code in too.  I just need to
base64-decode the host key so I can show it to the user, write new
keys to the file, and pick up the option from qodemrc to point to
~/.ssh/known_hosts (on Win32 that will be in
Documents\qodem\known_hosts).

May 11, 2012

Break time!  SSH connections are working through Win32 now.  I
switched to libssh2 and gcrypto in the end.  I think what was actually
killing it was the -mwindows flag that libcrypt/libssl needed; qodem
is a console application but libcrypt was referencing CreateDC and
other GUI functions.  But that's still a guess.

For the first time, I'm actually putting a preliminary Win32 build on
the download page.

Code size: 79412 lines.

May 9, 2012

ssh connections are working through libssh.  The API is a little
different from what I expected.  A nice clean exit will appear in our
normal ssh_channel_read() returning 0 -- which follows the standard
POSIX convention -- but if the low-level is killed before the nice SSH
handshake is completed, it returns -1 with an SSH_FATAL error.  So I
had to catch that too, internally returning it as EIO like a write to
a closed socket.  The main problem was that if I didn't catch this
correctly the select() would return EBADF.  But it's cleaned up and
appears to be working now.

I managed to get libssh 0.5.2 to "compile" under the Win32
cross-compiler, but the resulting qodem executable does not run
(immediately terminates with no output).  This is a bit aggravating.
The libssh seems to work OK on Linux, and the code is relatively clean
with only a few modifications to get it to compile.  But the resulting
binary doesn't actually work.  Grrr....

May 6, 2012

I discovered the joys of Haiku OS in a VirtualBox VM.  It's pretty
cool actually, I'm very tempted to port PDCurses to Be/Haiku and
producing a native binary for qodem.  Conveniently, the Alt-
keystrokes are already used by the Terminal application, so this
shouldn't be too different.  Since Haiku is still (sometimes) using
the gcc 2.95 compiler, I had a little bit of work moving data
declarations back up to the top of functions and blocks.  But gcc 2.95
was actually qodem's first compiler, so it wasn't too hard to get it
fixed.  Not linking fully though, the ncurses in haikuports isn't the
wide-char version.

The nice side-effect of this work is that I updated Makefile.old to be
a vanilla ncurses-based compile.  I've forgotten how much I miss the
old "just-compile-it-already" days before autoconf and automake.  It's
been nine effin' years since I could just "make" without "configure".

May 2, 2012

I've been picking things back up again.  The last few days I put in
compile guards around all the serial port code to isolate it from the
Win32 port code.  I'm planning on going to 1.0 without TAPI.  If Win32
users come in later asking for it I can slot it in later, but most
people out there are asking about BBSes and ANSI in relation to telnet
and ssh systems.

I've also started migrating from CryptLib to libssh.  There is still
more work to do, but most of it is the initial network connection, the
other polling and port management is the same.

In the meantime my tools have changed a bit.  gcc 4.6.3 introduced a
new warning (-Waddress) that flags on attr_get(), so I can't compile
with -Wall -Werror anymore.  PDCurses also hardcodes X11 directories
that no longer work, so it needs them passed into configure.  It also
segfaults during Xinitscr and I don't know why.  Renaming ttytype to
pdc_ttytype seemed to fix it, but that shouldn't have worked.  I
probably have a memory bug somewhere to hunt down.

So where are we now?  Things are a bit broken in some spots, but we're
getting closer to a working 1.0 release candidate.

October 28, 2011

Windows support continues.  LOCAL connections are working, but cmd.exe
is pretty crappy about leaving the "console" (pipe) in cooked mode, so
you have to press enter before it responds.  Scripts are running too,
and exposed a timing bug with the POSIX scripting.  It's on the list,
I'll have to get to it later.

The only major Win32-only codepath left is serial port support.  The
rest is debugging and plugging in SDL, UPnP, and CryptLib.

I'm going to have to back off a bit due to increasing work in my real
life.  But for the first time in qodem's history, I'm down to a long
list of things that can be tackled in small increments.  No more deep
dives into mountains of function.  One bug at a time, and maybe in
2012 I can wrap this up.

Code size: 79257 lines.

October 22, 2011

It is a big day for qodem today: for the first time ever, qodem
connected to a remote system, survived vttest, downloaded a file via
zmodem, and did all this while running under Windows XP.

October 18, 2011

Figured out the stalls, it was a few small things.  packet_buffer
wasn't being looked at on the entrance to every loop;
decode_input_bytes() needed to flag if it had successfully taken a
packet off of packet_buffer so that we loop again; and
find_output_slot() was giving the wrong slot during a
check_for_repeat(), leading to potentially always stalled behavior on
more than one sent file per upload.  Those are now fixed and a new
alpha released.

October 17, 2011

Kermit full duplex sliding windows are in!  There is a stalling issue
with sending, but transfers still make it.  And both sending and
receiving can survive some pretty atrocious channel conditions.  And I
found the debugging problem: some logic for extended packets was stuck
inside a debug guard.

I'm going to update the dev snapshot on the web page and park it for
now.

Code size: 77736 lines.

October 16, 2011

Kermit is getting pretty close to done.  It still has lots of testing,
but sliding windows are basically in and working as they should, at
least on reliable links.  There is at least one corner case somewhere
screwing up transfers -- it vanishes when I enable debugging which is
a pain but I'll get it figured out eventually.  I also got a quick
7-bit link check in for the parity behavior: it will always seek to
NOT do 8-bit prefixing unless it sees a serial link using 7 bits,
e.g. 7E1.  What remains is beating it to death on unreliable links and
verifying that out-of-order packets are handled correctly, and then
adding RESEND/PSEND capability, and finally MAYBE putting in locking 8
bit.  I know one simple heuristic for locking 8 that wouldn't be too
bad:

    1.  Load up a block and encode it.

    2.  Count the number of 8-bit prefixing done in step #1.

    3.  If more than 55% of the bytes are prefixed, rewind the file,
        flip the toggle, and then re-encode it again.

But beyond pedantry I'm not sure how useful it would be to add it at
this point.  I barely even have a serial port setup anymore to test
7-bit links, and all of my future plans with the Kermit protocol are
on 8-bit links with both high latency and high throughput (hence the
desire to get windowing working OK).

All this work now makes kermit.c the second-largest source file in the
whole project.  It even beat out vt100.c, which is saying something.
But on the whole, I have to say I am much happier implementing Kermit
than any of the other protocols.  Granted, I know now how to make
zmodem.c re-factor a little better, but honestly Zmodem as a protocol
is just hard to make very robust because it bleeds state between so
many layers.  Kermit has some trickiness with the SEQuence number, but
overall it is well-divided between marshalling/encode/decode and
packet state machine.

October 2, 2011

Ah, the ever-so-elusive 1.0!

A lot has happened over the last 11 months.  Qodem has gained many
features, including a help system, host mode, internal implementations
of the protocols (telnet, ssh, rlogin, raw socket), and the beginnings
of a port to Win32 based on PDCurses.  It's also had a major overhaul
of the web site using a theme based on the Elinks user manual.  And
several kind folks out there have helped get it into the Fedora
repositories, identified bugs, and helped with the Qmodem(tm) archive.

On a personal note, I've also changed jobs and moved from Texas to
Michigan, which put a 6-month hiatus on development, and also took
away my POTS line so testing modem features will be a bit complicated.

So where are we now?

Last year I was diligently doing my normal thing of working on one new
major feature between releases, when I went to bed one night and the
next morning realized that I knew how to sketch out all of the
remaining big features leading to the 1.0 release.  These were the
help system, host most, internal protocols, and the Win32 port.  The
big breakthrough was getting the PDCurses build working.  Once that
was complete there was a clear path to the Win32 build.  Host mode was
just another finite state machine; the help system was just another
full-screen dialog; and the Win32 build was made much easier with the
mingw cross compiler script.  So I decided that even if would be a
really long cycle, I would just make the next release the final 1.0
release.

I'm now down to basically just finishing up Kermit (always with
wrapping up Kermit!) and finishing the Win32 port.  The Kermit code
waits mainly for me to get a really good window at finishing it up for
good, because I intend to re-use that code in several other future
projects and I want it as high quality and ready to transliterate to
Java and D as I can get it.  The Win32 port is already underway, with
the main hurdles being the file and network I/O.  These are somewhat a
pain, but can still be worked on one feature at a time.

It's still many months away to that 1.0, but I think it will be worth
it.  If nothing else, I'm ready to get my hobby time back. :) I'm
generally enjoyed working on this project, but being a perfectionist I
keep adding one more thing and moving my own goalposts.

October 31, 2010

I see the way now.

October 29, 2010

I've got two more days I can push hard on qodem before I have to back
off and take care of real life again.  I've decided to focus on these
items:

    * Making Kermit work over unreliable links.

    * Fixing the last known Zmodem issue (a delay in seeing SUCCESS at
      the end)

    * Re-testing the dialer and modem hangup on a real dialup system.

    * Printer support

Once these are in, there are only three more releases between 0.3.3
and 1.0 on the roadmap.  Realistically, that may only be 1-2 years
away.  And 2.0 (the port to Windows) may only be 80-160 hours beyond
that now that the PDCurses build is working.  This is pretty amazing
seeing the light at the end of the tunnel on this project.

[ ... later ... ]

Today has been really productive:

    * I've got the phonebook 80/132 print function working, well
      sorta: it creates a text file and spools it to lpr if the output
      is "|lpr", otherwise it generates a "savedfon.txt" file in
      ~/qodem for the user to work with later.

    * The number pad now works, including keyboard macros, thanks to
      PDCurses.

    * I have re-mapped the colors by default to match the DOS colors.
      This works both with the PDCurses X11 build and the normal build
      running under both XTerm and Konsole.

Tomorrow I'm hoping to fixup Kermit and also actually try to dial a
real modem and see how the dialer has held up.  (It won't be too long
before an analog line capable of supporting a modem becomes a
specialty item.)

But it's time for bed now.

October 27, 2010

The last couple days I've been working on some features I've been
missing that seemed too hard to get to earlier.  The first was
customizable colors.  I was expecting a real mess of qodemrc options,
but then I thought about how multimail does it and figured out that it
would be trivially easy to do the same.  And now there is a colors.cfg
file in ~/.qodem that has templates for all of the original Qmodem(tm)
color themes.  It's pretty bad-ass to switch to the "Red Shades" after
all this time. :)

The other feature is a little more dear to me recently: I have an X11
build!  Woohoo!

This means that I no longer have to fudge with Unicode fonts, default
foreground and background colors, metaSendsEscape, and 80x25 hacks.
In short, *everything* in the Getting Started Guide is obsoleted by
the X11 build.  I spent a couple hours today figuring out how to make
a separate Debian package also so that one can install both 'qodem'
and 'qodem-x11' side by side on the same system.  Both packages so far
on Squeeze are lintian clean.  It will be a little more work to
maintain two man pages, but otherwise everything is shared.  And it
also caused me to make fixes to my semi-broken ncurses-only stuff, so
things look even better in the source.

I'm going to be tempted to roll out 0.3.3 quickly and push the big
regression to 0.3.4, but I'll have to stand firm.  It's been probably
four years since I've just been a plain old tester of this project and
I've got to exercise it more and get a really stable build out there
next time.

Overall though I'm pretty happy with where things have gone this week.

October 25, 2010

This weekend I released 0.3.2.  It's a pretty good release.  It fixes
a crash bug (from a double-free()) in the phonebook 'F'ind, exposes
the port for ssh and telnet, and fixes a bug in keyboard handler that
prevented communicating with the modem when not online (pretty big one
for serial port uses).  I made really good use of Jeff Gustafson's rpm
spec file and built a Fedora FC10 build (which is quite popular on the
download page).  (I noticed after the build that Jeff's credit was
only in the CREDITS file and not the README - I fixed that in CVS.)
Right now I am downloading the amd64 version of Ubuntu 10.04 LTS
Desktop to boot up a live CD and compile for Ubuntu 64-bit.  After
that I'll see about spinning a OpenSuSE 32-bit rpm and eventually a
64-bit Fedora rpm.

That's a lot of different builds!  The reason for so many builds is
that I'm getting really close (in total project lifetime terms I mean
- it's still a few years away) from having a really long-term stable
build.  In fact 0.4.0 might just be that build.  It will have
essentially all of the features necessary for a good modern terminal
emulator.  After a very serious regression test effort (coming in
0.3.3) I may be able to mark this project as "mature/stable" and move
my primary efforts to my next project (codename NIB).  So in about a
year I expect to take a bit of a breather.

It's been a really fun project overall though.  I've gotten very close
to this technology, and still see some potential for bringing some
other BBS concepts back to the Web 2.0 Age.  I've become a better
programmer, learned some humility along the way, and might have made
some other people happy too.  It's been very good for me.

September 16, 2010

I just rolled another release to provide the baseline for (crossing
fingers) what may become the first version of qodem in the official
Debian repository.  Martin Godisch, one of the maintainers of minicom
and lrzsz, has been helping me get the packaging up to snuff and very
soon I'm hoping we'll have a sponsored package uploaded.

This release also AFAIK fixes all of the remaining issues in the
Zmodem implementation.  Block sizes go up and down based on two
different heuristics (8k of good data: UP, 3 bad blocks: DOWN to 32
bytes/block, 10 bad blocks at 32 bytes/block: give up).  I found the
unreliable link bug: my crc flag was getting reset to CRC16 by the
receivers ZRPOS and not being pushed back to CRC32 on the next ZDATA.
Totally not what I expected the bug to be, but fixed and now files get
through even with significant "line noise" (faked with random data).
So Zmodem is looking like my best protocol.  The next major release
I'll get Kermit features implemented to beat out Zmodem :) .

I also finally fixed the View Directory window so F4 will hide/show
dot-files, and regular alphanumerics will select the next
file/directory that matches that letter.  Other fixes included sgr()
blink and underline in vt100.c (linux.c had it right - I must have
been tired on the RV Gyre when I wrote that code), SPACE parity (which
is still not great but I'll get there in 0.4.0), and rendering of the
console status line.

Overall I'm getting a nice feeling about Qodem again.  It was pretty
dicey going through the UTF-8 conversion - lots of things were exposed
as broken: codepage handling, input, and the screen.  But I've had
very few hassles with Unicode now and I'm back to putting in new
functions and rounding out the old ones.  It almost reminds me of
0.1.1 which was a really decent long term release.

Code size: 58574 lines.

September 10, 2010

Nothing like a full code sweep changing bool/true/false to
Q_BOOL/Q_TRUE/Q_FALSE to remind me of how big this thing is
nowadays...

After several years, I have finally noticed the way to always get good
behavior out of xterm.  Now I have white-on-black working AND get
metaSendsEscape by default.  Once I verify that qodem can survive
80x24, I'll be down to only one issue (Unicode font) that would hit
people with a stock xterm install.  Of course, the bad UI issues users
saw was completely my own damn fault: xterm and ncurses both had
everything I needed, I just didn't research them thoroughly enough.

August 14, 2010

I am in the final stretch to a major release.  It works (well,
compiles anyway) on FreeBSD now, and OS X, and armel Ubuntu
(Sheevaplug).  The many small details (man page, versions, copyright
statements, disable debugging, etc etc) are done.

I put up a Pootle translation server at qodem.dyndns.org:8080 , and I
re-extracted all the English strings because so many of them have
changed with the scripting support.

I'm going to build the Debian i386 and amd64 packages and then let it
sit for a couple hours to see if I end up remembering anything else.
But...it's feeling good.  I can't wait to get this out there and then
start writing a TradeWars helper. :)

Code size: 57807 lines.

August 8, 2010

This week I finally figured out the LAST major feature qodem has been
missing: scripts.  The design is *so simple* I can't believe I waited
this long to think about it...  I'm about 30% of the way there.

Basically, scripts are just processes spawned just like the connection
programs in dial_out() via forkpty(), and qodem sits in the middle and
passes post-processed data back and forth between them.  The script's
stderr is routed to a FIFO and shown in the bottom of the console
window to report messages to the user.

This design has several awesome advantages:

* Scripts can be in any language.

* Scripts just have to read from stdin and write to stdout for their
  API.

* A complex script can be developed and tested independently of qodem,
  and can use the most advanced techniques available.  Imagine a
  TradeWars helper communication through qodem but having its own I/O
  through X11.

* Qodem handles all of the emulation details, stripping escape codes
  and control characters before passing the characters to the script's
  stdin.

* With very slight changes (mainly simplification) the scripting
  infractructure is exactly what we need for external protocols.
  Someone could easily add sexyz, gkermit, or (if I ever get to it)
  HS/Link.

About the only thing we lose in this design is the ability to script
qodem itself: dialing other entries, changing emulation or toggles,
etc.  That can be rectified with a more complex handshake to the
script (maybe something like telnet IAC codes), but there might never
be user demand for it so I can wait.

I've spent years dreading scripts, with a spec and the desire to learn
lex and yacc and make a minimal-but-decent "VM" to run scripts in.
And then I realize that I'll never need that in my real work: just a
dump pipe that strips the emulation out and passes to another process
is plenty for me.

I'm both tired from coding - it'll be another week before it's
testable - and relieved that it's finally in sight.  After this
feature is in and working I'm going to cut a new release and pause for
a good long while...

July 19, 2010

I went ahead and wrapped the 0.2.1 release with a tarball and amd64
deb.  I'll get the RFS email out and see if maybe this time someone
picks it up...

July 10, 2010

Whew, lots of small updates:

* I added explicit support for multiple codepages, so ANSI BBSes might
  be CP437 or ISO-8859-1, or in the future ATASCII/PETSCII/etc.  The
  UTF-8 and DEC terminals explicitly set their glyphs so they do not
  have their own codepage settings.

* Many of the runtime toggles are now settable for each phonebook
  entry.  That's handy for when one BBS needs line wrap but another
  doesn't.

* ANSI.SYS now recognizes and discards the RIPScript detection
  sequence (CSI !).

* A new method for explicit command lines was exposed to facilitate
  connecting through proxies.  You can say connect with the CMDLINE
  'tsocks ssh foo@bar.com' for example.

* Some of my OSX modifications are now included.  It seems to compile
  and run mostly OK on my Leopard box.

I'm getting very close to calling a 0.2.1 release.  I think I'll keep
testing a couple more weeks, but the next RC is going out on the web
site now.

Code size: 56060 lines.

June 14, 2010

I'm running under amd64 text-mode only.  The zmodem crc32 code
naturally failed; changed unsigned long to uint32_t and it immediately
started working again.  Version bump to 0.2.1 while I test other
issues under amd64.

December 18, 2009

I just uploaded 0.2.0 to debian-mentors.  Hopefully it can find a
sponsor soon and get any Debian issues worked out.  I've also
contacted Jeff Gustafson regarding some packaging work he did for
Fedora.  With any luck the "final" 0.2.0 will build out-of-the-box for
both Debian and Fedora and behave well.

I'm really excited about this release.  (Of course, 10 seconds after
uploading I'm adding a minor bug to the TODO, but that's how it
goes... :) )

December 17, 2009

It's been a mad dash to get all the function I wanted in place for the
next release, but it's here now!  Unicode pretty much everywhere
except filenames, a slightly cooler info screen, fixed form handling,
and a few glitchy things better.  I'm now switching modes to
documentation, working on the man page.  Once that's in I'll be in
test mode for at least a few weeks.  These last two releases have
touched pretty much everything and need to be more thoroughly looked
at.

I changed the numbering scheme such that this next release is 0.2.0 to
designate point releases as fixes for downstream.

Code size: 54605 lines.  (I'm not even sure why I'm keeping count
anymore.  Habit I guess.)

December 12, 2009

Whew!  I finally got a decent (I hope!) Debianized build going.
Getting the "ITP" (Intent To Package) email out took some effort too:
reportbug (3.x) in Debian stable barfed due to UTF-8 issues, so I
wanted to use debian-bug in Emacs, but that required getting my local
email to run over SSL (solution: stunnel4 + exim4 changes).  But now
it's working and my development system can now send email from home.
So I got the ITP bug sent in, but I don't know if that's enough to get
the process rolling or not.

Getting it ready for Debian involved finally updating the man page to
something more useful and cleaning up many other small issues with the
build.  No matter how much time it takes to get into an official
repository, the project has benefitted very visibly from
"Debianization".

I also separated the music sequences for connecting via modem and
connecting via everything else.  The nice thing about having the music
was when I would be dialing for an hour or so and just wanted to be
notified when I got in; the modern age has kind of made that obsolete.
With this change I still get music for file uploads/downloads and
"ANSI Music".

I'm awfully close to cutting 0.1.4 features-wise.  I want to add
Unicode to the phonebook and keyboard macros, fix field_render to
behave a little better, add capture-on-connect to the phonebook, and
put in a "mixed doorway mode" feature.  I can probably knock two of
those items out tomorrow.

I'm really getting excited about this next release though.  It will
feel very nearly like what I was trying to get from the start of this
project.

December 9, 2009

I replaced the calls to libform with a very simple text field which
more closely resembles the original Qmodem(tm) look.  I'm also hoping
it makes it easier for future support of wide characters and also
porting to Windows.

Also, I got double-width and double-height lines working, and it was
SOOO much easier than I thought!  Go me!

December 4, 2009

I'm just about done with Kermit for the moment.  It still lacks some
more advanced features such as locking shift and long packets, but
it's darn close in function now to gkermit and should do quite well
over TCP/IP and reliable 8N1 serial links.  Features for unreliable
links -- mainly full-duplex windowing and better ACK/NAK behavior --
will be deferred to the next release.

In fact, that's all the 0.1.5 release will be: file transfer protocols
over unreliable links.  Zmodem needs to change its block size on
errors; Kermit could benefit from having server mode when the other
side is over a serial link.

I'm hesitant to wrap a 0.1.4 too quickly though.  I've got a lot more
shakedown to do with the Unicode and Kermit, and making sure the
emulations are more solid than the 0.1.2_en release.  I'm tempted to
bring forward a Unicode-to-ASCII map so that you don't HAVE to run a
UTF-8 console, but then I have a feeling that could be a step
backwards.  Maybe I should defer until I've had more time to eat my
own dogfood.

For now I'll leave CVS head in a good place, test over the next few
weeks/months, and then cut a 0.1.4.

Code size: 53237 lines.

November 24, 2009

I went ahead and cut 0.1.3 as an alpha release.  The Unicode and
Kermit support are pretty big and will need a couple releases to fully
iron out, but it's so much better than the 0.1.2 release I couldn't
hang onto it much longer.  I'll probably cut a bug fix release next
spring.  I wish I had more time to dedicate right now...

Anyway, hope someone out there enjoys this.

Code size: 52122 lines.

November 23, 2009

Basic Kermit receive is mostly working.  I have to admit, Kermit is a
MUCH easier protocol to implement than Zmodem.  The only "missing
features" I've seen -- file modification time -- is a deliberate
omission in gkermit, not a bug in the protocol itself.  The
documentation ("The Kermit Protocol" PDF floating around) is very
good, and the state machine is very simple in comparison to the other
three protocols.  Plus when I get stuck the gkermit code is much more
readable than lrzsz (but still looks like mainframe assembly
language).

I'm basically only one feature away (extended packet format) from
having a decently fast receive implementation.  Then I'll get into the
send side, and finally the more modern features (locking shifts and
streaming).

November 20, 2009

Whew!  I've gotten a lot done recently:

* There is a new UTF-8 aware emulation for Linux.  This allows Qodem
  to be a true Unicode terminal emulator.

* VT220 NRC character sets and the Multi-national set are converted to
  Unicode.  Plus a little tweaking and it appears to be doing the
  correct thing now in vttest.

* I've begun stubbing in Kermit.  I'm hoping to have a minimal Kermit
  implementation in pretty soon.  From the docs it looks like it will
  be a hell of a lot easier to implement than Zmodem was.

I'm ready to get some sleep and look at Kermit tomorrow...

November 15, 2009

I've spent the last few days bringing over the QTerminal Unicode
codepage support to qodem.  Getting the wide-char ncurses output
wasn't too bad conceptually, but it was a LOOONG slog through the
1000-ish direct calls to ncurses to wrap it with my own calls.  OTOH
this is a significant piece of porting to non-ncurses platforms, so
that's nice.  But what's REALLY nice is that all of the CP437 glyphs
render right - even the music symbols I couldn't get in earlier - both
on the Linux console and under xterm.  (Took a while to find a good
font that had both CP437 and the DEC special graphics characters, but
Andale Mono did the trick.)

There's also been new activity on vttest exposing some broken behavior
in VT100 and LINUX emulations.  That's fixed.

I finally gave up on using gettext to translate everything by default.
The en_US keys are now directly in the code.  This is the normal case
for most open-source projects, but now I have the ability to lose a
string and make translation harder for anyone who might come along
later.  But at least it works whether or not my locale is set.

I've got a very long list of code sweeps, but I'm hoping within a
month or two of making a 0.1.3 release.

February 23, 2008

I discovered an awesome project on SourceForge: Turbo Vision!  The
last couple weeks I've refactored the VT100/LINUX emulation code into
a QTerminal window class that runs easily under a Turbo Vision
application.  It's quite slick: I can run Qodem-inside-QTerminal and
it works nicely.  Since this version has only a few Unixy things, I'm
looking later this spring at porting it to Win32 too.

I'm hoping that the Turbo Vision maintainers might be interested in
getting this into their project.

September 10, 2007

Found two bugs in Synchronet's Zmodem implementation: 1) it doesn't do
ZCHALLENGE; 2) it doesn't do ZSKIP.  They are both rather serious,
I'll see if I can report them...

September 6, 2007

I found a bug in shorten_string() that caused a segfault, fixed that,
added a check in save_form() to prompt for overwriting a file, and ...

...I just published the 0.1.2 release!  Code size: 46683 lines.

September 5, 2007

Finally got around to checking out Xmodem and Ymodem vs. Qmodem(tm),
and also finished support for the -G protocols.  Xmodem with
Qmodem(tm) works fine, Ymodem receive works OK, but Ymodem send (from
Qmodem to Qmodem(tm)) is broken.  Qmodem gives up immediately, and
provides no clues as to why.

I'll try them out against ZOC and HyperTerminal, but won't spend much
more time on them.  They are buggy protocols and implemented in
several other places (not just lrzsz) so other people have recourse if
they need their own code.  Zmodem OTOH seems to be much more rare, and
despite its flaws it IS capable of sending files across without hosing
their contents on 8-bit-clean channels so there is some real value in
keeping it reliable.

But, assuming that I am nearly finished, I can just about wrap the
0.1.2 release next week.  That would be very nice indeed as it fixes
so many bugs from 0.1.1.

September 4, 2007

Found two bugs in HyperTerminal's Zmodem receive implementation.  Both
are now noted in the README.

Also found a SERIOUS bug in parse_packet() that would core qodem badly
when Zmodem'ing with QmodemPro.  And a bug in ZOC (it does not
understand ZCRC) and one in Qmodem/QmodemPro (they both compute ZCRC
incorrectly).  But as of now Qodem can do Zmodem upload and download
with rz/sz, Qmodem, QmodemPro, ZOC, and HyperTerminal.

August 31, 2007

Qodem can now send and receive to itself with no errors, which is a
major victory for it.  Now to test it against ZOC...

Testing against Hyperterm and ZOC, basic transfers work but there are
some oddball corner cases, in particular Hyperterm's "Skip" button on
receive sends a ZRPOS that equals file size which my code thought was
a violation of the protocol and also (I think) a 1024-byte file that
Qodem didn't end the transfer the right way with.  I found both issues
in the code and fixed them, and a bug in the input bytes buffer
handling in qodem.c that left a bunch of garbage onscreen after a file
transfer, and also the bug that entering DEBUG emulation didn't print
the header right away.  So now those are all fixed now in CVS.

August 30, 2007

Whew!  All day today has been Zmodem vs lrzsz.  I found another "bug"
in the protocol: you can't actually escape out additional characters
so it won't be possible to protect users from getting kicked out if
they upload the rsh/ssh/rlogin/telnet disconnect sequences.  I also
found half a dozen bugs in Qodem.  But it seems a lot more stable on
uploads now, and uses a heck of a lot less CPU with the new streaming
arrangement.  I have null modem cable at work I'll be able to use to
test Qodem vs Qodem on Zmodem uploads and downloads, and then I'll
need to test against ZOC on Windows to see how it behaves.

I still have some "glitches" around the file handling as one user
noticed, and I've got that whole Xmodem and Ymodem debugging cycle to
get through (sigh).  But I have hopes that when that is done I'll have
the -G protocols and will cut a new release.  Maybe (cross fingers) by
Halloween.

June 29, 2007

Spent all day reorganizing the way file transfers move data in
process_incoming_data().  Still working on getting XYZmodem working
again...not fun.  The lrzsz version of sx SUCKS to debug against.

October 11, 2006

Just found the bug that caused my TradeWars macros to crash:
substitute_string() had all sorts of errors if it had to do more than
1 replacement.  Fixed in CVS, will eventually roll out to 0.1.2.

July 20, 2006

Removed the last use of perror() and random printw().

Forked the code on my dev box to 0.1.x (patches) and main trunk.  I'm
not sure how to tag CVS appropriately, so we're stuck with the
latest-and-greatest on Sourceforge.

July 19, 2006

I added a quick section in qodem.c to set ESCDELAY to 20 milliseconds
(if it isn't already set by the user).  This makes vi much more
responsive.  :)

Now I'm starting to encapsulate the zmodem code into 'qrzsz', a
separate program to be a drop-in replacement for the public domain
rzsz.  I'm doing this because it will force me to clean-up the API to
zmodem so I can more easily add it to other programs, and also to pave
the way for the Unix HS/Link port.

July 15, 2006

I'm trying to find the "keyboard editor doesn't load keys" bug and am
stymied so far.  There!  Got it, have to switch emulation, THEN hit
Alt-J and see nothing.  OR, connect localhost ANSI, then hit Alt-J.
Looks like if I hit Alt-J from the first everything works OK.

Found it!  I was calling reset_function_key_editor_textboxes() AFTER
switch_current_keyboard().  I honestly don't know how it worked the
first time.  (I *really* need to re-factor switch_current_keyboard()
someday.)

Packaging up for 0.1.1 release.  Code size: 45237 lines.

July 13, 2005

Down to one major bug before the 0.1.1 release (unless another crops
up of course).  The disappearing cursor bug actually only appeared
with the serial port stuff because all the other callers to
notify_form() and notify_prompt_form() knew what to do afterwards.
Nice.

July 12, 2005

I added a wrapper to beep() so that we can play Linux console music
:-) .  It also gives us the ability to later surface beeps through a
real sound system if that's available.

Also, ANSI "animates" a little better at the price of performance.
Online it looks fine, but local full-screen apps have noticeable lag
drawing the screen.  So I've exposed it as an option, with the default
to NOT animate.

Three more critical defects to nail, then I'll probably cut 0.1.1.

I *think* I got the screen resizing problems fixed.  There were a lot
of small issues that arise when HEIGHT and WIDTH change suddenly, but
now it behaves a lot like xterm which makes me happy.

July 5, 2005

Found rendering problems in Avatar and ANSI emulation thanks to
Doghouse BBS.  Avatar wasn't handling cursor position ( ^V ^H row col)
correctly, and ANSI wasn't resetting cursor position (CSI u)
correctly.  With both fixes in, BRE looks good.  :)

June 27, 2005

Just found an oversight: reset_emulation() wasn't being called until a
connection was made, meaning the emulation-dependent variables in
q_status weren't being set right off.  It was exposed when I added the
compose_key() function, which I'd kind of like someday to put
everywhere in the program, so you could "compose key" a high-ASCII
name into the phonebook for instance.

June 24, 2005

Tried that download again and landed on a "telnet>" prompt.  Now
THAT'S weird!  I added escaping of "~" and "^]" to zmodem so maybe we
won't accidentally do it during a file transfer, but it's very odd
that I got the prompt at all.

I've added wrapping on column 80 to ANSI, Avatar, and TTY emulations,
currently as an option in qodemrc.  I might expose it as a toggle at
some point.

Just fixed a slight rendering bug that was affecting my TradeWars 2002
game.  :)  Control characters that were meant to be glyphs weren't
being printed.

June 23, 2005

Doh!  A long Zmodem download from a Maximus board caused lots of
*strange* behavior right as it ended.  I think Qodem responded to
something by trying to re-send the ZFIN, which did weird things to the
BBS session.  I'll have to run a download again in debug mode to see
what I missed.

Just added a check to hangup so we're not always talking about a
"modem".  Now Qodem feels to me like a telnet/ssh client that can also
do modem, not the other way around.

June 20, 2005

I put most of the important bugs/features on the Sourceforge bug
tracker so any users I end up getting will feel more confident about
reporting them.  Also fixed the Avatar crash bug and made it so Qodem
won't try to create directories just to emit a usage screen.

June 12, 2005

Got the modem to dial correctly and actually used a Maximus BBS!
Woohoo!  Exposed some problems with Avatar -- I don't yet support the
commands in the May 1, 1989 addition to the AVT/0 text.  Once I fix
those I'll be ready to cut.

Ok, got most of them in.  I'm going to pass on the two scroll region
commands for this release because I haven't ever seen a BBS use them.
:)

I've re-numbered the TODO to reflect a new roadmap.  Basically, THIS
release is the one I want to be known as "stable" for the next year or
so, yet I'll continue making patches as serious bugs emerge.  So 0.0.8
becomes 0.1.0 and the first patch will be 0.1.1 etc.  It's a good
release, way better than a lot of other 1.0's on Sourceforge and
generally one that I think many people can enjoy.  I'll also get an
English-only version out in a few weeks so we don't have the
LANG=en_US irritant factor all the time.

After I get the scripting and Kermit support in along with some other
smaller features we'll have 1.0.0.  That will be the "final"
milestone, all the things I've personally wanted/needed in my very own
terminal emulator.  Other neat goodies and the Windows port are 2.0
and 3.0, respectively.

I just de-localized Qodem.  It only took about 90 minutes, since I had
an "un_gettextize" perl script that created a sed.  Then hand editing
the sed script, and viola! English-only Qodem that does NOT require
GNU gettext to run!  It's going online now.

June 7, 2005

I finally found the one extant copy of Sam H. Smith's farewell HS/Link
post (SHSMITH.HTM) where he apparently has released the 1.21b7 source
to the public domain.  I've got a question out on Usenet about it, and
I'll begin hosting the files myself on Sourceforge to see if anyone
comes along with better information on the copyright status.  But if
it really is free I'm going to incorporate it and spinoff a
command-line client for it so that we can at least one bidirectional
protocol under GPL.  (The Hydra protocol source code is not licensed
in a GPL-compatible manner, even though its source is available.)

Also spent a couple hours poring over the last releases of a few DOS
terminals.  Only one feature is going to find its way into Qodem:
"automated trigger macros" from ACECOMM.  This would let a user define
immediate actions based on the incoming data stream, which could make
Qodem one hell of a MUD/MUSH client. :-) I'll spend some time crafting
it appropriately for the Qodem interface, but I think it will do a lot
to help users automate chores when online.

June 3, 2005

Screw RPM.  Maybe it will re-appear in a future release, but for now I
say the rpmbuild system needs to get easier to use before I want to
spend any more hours on it.  It took about two hours, total, to get
qodem packaged as a Debian .deb package, because they use standard
tools (ar, gzip) and have only an unusual filesystem layout to things.
RPM has already taken two hours and I've got almost nothing to show
for it.  A .spec file that I can't use without creating
$HOME/.rpmmacros because RPM wasn't designed from the start with a
non-root user in mind.

So now we're down to only modem dialer before release.

June 2, 2005

Looks like Zmodem is basically complete now.  It successfully does
error recovery on fast modem links.  However, uploads are slow and I
know why: I'm not filling up the write buffer as fast as I could.  Of
course, this only affects very fast (as in local) transfers, but I'd
like to eventually get it streaming full blast in both directions.  Oh
wait, testing it again and I see the problem: it's the disk access
when DEBUG_ZMODEM is defined.  Nevermind, we're seeing better than 1
megabyte/second on local transfers.

Now it's just testing the modem dialer and making an RPM target, then
we release 0.0.8.

June 1, 2005

Got Zmodem receive to do error recovery, now trying to get send to do
the same.  Almost there, but it's pretty tricky.

May 23, 2005

I found a pretty serious bug in batch_entry_window() that would cause
it to drop core if the screen size was too small (like under xterm).
Now we ask xterm to resize to 80x25 if necessary, so that should
improve things a bit.  Also, backspace was very odd.  I ended up
querying the x location in the form so that I could ignore backspace
if I was at the beginning of the field.  Sadly, no one seems to be
asking much about forms on Usenet.

Getting closer and closer to the 0.0.8 release.  Right now it's just
bugfixes and pounding on Zmodem.

May 17, 2005

Fixed the rendering bugs (cross fingers) in EDT and LSE.  Not only was
I ignoring a default value in decstbm(), I was mis-handling erasing
lines containing characters with A_REVERSE flag set -- except that
Linux console does that too and now it's expected behavior apparently.
Terminfo calls it "back color erase" (bce).  I'll probably need to do
some work later to ensure it does what most users expect.

Did another survey of the terminal emulator round-up.  Apparently
there is a project on Sourceforge.net for the Linux console, but the
code seems to be stagnant.  I'm thinking (briefly) about asking if I
can add VT52 support to it, and at least get it through vttest.  OTOH
it does handle LSE and EDT reasonably well, so it's not bad as a
VT100-ish emulator.

Other projects look neat but mostly they are focusing on X11-based
emulators.  After our 0.0.11 beta release, I'm going to start
advertising like crazy.  I really want people to use THIS code to play
their MUDs with. :-)

Long day.  Heading home.

May 13, 2005

I finally tried to use EDT and LSE on VMS, and sure enough it broke my
VT102.  I'm going to debug that use case again and get it working
before this next release.

May 4, 2005

Continuing in our run to a reasonable beta, I took out the strings
that referred to the non-existent help system, so users shouldn't be
trying to hit F1 and getting disappointed.  Now I want to add the file
transfer event logging, and we'll be almost done.

For this release, I'm going to package as .deb, .rpm, and
Slackware-ish tarball in addition to the standard source tarball.

April 29, 2005

Removed an entire class of keyboard errors by wrapping most calls to
wgetch() through my own function.  It's nice because it makes the
Linux console keyboard behave like an X (or DOS) keyboard: if you add
META/ALT to a key within a form, it's processed just like
Alt-keystroke, and most forms don't care about Alt.  So if you hit
'Manual Dial' in the phonebook and hold down Alt you'll get the same
output as if you didn't hold down Alt.  Can't believe I waited two
years to fix that.

I should be able to commit all this in about 14 hours.  I'm going to
wait another week or two before cutting the 0.0.8 release though,
because I want to flush out any remaining memory leaks and bugs in the
file transfer protocols.  I want to really shoot for an
almost-beta-quality release, something much better than my prior
releases.

April 28, 2005

Slogging through the VT220 emulation now.  Other than "protected areas
(DECSCA)" the rest of it looks rather straightforward.  Bringing VT220
into this release makes it the "big one", where there's enough
function in one place to start using it for real work.  The only
features left after this are QSL scripting (huge work), Kermit, and a
handful of small-to-medium features.

VT220 is a LOT more complicated than VT100 though.  To really do it
justice I should support a lot more character sets, which may mean
mapping them to Linux's UTF8 or ISO-8859-1.  I'm still thinking about
that though, how far I want to take it.

Re-investigating the vt100.c and linux.c is good though.  I removed a
lot of unneeded VT52 mode checks.  I probably have quite a bit more
junk in linux.c that could be tossed.

[LATER]

Well, VT220 is almost passable at this point.  Emacs, joe, and mc are
all happy, and most of vttest is too.  But I wish I could do some of
the features it's got like soft fonts and user-defined function keys.
OK, I could do the function keys, but I think that would actually
hinder users who get used to Qodem-style keyboard macros.

I should be back on land in about 48 hours, then I can commit this in
and mothball it until I'm ready to implement the scripting.

Code size:  43457 lines.

Sleep time.  G-night.

April 27, 2005

OUCH!  I just ran both Qmodem(tm) 5.0 and QmodemPro(tm) for DOS 1.53
through vttest.  OUCH!  AAAAH!  I now see why the Unix gurus in the
early 90's complained so loudly.  Qmodem(tm)'s vt100 emulation is the
*worst* I've personally run through vttest; even worse, development
for QmodemPro(tm) had *2 years* to fix it, and they didn't do a damn
thing!  Re-implement most of the UI in TurboVision, sure; but fix the
vt100 emulation to be usable, no way!

I'm embarrassed.  Royally.  I loved that program enough to clone it --
going on 1.1 megabytes of source now -- but that's *pathetic*.  I
wouldn't be surprised if that's a large reason it couldn't stay
competitive with Procomm.  They cut too many corners.  Or maybe people
abandoned them because they flat-out lied:

        "QMODEM 4.6 TEST DRIVE supports the standard VT100 functions;
        half/full duplex, set/reset modes, scrolling region, keypad
        application mode, special graphics character set, US & UK
        character sets, full display attributes including ANSI color
        extensions, programmable tabs and cursor control."

Bullshit.  Scrolling region:  broken.  Keypad:  broken.  UK character
set: broken.  C0/C1 control codes:  broken.

HyperTerminal(tm) almost gets it right, even does double-width and
double-height.  PuTTY's pretty darn close too (only one screen looked
off).  minicom's is mostly OK for real-world use (VT52 broken though).
But Qmodem(tm)...  it's real bad.

But you know what?  Qodem mostly gets it right.  I re-ran the tests
with line wrap both enabled and disabled: no change in screen
results.

HAPPY 2ND BIRTHDAY QODEM.

April 26, 2005

I now have technically all the features needed for the 0.0.8 release.
I now need to spend a lot of time fixing the memory leaks and testing
the file transfer protocols on fast noisy lines.  If I get more time
on this trip I may bring the file post-processing feature into this
release.

Fixed some amusing bugs in music.c around the handling of '&gt;' and
'&lt;'.  Fixed those, then I noticed that ZCRC *can* actually do a
real crash recovery (it takes filesize as an argument).  lsz.c was too
spaghetti for me to see it, but lrz.c sets it up for that;  so I
changed zmodem and now GC is saying that something (length 5) is
getting smashed.  (Maybe it's the ZCHALLENGE value?)  Something else
in the "fix before beta" category.

April 25, 2005

I just changed the splash screen (and fixed a bug in it).  The old one
looks "better", but it's just *too* close to Qmodem(tm) 5.0 to keep as
is.

I finally had a chance to run Intellicomm for the first time.  *sigh*
I'm really sad now.  The author put SO MUCH work into Intellicomm, and
if I had had it circa 1990 I would have been lucky.  It was designed
to automate 90% of the drudgery that was a BBS: get new files list,
exchange QWK messages, check email, store time in time bank, etc.  You
could set it to do all of the automation in the call and then beep you
when done so you could play the door games.  I would have had a lot
more time in the evenings after school, and I think a lot more fun.
"Watching TV....  Hmm, computer is beeping.  Oh!  Time for TradeWars."
But then the Neverending September rolled through and all those
functions are no longer required by practically everybody.  File and
message BBSes (particularly the DOS- and OS/2-based ones: WWIV,
Telegard/TAG/Renegade, PC-Board, RBBS, Maximus, etc.) are almost
extinct now that we've got WWW and FTP everywhere.  It's really
disheartening in a way.  I only hope he got lots of registrations to
pay for it.

But now that I've seen "keyboard repeat buffer", I think I'll skip it
as a feature.  Modern OS's have shells with history and readline.

[LATER]

Got most of dialing working now.  I won't be able to test the connect
part until next week (life offshore, oh well).  Made the phonebook
recognize the difference between a phone # and a network address.  Got
manual dial "working" (as best it can without a real phone line :) ).
Just hope the boat doesn't go down, or I'll have a bitch of a time
re-doing all this.  Code size:  41981 lines.

But you know what?  I don't need minicom anymore.  WOOHOOO!

April 24, 2005

Got minicom-style lockfiles and serial speeds up to 115200.  Don't
have 230400 yet, but neither does minicom on my hardware, so it might
be kernel driver or setserial.

Gonna be a pain adding the serial parameters to the phonebook.  Sigh.
I'm glad I waited until Qodem was functional in most other respects
before heading into the serial port stuff, I think I would have lost
patience.

I can talk to a modem now.  Just need to put the stuff into the
phonebook and then get dialing working and we'll be set.  Phonebook
now has the ability to set port settings.

Just realized that the modem is going to introduce some subtle
problems.  For instance:  if I have a phone BBS and an SSH host both
selected in the phonebook, which one should I "dial"?  If I want to
integrate the SSH seamlessly in then I need to detect the lack of
password as a "busy signal" and keep dialing.  OTOH I can put up a
message saying that automatic dial might not work well with a
non-serial connection tagged and ask the user if they want to remove
the non-serials from the list.

Taking a break, I realize that my initial impulse behind this project
has proven to be true: that we can make a decent terminal emulator a
hell of a lot faster with recent software tools than they could back
in the 80's.  Sounds like "duh", but seriously: curses handles the
keyboard about 95% right and the screen about 100% right.  (I'm quite
impressed that curses only emits what has to be changed on each call
to refresh().)  POSIX I/O makes moving bytes between pty's, serial,
and the console a breeze, relatively speaking.  By the time I get home
I'll probably have a reasonable beta (except for scripting) that could
compete with the original Qmodem(tm).  That's in only about 4 months
of full-time coding (spaced out over two years in offtime).

Damn, long two days.  Code size:  40972 lines.

April 23, 2005

Added the communications parameters form.  Now to get control of the
serial port...

This is a huge break-off from Qmodem(tm) by the way.  Qmodem(tm) was
serial-only (+ telnet only in QmodemPro 2.x for Windows).  I need to
make sure the serial port behaves in a reasonable way.  Should it
automatically open on startup, or only if we "dial" a serial number?
Should it close right after the call completes, or should it wait for
an explicit command?  When the user uses a non-modem call, should it
also unlock for other applications?

Most programs just open the serial port and assume it's theirs, or
they take the other extreme and don't talk to it at all unless they
need it to dial out.  I want to emulate a real DOS terminal though --
where the serial port is generally open unless it doesn't have to be.
Maybe.

Break time.

April 22, 2005

Well, work has sent me offshore.  This should bode well for the 0.0.8
release, only because outside of my mandated 12-hour days I've got
nothing else to do.  Yippee.

Well, I've got the modem configuration menu stubbed in.  Now to get
the serial port stuff functional...  It'll take a day to get it
right.  I've peeked a bit at minicom (thanks to the GPL) and see I'll
have to do mark and space parity at the application layer.

April 21, 2005

I can't quite get my head around the "Keystroke/Command Buffer"
feature, I think it's going to be removed.  I first saw it advertised
in the README.1ST file distributed with Intellicomm 2.01 and it
sounded neat, particularly if you need to configure a Cisco router,
but I can't quite visualize a good way to do the user interface for
it.  I suppose I could run Intellicomm and see what they do, but for
now I'm just going to leave it be.  I've got keyboard macros,
split-screen, and QuickLearn coming in that will probably cover most
people's itch I think.

If anyone out there knows how they would want to "edit typing errors
and/or re-send any of the last five BBS commands entered" please let
me know.

Fixed the leaks in keyboard.c and options.c.  Pretty silly stuff, but
easy to see how I lost track of it.  I am *so* glad the Hans-Boehm
garbage collector is out there.

April 20, 2005

Sheesh.  Tired.  Work took a lot out of me.

I'm starting to see the light at the end of the tunnel on the first
real release.  Once I get serial port and scripting support in, I'm
going to spend a long time exercising features and then cut a public
beta.  From that point I'll see about packaging for distros and ports
to other Unix-like platforms.  It'll be neat though when it happens.

I took a peek at some of the competition: minicom.  The "bad news" for
me is that minicom is *really* small and a good mix of features for
most people.  It doesn't actually uses (n)curses so it doesn't have as
much "bloat" as Qodem.  However, its VT102 emulation is poor, even
compared to the raw Linux console.  (To be fair, it was a whole lot
better than Qmodem(tm) and most other DOS-based emulators.)  I'm even
more proud now of the work I put into beating the vttest scenarios.
Specifically, I was looking for the /var/lock/LCK..ttySx code -- I'm
probably going to do a more-or-less direct rip (with attribution of
course) so that we can share a modem properly with pppd.

April 14, 2005

Send is now working.  All I need to do now is add timeout checks and
do a walkthru before 0.0.7 goes out.  I'm hoping that's TODAY.  :)

I got ZTerm to transfer a 1.5 meg file through a 3-wire serial link
(using a small Perl program to shunt characters from STDIN to
/dev/ttyS0).  I'm reasonably confident now that my implementation will
talk to Zmodem's-other-than-lrzsz.

Sigh.  There's GNU gettext FUCKING UP MY FUCKING TRANSLATION FILE
AGAIN.  This time it was 'make dist' that corrupted en.po.  I swear
the gettext "tool" chain is going away before I go to
"production/stable".

Released.  Code size:  37501 lines.

I'm getting food now.

April 13, 2005

Zmodem send is really pissing me off.  It *looks* like rz has bugs in
its CRC32 stuff.  For example, it's trying to compute the CRC on a
binary header with 8 input bytes, and of course I've no idea just
where those 8 bytes are supposed to come from (since only 5 are used
by sz).  When I switch to hex headers with 32-bit "sub-packets", I get
other problems.  This is really irritating.

Yup, it IS a bug in rz.  Namely, rz ASSUMES a CRC32 on ZSINIT *even if
the header is CRC16*.  Blech.

Using the weird negate-the-crc-function-output I used for ZCRC and
suddenly it works again.  This is really f'ed up.

April 11, 2005

Receive is now as complete is it's likely to ever get, barring bugs.
Stubbing in send, and a few minor cleanups (don't allow non-supported
protocols to be selected, etc.).

April 8, 2005

Receive is now mostly complete.  I expect some most corner cases to
pop up, particularly in the almost-never-used options (ZCOMMAND et
al), but I can cd into a directory now and say "sz *" and everything
comes down, with crash recovery and skipping already-downloaded files.
Send should be significantly easier than receive now that I know what
to put on the wire.

I cleaned up the Alt-Z help menu (alphabetized each section) and put
in a system call to fire up the editor on Alt-N configuration.  Much
nicer than hand-editing IMHO.  Next week I hope to complete a passable
Zmodem send and then cut for release.

April 6, 2005

Work took some time today, but I managed to get receive ZSKIP mostly
functional.

April 5, 2005

Wrapping up Zmodem receive.  ZCRC handling was a bitch to debug
though.  I think the CRC32 function I've got doesn't match the one in
lrzsz -- like it does some weird negation thing in ZDATA that is
correct but is supposed to be handled at my layer, but then doesn't do
it for ZCRC.  Or maybe ZCRC is just not well supported anywhere.  What
I find hilarious is the fact lrz doesn't even implement ZCHALLENGE
even though it's part of the "spec".  But anyway, now we're using
CRC32 by default and only three features away from a most decent
receive (ZSKIP, ZRPOS, and error handling).

April 4, 2005

Zmodem receive is working in a very rudimentary way.  Need to fill in
a lot of gaps still...but my writer's block is finally over!

That's it for today.  We can receive multiple files in sequence, so
long as the link is reliable already (i.e. TCP).  Still need to do
crash recovery better and get all the small breaks closed -- it's way
too easy to make me drop core right now.  But hey, receive is the hard
part, send will be a snap after this.

April 1, 2005

Progress!  I finally figured out why I *hated* my original Zmodem
code.  It was a direct rip of the X/Ymodem stuff that had this
godawful huge state machine with gobs of copied code between
sections.  I just now refactored zmodem_receive() to call
sub-functions for each major state and poof! instant readability
improvement.  Just gotta get the decode bytes function to handle the
crc part and we'll almost be back up to receiving files.  OTOH I'm
moving and it may be a few more months....

March 8, 2005

I might get some time this spring to code on this project, at least I
hope so.  I've been using Qodem at work for the scrollback and
keyboard macros functionality, it's been nice.

I finally outlined what I wanted the scripting language to look like,
after looking at Procomm, IntelliCom, Telemate, Terminate, Kermit,
Commo, and others I forgot.  And Qmodem(tm) of course.  It'll resemble
the most-critical subset of C that I use myself, but won't have
pointers or direct memory access.  However, it will have one kind of
reference (structref) so I can add all the typical data structures
necessary for complex scripting.  The VM is going to be very trivial
and stack-based; we won't be winning any speed contests with it.

January 1, 2005

AAAAARRRRGH.  Zmodem is pissing me off.  My main complaint is that Mr
Forsberg had no idea what he was doing when he put together the
"protocol".  Basically, here's what I don't like about it:

1.  The CAN escape character is used both for demarking data
boundaries (CRC escape) and encoding field data (control character
escape).  Hence I cannot run all of the input data through a decode
function and then parse out fields.  Control character escaping is
done inside the CRC check and header arguments too, so I have to make
this fugly hack job.

2.  The CRC escape sequence uses the byte within the escape sequence
as part of the calculated CRC value.  So when a file block comes in I
have to check the CRC and then magically remember to ignore the last
byte when I write data to disk.

3.  The Unix file permissions mask included in the ZFILE data is not
encoded in a platform-independent manner so I can't use it.

4.  He didn't bother documenting the fact that he was transmitting the
number of files in the batch and the total filesize in the batch, so I
can't use those either.

5.  ZCOMMAND!  What the *hell* was he smoking allowing one end of the
link to issue shell commands to the other side?

I sincerely hope that when I get to implementing Kermit I'll be able
to decode the data all-at-once before parsing for header information,
and that the CRC calculation doesn't require mucking with the file
data.

So I'm about 20% done with Zmodem, probably 50% done with receive.  I
need to add a static buffer that I copy the incoming bytes to so I can
ensure each major stage has all the data it needs before I start
decoding things.  Then get the handshaking right at the end of file
and add support for ZCHALLENGE and recovery.

THEN I need to get the send side working, and THEN I have to get these
options exposed in qodemrc.  After all that I'll cut 0.0.7.

December 7, 2004

Found some redraw issues surrounding save_form() and
view_directory().  It's ugly...I may end up redoing the forms in a
future version because they don't know enough about how to redraw
themselves over each other.

December 6, 2004

ANSI Music is now in.  What an abomination!

Almost there though...just Zmodem remaining in 0.0.7.

December 5, 2004

ANSI Music is about 60% stubbed in now.  I can play tones through the
Linux console, and I can detect and partially parse a GWBASIC PLAY
string, but I still don't have the last bit done.

I've  found  a  number  of  small usability  defects  in  the  console
(scrollback buffer  handling, VT100, line  wrap, etc.)  I now  "eat my
own dog food" and use Qodem  as my primary interface at work.  I think
this next release will really be  a good one for most people.  I still
need Zmodem finished out before I can cut it though.  Work is sending
me on a trip and I might find some time to get it done then.

October 13, 2004

I'm putting in support for "ANSI music", a serious misnomer for a
PC-ROM BASIC hack to the ANSI.SYS driver.  I've found a handful of
other tiny things to fix too before we go out on 0.0.7.

October 9, 2004

Stubbing in the Zmodem FSM now.  The last time I looked at zmodem.c I
was buried in a mess of Xmodem and Ymodem implementation junk (which
still looks NASTY as hell) and was also using the lrzsz output parsing
junk -- that was back in July.  Now I'm going for a real Zmodem
implementation to get the block size and stuff right.

September 30, 2004

Finished up the ASCII transfers (CR/LF handling).  Also added a flag
to automatically enable doorway mode on connect -- really handy when
you connect to Unix boxes and want PgUp/PgDn to work right off.

September 29, 2004

Got ASCII upload and download working, but discovered some interesting
"problems" with the pty master/slave.  I switched over to forkpty()
instead of the ripped-from-Eterm code and it behaves better by a bit.
However:

1)  The default behavior of non-blocking causes write(q_child_tty_fd)
to hang indefinitely if the program on the other side isn't actually
reading.  I had to make it non-blocking so the user can retain control
and cancel the ASCII upload.

2)  vi sucks at reading fast, hanging if the buffer fills too
quickly.  cat &gt; filename works much better.

So now to plug in the ASCII translate table functions and ASCII
upload/download will be complete.  Got the tables in, now to commit
changes...

Figured out how to get ssh to set the right lines and columns when it
connects to a remote system:  TIOCGWINSZ.  ('strace stty rows 100').
So that's fixed.  Also figured out why arrows weren't making it
through in Linux emulation, so that's fixed.  Commit and hang with the
wife now... ;-D

September 28, 2004

Figured out how to auto-detect the Linux console.  ioctl(fd, GIO_FONT,
...) will return 0 only on a VGA VT;  it returns EINVAL when run
against a PTY.  So now we always look our best (cross fingers).  :-)

September 27, 2004

I stubbed in the ASCII translation table editor.  Now to plug it into
ASCII upload/download....

September 26, 2004

I got a full-time job in mid-July and that has taken all my time.
Today is the first day I actually looked at the Qodem code and got it
to compile again.  I'm going to sweep through some old bugs for these
next few days and then work on Zmodem after.

Just fixed a bug in the keyboard file handling, and added ANSI
fallback support to Avatar emulation.

Added wildcard filter support to view_directory().  Committing it in.

July 1, 2004

Well, I'm back in Texas and resuming work on this project.  I just
ripped out the code that glues Zmodem to lrzsz, so now Zmodem is
broken until I implement it.  It's been a very slow morning, for some
reason I've just been DRAGGING along this week.  I think I'm going to
take a nap and then start into the guts of real Zmodem.

May 29, 2004

That's it!  We've got a couple bugs to iron out and I'll just let it
lie for now.  You can load up default.key, put in $USERNAME^M and
$PASSWORD^M into a key, and then use that key on sites connected to
via phonebook.

Committing it in...  We are now mothballed until I finish the move to
Texas and get settled in to a new routine.

For my future reference here is the 'wc' output:

  1449   4656  34834 ansi.c
    47    244   1675 ansi.h
   419   1229   9994 avatar.c
    45    235   1623 avatar.h
   579   5566  32498 colors.c
   126    619   4624 colors.h
   362   1769  10456 config.h
  1380   4004  38882 console.c
    57    289   2136 console.h
   214    799   5876 debug.c
    46    238   1653 debug.h
   264    819   7019 dialer.c
    45    235   1607 dialer.h
   619   1776  14680 emulation.c
   120    505   3915 emulation.h
  1105   3790  34087 forms.c
    60    325   2241 forms.h
   188    656   4550 getopt1.c
  1055   4477  30212 getopt.c
   169    903   5861 getopt.h
  3440   8920  84533 keyboard.c
    49    245   1787 keyboard.h
  3680  11363  87768 linux.c
    68    414   2727 linux.h
   437   1497  13388 options.c
    76    288   2341 options.h
  2734   8119  76040 phonebook.c
   105    393   3021 phonebook.h
  1688   5442  53558 protocols.c
   158    710   5556 protocols.h
  1122   4008  32310 qodem.c
   293   1046   7550 qodem.h
    92    362   2689 screensaver.c
    48    249   1757 screensaver.h
  1634   5829  48005 scrollback.c
    99    485   3855 scrollback.h
   286    727   6825 states.c
    91    428   3212 states.h
   143    672   4472 status.h
     6     14     92 test.c
   138    686   4457 tty.c
    43    227   1513 tty.h
  3367  10261  80211 vt100.c
    56    298   2058 vt100.h
   548   1804  14542 vt52.c
    49    256   1765 vt52.h
  2270   8264  63144 xmodem.c
    63    309   2147 xmodem.h
 31132 106450 859746 total

May 28, 2004

Wow, four whole days with no entry.  I've been slowly working through
the function key editor.  Sucks that I've got to do every small
operation for about 75 keys, but OTOH C macros makes it much easier:
I've got about six functions that are just 1 macro for each key (copy
to/from disk, copy to/from editor, load terminfo keys, etc).  What's
left is the actual using of the bound keystrokes in
keyboard_handler().  That should be a rather quick one, and then a
sweep for bugs.  I notice that aborting an edit still leaves stale
data in the textboxes on the next load.

And once I have F9==username^M and F10==password^M working, THAT'S IT
until the end of the summer.  (BTW I purchased a copy of Tim
Kientzle's <u>The Working Programmer's Guide to Serial Protocols</u>
and hope on the next development push to get Zmodem and Kermit
working.  To Mr. Kientzle: I intend to write my own code rather than
incorporate the code provided with the book.)

May 24, 2004

I've been a little lax on the worklog, but I've actually gotten more
code working.  Ymodem receive works now, but I notice 'sb' will use
128-byte blocks unless you explicitly ask for 1k blocks, AND 'sb' puts
an extraneous linefeed in the terminator block that's not part of the
spec so you always have one error count before the end.

Today I need to do some fixing up on the strings in xmodem.c and also
do dynamic re-calculation of the # of blocks when a 1k protocol sees a
128-byte block.  Got the protocol cleaned up, I *think* it's fully
working now.

Added terminfo support for unknown key presses -- should be very handy
for ANSI mode because everyone disagrees what ANSI really means.  (I
actually saw a BBS send DECAWM!  I mean that's pretty ridiculous,
DECAWM is not at all in ANSI.SYS.)

Just added commandline support, so you can say "qodem ssh hostname"
or "qodem csh" and get a shell inside the qodem environment.  Cool.

But ssh isn't passing LINES and COLUMNS correctly.  Wonder why?  I
just saw the -e none option to eliminate the escape character ~.

May 22, 2004

I'm re-factoring xmodem.c already.  I don't like the way it morphed
into spaghetti code once I added all the cases for vanilla, CRC, 1K,
1K/G, Ymodem, and Ymogem/G!  Timeouts aren't being checked in a
straightforward manner, it's manipulating q_transfer_stats directly,
etc.  Ugh.

At least I know HOW the protocols work now.  I'm fighting my way
through an Atkins diet weekend so it's slow going.  (Any of you who
have actually been on Atkins or something similar (Atkins is only the
third person to discover that all-meat diets work) know the feeling of
tiredness that comes after being in it for a few weeks.)

May 21, 2004

Ymodem send is sorta working.  I'm cleaning up small glitches in the
batch upload progress window.  YMODEM.DOC, the "definitive" source for
the protocol, is ... ahem ... a bit lax.  Other people who have
implemented their own have complained on Usenet about it, only to be
responded with "why don't you just se DSZ.EXE?"  *sigh* I'm starting
to call some of this work "digital archeology" because I have to
determine what the designers intended by looking at the existing
artifacts rather than the specifications, and also (more importantly)
because even though we depend on XYZmodem almost daily there are NO
BOOKS whatsoever in the libraries and bookstores that really discuss
it.  Hundreds of books on signalling, AppleTalk, OSI stack (which no
one actually uses), BGP/RIP routing protocols, but nothing on the good
old reliable file transfer protocols.

May 20, 2004

I've got an interesting crash.  The _() macro (gettext()) is hanging
indefinitely with a smashed stack, after I've done a Xmodem-1K
download.  Ooh, actually it's after ANY kind of Xmodem download.  This
is good, believe it or not, because I can isolate it to
xmodem_receive().  Still, it's a very odd item...  Let's rebuild and
see if it remains...still there.  But at least it's a nice
reproducible bug.  And it's not there when the transfer is aborted.
Got it!  I was fclose()'ing the same file handle twice.

Got Xmodem send mostly working, at least in Xmodem normal and CRC
mode.  I'm sure there's going to be a whole cycle for testing it
against various BBSes and versions of rx/sx before I can say they work
100%.

Now for Ymodem.  Same idea, just bigger.  Probably deal with it
tomorrow though.  Since 0.0.6 I'm trying to keep an even keel on
things.  My cat needs some more attention.  :)

May 18, 2004

0.0.6 is out!

May 17, 2004

Got the phonebook Find Text and Find Again functions working.  Zmodem
autostart also works.  I tested VT102 against flash.c and resetting
the emulation and clearing the screen corrects the display.

It's starting to feel like a very usable program now.  I can use vi
and joe from inside a local ANSI shell; the scrollback sometimes is
glitchy after some direct cursor addressing commands, but the visible
screen is 100% correct on my particular test load of mc, emacs, joe,
vi, and bash shell.  I think with 0.0.6 I'll be ready to actually eat
my own dog food as they say.

Heehee.  I just scp'd rz to a box, then ssh'd over to it, and sent sz
over Zmodem and finally used the remote sz to send rz back to me.  And
it all worked.

(Doh!  Spoke too soon.  Emacs + ANSI = color problems.  Again.)

I greyed out the 3 protocols and 1 emulation I don't yet support.  You
can select them but who knows what will happen.  *grin* I just have
phonebook Undo function and Xmodem/Ymodem to verify and then I'll
seriously be ready to cut.

May 16, 2004

Whew!  (I say that a lot lately.  :) )  I telnet'ed over to a couple
live BBSes to test the ANSI support and found a number of minor bugs
to fix.  But I slogged through it, and noticed a few oddities out
there:

   CSI Pn L (ANSI X3.64 Insert Line) is being used by Emacs with no parameter.
   CSI Pn M (ANSI X3.64 Delete Line) is being used by Emacs with no parameter.
   CSI Pn @ (ANSI X3.64 Insert Character) is being used by lynx.
   CSI Pn P (ANSI X3.64 Delete Character) is being used by lynx.
   CSI Pn G (ANSI X3.64 CHA) is being used by BBSes.
   CSI Pn d (ANSI X3.64 VPA) is being used by BBSes.
   CSI Pn b (ANSI X3.64 REP) is being used by lynx.

   CSI ! _  is being used by BBSes to detect RIPscrip.

   CSI Pr ; Pc f (ANSI X3.64 HVP) is seeing Pr=0 and Pc=0.  The
                 standard specifies top-left as (1, 1).

So I got those added to ansi.c, along with a generic control character
handler for ANSI and AVATAR.  Then I tested VT100 and LINUX against
mc, lynx, and emacs and saw that Insertion/Replacement mode had a
minor bug and fixed that.  I *think* we've got all the emulators
running correctly now.  (Certainly better than the DOS versions of
Qmodem(tm) and QmodemPro(tm).)

I'm going to divert tomorrow to phonebook stuff and finally get down
to the protocols.  I think before this is over I'm going to have to
re-implement the protocols myself.  lrzsz's code is pretty much
spaghetti on the inside (even the last maintainer commented on that
inside the source), and Qodem is completely dependent on the
English-only output of it.  I see no need to implement CompuServe B+,
but IND$FILE might be handy later down the road.

Well, more immediately:  I need to get about five features in the
phonebook, then verify AVATAR and get Xmodem and Ymodem working
through lrzsz.  When those are done -- probably three more days -- I'm
going to wrap 0.0.6.

The version beyond that is going to be focused on wrapping the
keyboard mapping functions, stubbing in some scripting hooks, and
VT220.  I don't expect to get started on that version until the end of
the summer:  we're moving across the country and I need to get a new
job.  So this week is my great push for a reasonably stable version I
can be proud of showing off.

[LATER] I just got two features in the phonebook: passwords are
covered up with *'s so casual onlookers won't see them, and the
PgUp/PgDn/Home/End navigation keys are working.  (And there was a
serious bug: the phonebook couldn't handle multiple pages of entries.)

Finally, I noticed that all the recent activity has pushed Qodem up to
the second page on Sourceforge for projects in the "Terminal
Emulators" category.  Go me!  :)

Sleepy time.  It's only 12:00am too!

May 15, 2004 pt 2

Whew.  Finally got Linux function keys and colors working alright.
'mc' looks nice, but I still have a few ANSI cursor movement commands
to implement before it's really sharp.  [LATER] Got it.  Now I can run
Qodem from inside itself and still get colors.  However, ACS_?ARROW
characters don't render for some reason.

Next up:  protocols.  I really need to debug what's going on with
Zmodem, and then get autostart working, and finally get Xmodem and
Ymodem up and running.  It's pretty bad right now, this is rather old
(for this project) code that never got a decent sweep.

[REALLY LATER] Well, I slogged through the first compile on Solaris.
Yech.  All I can say is at least it isn't AIX.  The vt100 term doesn't
have all of the graphics glyphs defined, so windows have top and right
side and letters elsewhere.  Whatever, it's 2:10am and I'm going to
sleep now.

May 15, 2004

Again, very sleepy: 1:44am.  But I've been on a rather good roll so
far.  I had this cool idea for DEBUG emulation and got that working in
only about an hour.  Then I found some books circa 1985 from the
library that talked about "teleprocessing terminals" (TTY) and
"intelligent terminals" (VT52, IBM 3270).  Heehee.  It's so easy to
forget these days how much really ran on those machines.  I remember
old Columbo mysteries where some of the "computers" didn't have
monitors.

But I recalled from the book that TTY is the evolved form of Baudot,
and those control codes were actually a semi-official standard before
ANSI X3.64 arrived.  So I beefed up tty() to properly handle the
control codes.  Then I remembered what it was like to use a real
typewriter, so for sheer perversity's sake I added underline support.
I'm actually a bit proud of the fact that my "dumb terminal" will
actually behave like a REAL non-intelligent terminal from the 1970's
would.

A few other nice touches:  up-and-down arrow while browsing the
scrollback; ENQ support on all emulations.

Tomorrow ("today" after I sleep) I'm going to add color to the LINUX
emulation, and then add the application/numeric key bindings for VT52
and VT100/LINUX.

May 14, 2004

Got my first response to a query on comp.terminals!  I've added some
emulations on the TODO.  SCOANSI looks like it'll be a good
challenge...especially on the keyboard mapping.

It's been a busy night.  I've got Linux console emulation separated
out, tomorrow I'll add the color changes.  VT52 is now a "real" VT52
as in it's very limited, but I added ANSI colors (parameterized)
because 'ls' puts the colors out even though the termcap clearly does
not have them.

Sigh.  I'm quite sleepy.  I'll finish this update tomorrow.

May 12, 2004

What in the hell is a "fuzzy translation"?  It all started with a
debian utilities update and FOUR HOURS LATER I could finally compile
the project again.  I love GNU but DAMNIT this was a FUCKING WASTED
DAY because they stopped supporting their old way of doing things.
Naturally they say "read the documentation" except that the docs don't
have a section called "What we recently changed that probably broke
everything you're doing."

Then when it compiled again I was trying to see the unknown keycodes
on the numeric pad and the translated message wasn't being used!  Huh?
More digging and I discovered the following:

1.  msgmerge marks entries 'fuzzy' for no apparent reason.  Then these
entries are NOT ACTUALLY USED at runtime!  What the hell is that
about?  I can't get msgmerge to really believe that a given string is
the right translation.

2.  msgmerge marks entries 'obsolete' that aren't directly visibly
accessed in the code.  Guess what?  That means you can't have a
programmatic key string to do the lookup at the appropriate time later
or in a loop (like I do in options.c).

I am very seriously considering ripping out all the GNU build utils
and returning to a simple Makefile.  This is pissing me off like you
wouldn't believe.  I keep running into these productivity traps and
the documentation just gushes about how wonderful the "GNU Way" makes
life easier.

You know what makes life easy?  Simplifying the project down so that
very little platform-specific code is in it, a 'make what' shows the
platforms supported and 'make XXXX' builds it.

[LATER]

Nah, I can keep this problem isolated to the po/ Makefile's.  It's
just msgformat that is screwing up the en.po before it ever installs.

May 11, 2004

Version 0.0.5 is now released!

May 10, 2004

I was just going to take a "break" from the hard VT100 stuff for a
slick-ism:  when run from a Linux console Qodem uses VGA glyphs, but
when run anywhere else it uses ACS_* characters.  It's very subtle but
*damn nice*:  "correct" drawing characters around the menus and such.

A "side effect" of this work is that the VT100 emulation now supports
multiple character sets.  :-)  So though I didn't plan it at all,
we've now got the most critical 2/3 of the special graphics character
set available, AND we have the British pound sterling symbol in the
right spot.

Quick line count recap:

   658   1869  15007 ansi.c
    45    235   1609 ansi.h
   299    868   6811 avatar.c
    45    235   1623 avatar.h
   578   5564  32478 colors.c
   126    622   4638 colors.h
   276   1353   7837 config.h
  1232   3588  35375 console.c
    54    277   2050 console.h
   252    805   6742 dialer.c
    45    235   1607 dialer.h
   369   1169   9947 emulation.c
   103    446   3394 emulation.h
  1093   3761  34846 forms.c
    55    296   2045 forms.h
   374    833   6909 keyboard.c
    42    225   1537 keyboard.h
   413   1404  12433 options.c
    72    284   2245 options.h
  2064   6339  59978 phonebook.c
    99    370   2802 phonebook.h
  1730   5880  55318 protocols.c
   109    477   3663 protocols.h
   923   3135  25320 qodem.c
   210    772   5532 qodem.h
    93    364   2709 screensaver.c
    48    249   1757 screensaver.h
  1544   5460  45335 scrollback.c
    98    477   3732 scrollback.h
   270    692   6540 states.c
    88    415   3111 states.h
   123    580   3883 status.h
   124    635   4150 tty.c
    43    227   1513 tty.h
  3071   9278  71569 vt100.c
    51    267   1828 vt100.h
   464   1245  10594 vt52.c
    45    235   1609 vt52.h
 17328  61166 500076 total

VT100 is getting a bit large, but it effectively does everything now
except double-sized characters (never) and VT102 insert/delete
character/line codes.

There's a serious bug in the VT52 sub-mode that broke the test, but I
haven't dug into it.  I had a chemical engineering final exam this
morning that took precedence.  *grin*

May 9, 2004 part 2

Oops.  Just broke the VT52 test when adding origin mode and scrolling
region stuff.  Probably doing something wrong in the
scroll_up/scroll_down code in scrollback.c....

Well, I'm getting really darn close to wrapping 0.0.5.  I've just
got to fix the VT52 tests, then add support for the VT102
insert/delete codes.  I've got plenty other things I'd like to get in,
but I'm already better than QmodemPro(tm) was.  I still need to test
the VT100 and ANSI support with mc, joe, emacs, vi, and lynx.  Once
those are all happy with both emulations I'll call emulation work done
until 0.1.0.

Then comes (whew!) the Zmodem tests across long connection chains.
Once I've got that working -- which should just be figuring out the
command-line flags to pass to it -- the I'll close on 0.0.5 and start
fleshing out the remaining phonebook functions.  I'm hoping to get
past the VT100/ANSI and Zmodem stuff in the next two weeks.

It's been fun though getting this working again.

May 9, 2004

Wow, look at that.  Five solid months of inactivity.  Ah well, we made
some good progress today.  We've got cursor positioning, VT52
compatibility, and terminal reports supported in VTTEST.  The really
big kahuna still remains: "screen features".  And I'm definitely going
to have to make some compromises on that one.  There's just no way to
draw the graphics glyphs with a curses-based program, nor is there
"smooth scroll".  So I'll probably have to detect and "box" out the
glyphs that won't render correctly and prevent codepage changes.
Small price I suppose for my target audience: they should still have
good emulation on VMS systems and over ssh/telnet to typical
terminfo/termcap'd applications.  I'll just make a note on it as a
known limitation and recommend xterm for those who really need
accurate-looking glyphs.

I still want to add a layer of codepage translation for Qodem itself,
so that for instance it can detect that it's running on a non-raw
console and then use the curses ACS_* characters instead of IBM VGA
glyphs.  Ah well, I've been coding pretty much 100% for the last seven
hours, time for sleep.

January 1, 2004

First day of the new year.  My resolutions to the wife are 1) to be
busy, and 2) to stop buying shit from eBay.  :)

I had surgery December 19, and got laid off December 23 (it was
announced November 19).  So now I've got more time to put into real
code and not the bullshit pay-bills code.  Yay.

I could rant a bit about how bad corporate code is, but alas I can't
give any details because I signed a non-disclosure agreement about my
slave^H^H^H^H^Hemployment with IBM.  (I AM allowed to say I worked for
IBM, but I can't name any customers who bought the junk, what I did
specifically for the company, or exactly who inside the organization
is running it into the ground.)  Fortunately my open-source work is
nowhere near the domain that my one-year non-compete clause covers
("autonomic computing" and/or "web site analytics").

Regardless, I'm coming back up the curve to this code.  I fixed the
tabulation set/clear so that vttest passes.  I'm still working on the
big kahuna "cursor movement" test that even Linux console can't fully
pass.

November 16, 2003

vt100.c is now the largest source file.  Go figure.  :)

November 14, 2003

I got most of the VT100 functions stubbed into the state machine.  The
remaining functions are tab stops (horizontal and vertical) and
scrolling regions.  I just re-factored the functions into real
functions and suddenly VT100 doesn't look so bad.  Complex for sure,
but thanks to http://vt100.net it's not too crazy.

In other news, I'm stuck at a meeting at work.  It's VERY boring,
corporate junk, the kind of thing that naturally happens when managers
intersect programmers.  They think that a new process will make up for
passionless clock-watchers who just happen to know a little Java.  For
anyone actually reading this, stay away from anything with "Tivoli" in
its name.  'Nuff said.

October 31, 2003

I finally fixed a stupid bug in cursor_right() that made 'mc' look
like crap under ANSI emulation.  Now mc looks good, so we've just
about got ANSI beyond what ANSI.SYS could do, so it should be suitable
for BBSes.

I also put the skeleton in for the VT100 state machine.  It's going to
take a while to get right, and the testing effort will probably be a
bitch, but by God we're gonna have a solid VT100 before I switch the
project release state to alpha.

October 29, 2003

Been culling small bugs, it's very fun again.  I verified that uploads
and downloads over ssh are working.  Once I get the vt100 through
vttest and cut 0.0.5, I'll have matched the critical features of the
zssh project...that'll be cool.  I'm sure zssh has better scripting
support right now, but that'll come to qodem eventually.

October 20, 2003

Just cut 0.0.4!  Wee!

Integrated with the Hans Boehm garbage collector.  Pushed the
remaining phonebook stuff to 0.0.6.  Now it's vt100 and vttest.

October 16, 2003

Added the logic in for Q_STATE_UPLOAD and Q_STATE_BATCH_UPLOAD.  No
idea if it works yet of course.  :)

My Emacs learning curve is mostly complete.  Lots more Ctrl keys to
mess with, but I do like the syntax highlighting and parenthesis
matching.

October 9, 2003

Got batch_entry_window() mostly stubbed in.  The next major piece is
actually batch uploading.

October 6, 2003

Fixed view_directory() finally.  That's an important screen before the
batch upload window.

October 5, 2003

I finally had time to write some non-Java code in a while.  Life's
been busy as hell these last few months...got married August 1, took
three trips out of town (one to Texas for 10 days), etc.  The Texas
trip was fun...besides all the great food I had a chance to setup
computers for my new in-laws and it was refreshing to be a geek
without the paycheck riding on it.  It's fun coming up with analogies
to explain why computers have such illogical behavior ("Why
single-click here but double-click there?  How come steering wheels
are round?  It just came that way.")

The last few weeks I've been digging into alternative languages and
it's nice.  Did you know Pascal is still alive and kicking out there?
I've also been looking at Python, D, Ada, and Smalltalk.  And in the
office I had to do a bit of Perl.  Then I read a bunch of "java sucks"
rants online.

The net result is I feel so much freer thinking about code again.  I'm
curious now about how C++ templates work.  I abandoned them in
ShiftyNet for the sake of portability, but then again it would be nice
to figure out how to get GCC to only create one implementation object
file.

But I digress.  Qodem is alive again.  I finally got around to fixing
the phonebook format to store attached notes to entries and cleared a
few bugs.  The next big item is upload support.

August 13, 2003

Gotta switch back to emacs...  Renamed "dirty" to "q_screen_dirty",
now all globals are prefixed with q_.

July 14, 2003

Meetings at work are wonderful opportunities for rote work on this
project.  I'm about 80% complete on gettext-izing console.c, after
that it'll be phonebook.c and then I'll focus on fixing the known
bugs.

June 25, 2003

Emacs is proving to be...interesting.  Like the default behavior of
untabify, NO!  Gotta figure it out soon.  I want END to be
end-of-line, not end-of-file, likewise HOME. We'll get there
eventually, in the meantime I got gettext working.  Now, I'm going to
force EVERYTHING through the gettext layer.  This will mean the binary
image of qodem won't work without the locale files (ooh, changes to
the .deb) but it'll put English on equal footing with every other
language.

June 24, 2003

Started gettext.  Gonna begin switching to Emacs finally.  (FYI my
editor for the last four years has been 'joe'.)

June 19, 2003

Got the phonebook load function working.  Added "RAW" capture type,
about to add HTML.  Gotta run to the auto insurance agent this
afternoon though...

Released this code as 0.0.3.  Bumping to 0.0.4.  This next release is
intended to close out some of the bugs and re-factor for a better
long-term future.

June 7, 2003

Split screen is partially functional.  Now adding directory list.
Mostly functional now, just need to plug in the wildcard/regexp
support.

June 3, 2003

Got the sort in, hideously inefficient but who's looking?  It works,
now I think it's split screen.

June 2, 2003

Started stubbing in the sort FON function.  Got the call info working
yesterday.  I've gotta get to 0.0.4 ASAP, because I'm interviewing for
positions at startups and as soon as I get an offer I'll be head down
into the paid-for work until January.

May 28, 2003

I'm moving most of these kind of entries to the ChangeLog now.  Added
a debian package target.

May 27, 2003

Moved the carriage return and linefeed handling to emulation.c.  That
opens the door for a full vt100 later.

May 21, 2003

Stubbing in the screensaver.  Hmm...got a small "issue": the
screensaver won't know to kick in while blocking on keyboard input.
Simple workaround: if the screensaver is on then the keyboard will
never block.  Not a big deal since the phone directory has most of the
keyboard-intensive input.  Except for the forms functions
(notify_form(), save_form(), etc.).  I'll open some bugs on
SourceForge.

Released 0.0.2.

Got automake to create Makefile.in from Makefile.am.  Not bad.
Starting the debian package builder work.

May 20, 2003

Got the insert/delete working on the phonebook, along with pick lists
for the method and emulation.  Added an idle disconnect.  Screen saver
is the last feature before 0.0.2.

May 19, 2003

Got the phone entry revision screen in.  Mostly works, except for
choosing emulation and method, but fonebook.txt can be manually
edited.

May 18, 2003

Stubbing in the phonebook screen now.

May 17, 2003

Added command-line support for the other connection methods, now we
can actually be used as a telnet or ssh client.

May 16, 2003

Cleaned up the capture and screen dump routines to make use of
open_workingdir_file().  Found some history regarding Qmodem(tm) from
the BBS timeline on www.textfiles.com, put it in the README.

May 15, 2003

Moved the state-switching stuff to states.c.

Moved the ANSI emulation code to ansi.c.  This will help us get
VT52/VT100 later.

Alt-P Capture File now works.

May 14, 2003

Added a man page.

Alt-2 Backspace/Del now works.
Alt-3 Line Wrap now works.
Alt-4 Display NULL now works.

I was reading http://vt100.net and it looks like a "real" VT100 is
quite difficult to do right.  So I'm going to dedicate an entire point
release to the VT100 emulation, maybe just going with the emulator
from PuTTY or xterm.  I'll save it for nearly-last since it'll take
forever and a day to test.

May 13, 2003

Released 0.0.1.  Feels nice, this is my first actual file release of
any open-source project.

Alt-M Mail reader now works.  Alt-- Status Lines now works.

May 12, 2003

Got the download screen working for Zmodem receive.  Added keyboard
emulation and beep support, so now bash shell works properly.

May 9, 2003

Stubbed in the download/upload window.  Lunch, and then a quickie
parser for rz's output.  Current 'wc' output:

    523    5417   30848 colors.c
    120     595    4383 colors.h
     20      98     579 config.h
   1003    3199   28323 console.c
     56     286    2067 console.h
    194     641    4688 dialer.c
     55     262    1800 dialer.h
    787    2188   17782 emulation.c
     84     392    2853 emulation.h
    330    1206    9547 forms.c
     46     251    1716 forms.h
     53     264    1743 messages.c
     60     303    2104 messages.h
    416    1532   12364 options.c
     63     263    1949 options.h
    744    2359   20814 protocols.c
     94     367    2835 protocols.h
    610    1900   15252 qodem.c
    130     513    3809 qodem.h
    402    1330   10810 scrollback.c
     73     367    2644 scrollback.h
     88     427    2826 status.h
    123     629    4128 tty.c
     43     227    1514 tty.h
   6117   25016  187378 total

Hmmm...getting a segfault on a large file.  Doesn't seem to be any
problems in the read()/write(), yet somehow memory is getting
corrupted...hmm...  oh well weekend time.  I'm shooting for a 0.0.1
file release by this time next week.

May 8, 2003

Protocols are close but no cigar yet.  There, got it.  rz expects
blocking stdin/stdout/stderr.  I can transfer files successfully.  Now
to get the success to finish out and commit...  Yup, needed to catch
SIGPIPE and handle write() returning EPIPE when rz is gone.

May 7, 2003

Alt-C Clear screen.

Stubbed in the download/upload protocol stuff.  I think I've figured
out how to do rz/sz but damnit if it isn't behaving correctly.  When I
spawn the child process it freezes the main process and keyboard
handlers/menus and refreshers stop working.  After the child exits
things are OK.  Hmmm...maybe wait4() is blocking?  Yup.  So how can I
check if a child is still running without calling wait4()?  According
to comp.unix.programming FAQ kill(pid,0) is the way to go.  But
that'll be a tomorrow thing...as my girlfriend is demanding the rest
of the night.  G'night.

May 6, 2003

Put in a simple notify screen for the mode switches.  Alt-H works now,
via SIGHUP.  Next biggie: dialing directory.  Today's todo:
        Unplug laptop from the wall outlet
        Give my girlfriend some squeeze time :)

Got scrollback save and clear working.  Sourceforge CVS is
down....grrr...

Alt-U Scrollback toggle.

May 5, 2003

Screen dump works.  And forms.h has a terribly important piece of
missing documentation: form field data won't be visible until
form_driver(save_form, REQ_VALIDATION) has been called.  Grrr!  But
we've got a generic form that will be used for downloads and capture
too so I can't complain too much.

Did some other low-hanging fruit: linefeed on cr, 8th-bit strip,
half/full duplex, and some status line fixes.  The list of deviations
from Qmodem(tm) is growing: good.

May 3, 2003

Got the ANSI color portion working, now putting in the cursor
repositioning.  Ran into a bunch if crap with the curses COLOR_PAIR(0)
meaning white-on-black rather than black-on-black...grr...but I think
it works now.

More cleaning.  WIDTH and HEIGHT are now globals.  Also got
ansi_position() fixed so mc won't cause a core dump.  Woo.  Stubbed in
the options file functions.

May 2, 2003

Starting the ANSI emulation now.  There, now it strips all the codes
it can recognize.  (Just a subset of the full function but we can
easily add more later.)  This weekend's TODO is to actually modify the
program variables so color comes in again.

OS shell works.  Doorway works...sort of.

May 1, 2003

Scrollback buffer render is working, along with the info screen.
Yippee!  Today's TODO is to figure out how to get proper 8-bit
characters from the child tty all the way through curses, then we can
start worrying about ANSI.

April 30, 2003

Only one hour into it and I've got bash showing me ls!  Weehee!  All
that's left of core function is scrollback, which I was thinking about
last night as I went to sleep.  The render is pretty easy, but I'm
probably going to have to re-implement the storage at least twice.
For now I don't care, let's get something up.  Today's TODO:
        Be able to scroll the screen up one line when we are supposed to.

Wow.  Got it running, took a bunch of stupid one-off errors, but
believe it or not rendering the entire screen each time (and using
printw() for EVERY character!) is still much faster on the console
than a "real" bash prompt!  Heehee.

Put in a top-level call to terminal emulation.  Soon we'll have basic
ANSI support.  :) Time to commit...code size: 2169.

April 29, 2003

Got CVS access.  Screwed up the initial import of course, lots of crap
in the attic.  Grr, anyway...

Fixed up the program flow, got some screenshots.  Today's todo:
        Get the child TTY working
        Put in the console help screen, with unimplemented features
                in gray.

I'm about to call Santronics, the new owners of Mustang Software, to
see if we can copy the help text from Qmodem 4.6 test drive.  :) Their
web site claims the # is 305-248-3204...dialing...  press 2 for tech
support ...  Well gosh.  Looks like Santronics didn't acquire Qmodem,
that stayed with Mustang who got bought out by another company that
later went bankrupt.  So Qmodem is officially in dead-product-land,
not legal to rip off the help screens.  I'll need written permission
from whoever bought Mustang...  Back to coding, let's get the help
screen in.

Done with the help screen.  Let's play with TTYs now.  I think I see
what Eterm is doing, pretty clever.  Got it!  Tonight when I get home
I'll get the execlp() call in and start dumping the child's output to
the console.  Boo.  Ya.  :)

April 28, 2003

SourceForge accepted the project, now waiting for CVS to be alive and
well.  Until then I've got the ALT- and ^Z/^C working, put in the
global state variables, and have the beginnings of the console state
working.  Tomorrow I'll see about spawning a /bin/sh and piping its
output around, then we'll boot to DOS and get some screenshots of
Qmodem to model the menus after.

Patrick is listed as a developer, I'm really hoping he'll be
interested in creating the scripting support.  I figure a Lex/Yacc
kind of thing would be in order, assuming it doesn't lead to huge code
bloat.  But I've never enjoyed compilers and that seems more his
thing, so I'll defer design decisions if he does in fact come fully on
board.

April 27, 2003

Project started.  Registered on SourceForge, awaiting approval.  GPL.

Got autoconf working mostly, need to add the check for ncurses.
Initial window comes up, checks for keystrokes, exits on 'q'.

I don't think this will be an insanely difficult project.  I suspect
it will be useful for work within a week or two.  Once I have the
todos done below it'll just be adding the pluggable
telnet/ssh/etc. and then a bunch of UI work.

Immediate todos:
        GNU gettext support (I18n)
        Detect alt- and ctrl-
        Signal handlers to detect ^Z and ^C
        TTY handler
                Allocate TTY
                Fork child /bin/sh
                Capture output
