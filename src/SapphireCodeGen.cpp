#include <mimalloc-new-delete.h>
#include "codegen/Application.h"

int main(int argc, const char **argv) {
    sapphire::codegen::Application app(argc, argv);
    return app.run();
}