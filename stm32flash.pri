CONFIG += c++11

SRC_PATH = $$PWD

INCLUDEPATH += $$SRC_PATH

HEADERS += \
    $$SRC_PATH/init.h \
    $$SRC_PATH/port.h \
    $$SRC_PATH/serial.h \
    $$SRC_PATH/stm32.h \
    $$SRC_PATH/utils.h \
    $$SRC_PATH/parsers/binary.h \
    $$SRC_PATH/parsers/hex.h \
    $$SRC_PATH/parsers/parser.h \

SOURCES += \
    $$SRC_PATH/dev_table.c \
    $$SRC_PATH/i2c.c \
    $$SRC_PATH/init.c \
    $$SRC_PATH/port.c \
    $$SRC_PATH/serial_common.c \
    $$SRC_PATH/serial_platform.c \
    $$SRC_PATH/serial_posix.c \
    $$SRC_PATH/serial_w32.c \
    $$SRC_PATH/stm32.c \
    $$SRC_PATH/utils.c \
    $$SRC_PATH/parsers/binary.c \
    $$SRC_PATH/parsers/hex.c \

include($$PWD/stm32flasher/stm32flasher.pri)
