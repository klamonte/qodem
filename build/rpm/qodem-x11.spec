Name:           qodem-x11
Version:        1.0.1
Release:        1%{?dist}
Summary:        Qodem terminal emulator and communications package.

Group:          Applications/Communications
License:        Public Domain
URL:            http://qodem.sourceforge.net/
Source0:        https://downloads.sourceforge.net/project/qodem/qodem/1.0.1/qodem-1.0.1.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Requires:       libXaw libXt
BuildRequires:  libXaw-devel libXt-devel


%description
Qodem is an open-source re-implementation of the Qmodem(tm)
shareware communications package, updated for more modern uses.
Major features include:
    * Unicode display: translation of CP437 (PC VGA), VT100 DEC
      Special Graphics characters, VT220 National Replacement
      Character sets, ATASCII, etc., to Unicode
    * Terminal interface conveniences: scrollback buffer, capture
      file, screen dump, dialing directory, keyboard macros, script
      support
    * Connection methods: serial, local shell, command line, telnet,
      ssh, rlogin, raw socket
    * Emulations: ANSI.SYS (including "ANSI music"), Avatar, VT52,
      VT100/102, VT220, Linux, XTerm, PETSCII (Commodore), and ATASCII
      (Atari)
    * Transfer protocols: Xmodem, Ymodem, Zmodem, and Kermit
    * External script support.  Any program that reads stdin and
      writes to stdout and stderr can be run as a script.
    * Host mode that provides a micro-BBS with messages, files, and
      sysop chat.
This version is built for X11 using PDCurses.

%prep
%setup -q -n qodem-1.0.1


%build
%configure --enable-x11 --disable-upnp --disable-ssh
make %{?_smp_mflags}


%install
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%{_mandir}/man1
cp docs/qodem-x11.1 $RPM_BUILD_ROOT%{_mandir}/man1/
# Create desktop file
mkdir -p %{buildroot}%{_datadir}/applications
cat > %{buildroot}%{_datadir}/applications/%{name}.desktop << EOL
[Desktop Entry]
Type=Application
Version=1.0
Name=qodem
Comment=%{summary}
Exec=%{name}
Icon=%{name}
Terminal=false
Categories=Network;Dialup;FileTransfer;Telephony;
EOL

# Install icons
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/{64x64,512x512}
install -pDm 0644 build/icons/qodem.png \
             %{buildroot}%{_datadir}/icons/hicolor/64x64/apps/%{name}.png
install -pDm 0644 build/icons/qodem-512.png \
             %{buildroot}%{_datadir}/icons/hicolor/512x512/apps/%{name}.png

desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop


%post
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :


%postun
if [ $1 -eq 0 ] ; then
    /bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null
    /usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi


%posttrans
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :


%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%{_bindir}/qodem-x11
%{_mandir}/man1/qodem-x11.1.gz
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%doc ChangeLog COPYING CREDITS README.md docs/TODO.md


%changelog
* Sun Jun 18 2017 Autumn Lamonte <lamonte at, users.sourceforge.net> - 1.0.1-1
* Sun Jun 18 2017 Autumn Lamonte <lamonte at, users.sourceforge.net> - 1.0.0-1
* Wed Apr 27 2016 Autumn Lamonte <lamonte at, users.sourceforge.net> - 1.0beta-1
* Sat May 19 2012 Autumn Lamonte <lamonte at, users.sourceforge.net> - 1.0alpha-1
* Sun Nov 30 2008 Jeff Gustafson <jeffgus at, fedoraproject.org> - 0.1.2-1
- Initial package creation
