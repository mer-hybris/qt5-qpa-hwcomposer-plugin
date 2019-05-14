TARGET = hwcomposer

PLUGIN_TYPE = platforms
PLUGIN_CLASS_NAME = QEglFShwcIntegrationPlugin
load(qt_plugin)
load(configure)

INCLUDEPATH += .
DEPENDPATH += .

SOURCES += main.cpp

SOURCES += hwcomposer_context.cpp
HEADERS += hwcomposer_context.h

SOURCES += hwcomposer_screeninfo.cpp
HEADERS += hwcomposer_screeninfo.h

SOURCES += hwcomposer_backend.cpp
HEADERS += hwcomposer_backend.h

SOURCES += hwcomposer_backend_v0.cpp
HEADERS += hwcomposer_backend_v0.h

SOURCES += hwcomposer_backend_v10.cpp
HEADERS += hwcomposer_backend_v10.h

SOURCES += hwcomposer_backend_v11.cpp
HEADERS += hwcomposer_backend_v11.h

versionAtLeast(QT_MINOR_VERSION, 8) {
    QT += core-private gui-private egl_support-private waylandcompositor-private dbus fontdatabase_support-private eventdispatcher_support-private theme_support-private
} else {
    QT += core-private compositor-private gui-private platformsupport-private dbus
}

enable-sensors {
    QT += sensors
    DEFINES += WITH_SENSORS
}

enable-systrace {
    DEFINES += WITH_SYSTRACE
}

DEFINES += QEGL_EXTRA_DEBUG
CONFIG += egl qpa/genericunixfontdatabase

CONFIG += link_pkgconfig

# For linking against libQt5PlatformSupport.a
PKGCONFIG_PRIVATE += libudev glib-2.0 mtdev

# libhybris / droid integration
PKGCONFIG += android-headers libhardware hybris-egl-platform

packagesExist(hwcomposer-egl) {
    # hwcomposer-egl is shipped in recent versions of libhybris libEGL-devel
    # This also requires a fairly recent version of the android headers, as
    # it enables support for the new hwcomposer v1+ APIs in the plugin

    PKGCONFIG += hwcomposer-egl libsync
    DEFINES += HWC_PLUGIN_HAVE_HWCOMPOSER1_API
}

qtCompileTest(hwcomposer2) {
    PKGCONFIG += libhwc2
    DEFINES += HWC_PLUGIN_HAVE_HWCOMPOSER2_API
    SOURCES += hwcomposer_backend_v20.cpp
    HEADERS += hwcomposer_backend_v20.h
}

# Avoid X11 header collision
DEFINES += MESA_EGL_NO_X11_HEADERS

SOURCES +=  $$PWD/qeglfsintegration.cpp \
            $$PWD/qeglfswindow.cpp \
            $$PWD/qeglfsbackingstore.cpp \
            $$PWD/qeglfsscreen.cpp \
            $$PWD/qeglfscontext.cpp

HEADERS +=  $$PWD/qeglfsintegration.h \
            $$PWD/qeglfswindow.h \
            $$PWD/qeglfsbackingstore.h \
            $$PWD/qeglfsscreen.h \
            $$PWD/qeglfscontext.h

QMAKE_LFLAGS += $$QMAKE_LFLAGS_NOUNDEF
