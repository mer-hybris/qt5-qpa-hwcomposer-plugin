# Build either with Qt5 or Qt6
%bcond qt5 %{without qt6}
%bcond qt6 %{without qt5}

%if %{with qt5}
%global qt_version_major 5
%global qmake %{qmake5}
%global qmake_install %{qmake5_install}
%endif
%if %{with qt6}
%global qt_version_major 6
%global qmake %{qmake6}
%global qmake_install %qmake6_install
%endif


Name:       qt%{qt_version_major}-qpa-hwcomposer-plugin
Summary:    Qt %{qt_version_major} QPA hwcomposer plugin
#FIXME
Version:    5.6.2.1
Release:    1
License:    LGPLv2 with exception or GPLv3 or Qt Commercial
URL:        http://github.com/mer-hybris/qt5-qpa-hwcomposer-plugin
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt%{qt_version_major}Core)
BuildRequires:  pkgconfig(Qt%{qt_version_major}Gui)
BuildRequires:  pkgconfig(Qt%{qt_version_major}DBus)
BuildRequires:  qt%{qt_version_major}-qtplatformsupport-devel >= 5.6.0
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(libhardware)
%if 0%{?droid_has_no_libsync} == 0
# Define droid_has_no_libsync 1 in prjconf if the android-headers have
# no libsync (in older hw adaptations)
BuildRequires:  pkgconfig(libsync)
%endif
BuildRequires:  pkgconfig(hybris-egl-platform)
BuildRequires:  pkgconfig(android-headers)
BuildRequires:  qt%{qt_version_major}-qtwayland-wayland_egl-devel
BuildRequires:  wayland-devel
BuildRequires:  pkgconfig(udev)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(mtdev)

%description
This package contains a Qt %{qt_version_major} QPA plugin using libhybris' Droid
hwcomposer for composing content onto the screen.

%prep
%autosetup -n %{name}-%{version}/hwcomposer

%build
export QTDIR=/usr/share/qt%{qt_version_major}
%qmake CONFIG+=enable-systrace
%make_build

%install
%qmake_install

%files
%defattr(-,root,root,-)
%{_libdir}/qt%{qt_version_major}/plugins/platforms/libhwcomposer.so
