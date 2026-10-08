# tests/consumer_qmake/app.pro — BLD-07: a consumer project with exactly one include(...) line
# pulling in the whole library (core + device) through the .pri files alone. mc_device.pri adds
# QtCore/Network/SerialPort, so linking this app also proves MSVC 2026 links the Qt kit
# (msvc2022_64 for Qt 6, msvc2019_64 for Qt 5.15).
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle

include(../../mc_protocol.pri)

SOURCES += main.cpp
