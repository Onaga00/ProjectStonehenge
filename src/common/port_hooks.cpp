#include "common/port_hooks.h"

namespace ac04::port {

MenuOptions& Menu() {
    static MenuOptions options;
    return options;
}

RenderMarks& Render() {
    static RenderMarks marks;
    return marks;
}

BootOptions& Boot() {
    static BootOptions options;
    return options;
}

InputHooks& Input() {
    static InputHooks hooks;
    return hooks;
}

}  // namespace ac04::port
