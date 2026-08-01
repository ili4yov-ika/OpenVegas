%define name openvegas
%define version 0.1.0
%define release 1
%define buildroot %{_tmppath}/%{name}-%{version}-%{release}-root

Summary: Open-source nonlinear video editor (VEGAS Pro–like)
Name: %{name}
Version: %{version}
Release: %{release}%{?dist}
License: GPL-3.0+
Group: Applications/Multimedia
Source0: %{name}-%{version}.tar.gz
URL: https://github.com/OpenVegas/OpenVegas
BuildRequires: cmake >= 3.16
BuildRequires: qt6-qtbase-devel
BuildRequires: qt6-qtsvg-devel
BuildRequires: qt6-qttools-devel
Requires: qt6-qtbase >= 6.8.0
Requires: qt6-qtsvg >= 6.8.0
BuildArch: x86_64

%description
OpenVegas — открытый кроссплатформенный видеоредактор (NLE),
ориентированный на workspace VEGAS Pro 22. Лицензия GNU GPL.

Основные возможности (MVP):
- Оболочка UI: media bin, preview, timeline, Master Bus
- Открытие проектов .veg (VegReader v0)
- Диалоги Welcome, Properties, Preferences, Trimmer

%prep
%setup -q

%build
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=%{_prefix} \
      ..
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
cd build
make install DESTDIR=%{buildroot}

%files
%defattr(-,root,root,-)
%{_bindir}/OpenVegas
%{_datadir}/applications/openvegas.desktop
%{_datadir}/icons/hicolor/*/apps/openvegas.png
%doc README.md LICENSE

%changelog
* Wed Jul 29 2026 OpenVegas contributors <maintainer@example.com> - 0.1.0-1
- Первый релиз OpenVegas (адаптация упаковки)
- MVP оболочка и VegReader v0
