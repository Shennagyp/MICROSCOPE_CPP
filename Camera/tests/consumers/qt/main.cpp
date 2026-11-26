#include <amscope/QtAmcamCamera.h>
#include <camera/qt/FrameStream.h>
#include <camera/qtwidgets/FrameView.h>
#include <QApplication>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    camera::qt::FrameStream stream;
    camera::qtwidgets::FrameView view;
    amscope::QtAmcamCamera adapter;
    return adapter.state() == amscope::CameraState::Closed ? 0 : 1;
}
