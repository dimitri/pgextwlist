Name:           %{?package_prefix}pgextwlist
Version:        %{major_version}
Release:        %{minor_version}%{?dist}
Summary:        PostgreSQL Extension Whitelist extension

Group:          Applications/Databases
License:        PostgreSQL
Requires:       %{?package_prefix}%{!?package_prefix:postgresql-}server
BuildRequires:  %{?package_prefix}%{!?package_prefix:postgresql-}devel
Source0:        pgextwlist-rpm-src.tar.gz

%description
This extension implements extension whitelisting, and will actively prevent
users from installing extensions not in the provided list.  Also, this
extension implements a form of sudo facility in that the whitelisted
extensions will get installed as if superuser.  Privileges are dropped
before handing the control back to the user.

%prep
%setup -q -n pgextwlist

%build
sed '/^DOCS/d' -i Makefile  # don't install README.md in pgsql/contrib
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%doc README.md
%{?pkglibdir}%{!?pkglibdir:%{_libdir}/pgsql}/

%changelog
* Sun Oct 15 2017 Christoph Berg <myon@debian.org> - 1.6-0
- New upstream version.
* Tue Sep 27 2016 Christoph Berg <myon@debian.org> - 1.5-0
- New upstream version.
* Tue Feb 02 2016 Christoph Berg <myon@debian.org> - 1.4-0
- New upstream version.
* Tue Jul 14 2015 Oskari Saarenmaa <os@ohmu.fi> - 1.3-0
- Initial.
