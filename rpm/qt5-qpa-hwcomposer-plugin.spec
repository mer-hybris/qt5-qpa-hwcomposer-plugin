Name:       qt5-qpa-hwcomposer-plugin
Summary:    Qt 5 QPA hwcomposer plugin
Version:    5.1.0.2
Release:    1
Group:      Qt/Qt
License:    LGPLv2.1 with exception or GPLv3
URL:        http://github.com/mer-hybris/qt5-qpa-hwcomposer-plugin
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(libhardware)
%if 0%{?droid_has_no_libsync} == 0
# Define droid_has_no_libsync 1 in prjconf if the android-headers have
# no libsync (in older hw adaptations)
BuildRequires:  pkgconfig(libsync)
%endif
BuildRequires:  pkgconfig(hybris-egl-platform)
BuildRequires:  pkgconfig(android-headers)
BuildRequires:  pkgconfig(udev)
BuildRequires:  qt5-qteglfsdeviceintegration-devel
Requires:       qt5-plugin-platform-eglfs

%description
This package contains a Qt 5 QPA plugin using libhybris' Droid
hwcomposer for composing content onto the screen.

%prep
%setup -q

%build
export QTDIR=/usr/share/qt5
cd hwcomposer
# Qt is built with mesa, which has gl3.h. We're built with hybris which doesn't include gl3.h, so explicitly disable es3
%qmake5 DEFINES+=QT_NO_OPENGL_ES_3
make %{_smp_mflags}

%install
rm -rf %{buildroot}
cd hwcomposer
%qmake5_install

%files
%defattr(-,root,root,-)
%{_libdir}/qt5/plugins/egldeviceintegrations/libeglfs-hwcomposer-integration.so
%exclude %{_libdir}/cmake/Qt5Gui/Qt5Gui_EglFsHwcIntegrationPlugin.cmake
