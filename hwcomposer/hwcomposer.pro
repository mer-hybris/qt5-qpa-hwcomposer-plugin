TARGET = eglfs-hwcomposer-integration

QT += eglfsdeviceintegration-private

DEFINES += \
    QEGL_EXTRA_DEBUG \
    MESA_EGL_NO_X11_HEADERS

CONFIG += \
     egl \
     link_pkgconfig

# libhybris / droid integration
PKGCONFIG += android-headers libhardware hybris-egl-platform

packagesExist(hwcomposer-egl) {
    # hwcomposer-egl is shipped in recent versions of libhybris libEGL-devel
    # This also requires a fairly recent version of the android headers, as
    # it enables support for the new hwcomposer v1+ APIs in the plugin

    PKGCONFIG += hwcomposer-egl libsync
    DEFINES += HWC_PLUGIN_HAVE_HWCOMPOSER1_API
}

SOURCES += \
    eglfshwcintegration.cpp \
    eglfshwcwindow.cpp \
    hwcomposer_backend.cpp \
    hwcomposer_backend_v0.cpp \
    hwcomposer_backend_v10.cpp \
    hwcomposer_backend_v11.cpp \
    hwcomposer_screeninfo.cpp \
    main.cpp

HEADERS += \
    eglfshwcintegration.h \
    eglfshwcwindow.h \
    hwcinterface.h \
    hwcomposer_backend.h \
    hwcomposer_backend_v0.h \
    hwcomposer_backend_v10.h \
    hwcomposer_backend_v11.h \
    hwcomposer_screeninfo.h

QMAKE_LFLAGS += $$QMAKE_LFLAGS_NOUNDEF

PLUGIN_TYPE = egldeviceintegrations
PLUGIN_CLASS_NAME = EglFsHwcIntegrationPlugin
load(qt_plugin)
