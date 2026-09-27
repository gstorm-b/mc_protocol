# mc_core.pri — the core library only: no Qt, no mock, no device layer.
# Consumer usage: include($$PWD/third_party/mc_protocol/mc_core.pri)
!defined(MC_CORE_PRI_INCLUDED, var) {
MC_CORE_PRI_INCLUDED = 1

CONFIG += c++17

INCLUDEPATH += \
    $$PWD/include \
    $$PWD/src

HEADERS += \
    $$PWD/include/mc/version.h \
    $$PWD/include/mc/core/types.h \
    $$PWD/include/mc/core/result.h \
    $$PWD/include/mc/core/device.h \
    $$PWD/include/mc/core/frame_config.h \
    $$PWD/include/mc/core/request.h \
    $$PWD/include/mc/core/limits.h \
    $$PWD/include/mc/core/convert.h \
    $$PWD/include/mc/core/log.h \
    $$PWD/include/mc/core/protocol.h \
    $$PWD/src/core/model/limits_table.h \
    $$PWD/src/core/protocol/hexascii.h \
    $$PWD/src/core/protocol/sumcheck.h \
    $$PWD/src/core/protocol/field_codec.h \
    $$PWD/src/core/protocol/device_encode.h \
    $$PWD/src/core/protocol/command_qna.h \
    $$PWD/src/core/protocol/frame_3e.h

SOURCES += \
    $$PWD/src/core/version.cpp \
    $$PWD/src/core/model/device_table.cpp \
    $$PWD/src/core/model/device_parse.cpp \
    $$PWD/src/core/model/frame_config.cpp \
    $$PWD/src/core/model/validate.cpp \
    $$PWD/src/core/model/limits_table.cpp \
    $$PWD/src/core/model/chunk.cpp \
    $$PWD/src/core/model/convert.cpp \
    $$PWD/src/core/model/log.cpp \
    $$PWD/src/core/protocol/hexascii.cpp \
    $$PWD/src/core/protocol/sumcheck.cpp \
    $$PWD/src/core/protocol/device_encode.cpp \
    $$PWD/src/core/protocol/command_qna.cpp \
    $$PWD/src/core/protocol/frame_3e.cpp \
    $$PWD/src/core/protocol/protocol.cpp

}
