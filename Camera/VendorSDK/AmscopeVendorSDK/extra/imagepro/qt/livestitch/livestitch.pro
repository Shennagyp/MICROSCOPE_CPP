QT += core gui widgets
SOURCES += livestitch.cpp
HEADERS += livestitch.h
LIBS += -L$$PWD/./ -lamcam -limagepro
#CONFIG += console
