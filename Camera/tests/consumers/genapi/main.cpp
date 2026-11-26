#include <camera/gentl/GenApiNodeMap.h>

int main() {
    camera::gentl::GenApiPort port({});
    return port.GetAccessMode() == GenApi::NA ? 0 : 1;
}
