Name:       qt5-qpa-hwcomposer-plugin
Summary:    Qt 5 QPA hwcomposer plugin
Version:    5.6.2.1
Release:    1
License:    LGPLv2 with exception or GPLv3 or Qt Commercial
URL:        http://github.com/mer-hybris/qt5-qpa-hwcomposer-plugin
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  qt5-qtplatformsupport-devel >= 5.6.0
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
BuildRequires:  qt5-qtwayland-wayland_egl-devel
BuildRequires:  wayland-devel
BuildRequires:  pkgconfig(udev)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(mtdev)

%description
This package contains a Qt 5 QPA plugin using libhybris' Droid
hwcomposer for composing content onto the screen.

%prep
%autosetup -n %{name}-%{version}

%build
export QTDIR=/usr/share/qt5
%qmake5 CONFIG+=enable-systrace
%make_build

%install
%qmake5_install

# doesn't exist on Qt 5.1, we don't currently care about this for 5.2
rm -f %{buildroot}/%{_libdir}/cmake/Qt5Gui/Qt5Gui_QEglFSIntegrationPlugin.cmake
rm -f %{buildroot}/%{_libdir}/cmake/Qt5Gui/Qt5Gui_QEglFShwcIntegrationPlugin.cmake

%files
%defattr(-,root,root,-)
%license LICENSE.LGPLv21 LGPL_EXCEPTION.txt LICENSE.GPLv3
%{_libdir}/qt5/plugins/platforms/libhwcomposer.so
