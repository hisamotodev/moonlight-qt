QT -= core gui

TARGET = imgui
TEMPLATE = lib

# Build a static library
CONFIG += staticlib

# Disable warnings (vendored third-party code)
CONFIG += warn_off

# Include global qmake defs
include(../globaldefs.pri)

win32 {
    contains(QT_ARCH, i386) {
        INCLUDEPATH += $$PWD/../libs/windows/include/x86 $$PWD/../libs/windows/include/x86/SDL2
    }
    contains(QT_ARCH, x86_64) {
        INCLUDEPATH += $$PWD/../libs/windows/include/x64 $$PWD/../libs/windows/include/x64/SDL2
    }
    contains(QT_ARCH, arm64) {
        INCLUDEPATH += $$PWD/../libs/windows/include/arm64 $$PWD/../libs/windows/include/arm64/SDL2
    }

    INCLUDEPATH += $$PWD/../libs/windows/include
}
macx {
    INCLUDEPATH += $$PWD/../libs/mac/include $$PWD/../libs/mac/include/SDL2
}
unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += sdl2
}

IMGUI_DIR = $$PWD/imgui
SOURCES += \
    $$IMGUI_DIR/imgui.cpp \
    $$IMGUI_DIR/imgui_draw.cpp \
    $$IMGUI_DIR/imgui_tables.cpp \
    $$IMGUI_DIR/imgui_widgets.cpp \
    $$IMGUI_DIR/misc/cpp/imgui_stdlib.cpp \
    $$IMGUI_DIR/backends/imgui_impl_sdl2.cpp \
    $$IMGUI_DIR/backends/imgui_impl_sdlrenderer2.cpp
HEADERS += \
    $$IMGUI_DIR/imgui.h \
    $$IMGUI_DIR/imconfig.h \
    $$IMGUI_DIR/imgui_internal.h \
    $$IMGUI_DIR/misc/cpp/imgui_stdlib.h \
    $$IMGUI_DIR/backends/imgui_impl_sdl2.h \
    $$IMGUI_DIR/backends/imgui_impl_sdlrenderer2.h
INCLUDEPATH += \
    $$IMGUI_DIR \
    $$IMGUI_DIR/backends \
    $$IMGUI_DIR/misc/cpp
