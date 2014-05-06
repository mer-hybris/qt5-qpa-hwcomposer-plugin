Name:       qt5-qpa-hwcomposer-plugin
Summary:    Qt 5 QPA hwcomposer plugin
Version:    5.1.0.2
Release:    1
Group:      Qt/Qt
License:    LGPLv2.1 with exception or GPLv3
URL:        http://github.com/mer-hybris/qt5-qpa-hwcomposer-plugin
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5PlatformSupport)
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
%setup -q

%build
export QTDIR=/usr/share/qt5
cd hwcomposer
%qmake5
make %{_smp_mflags}

%install
rm -rf %{buildroot}
cd hwcomposer
%qmake5_install

# doesn't exist on Qt 5.1, we don't currently care about this for 5.2
rm -f %{buildroot}/usr/lib/cmake/Qt5Gui/Qt5Gui_QEglFSIntegrationPlugin.cmake

%files
%defattr(-,root,root,-)
%{_libdir}/qt5/plugins/platforms/libhwcomposer.so
