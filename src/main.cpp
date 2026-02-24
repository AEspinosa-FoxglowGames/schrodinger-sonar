#include "app/app.h"
#include <iostream>

int main() {
    App app;
    if (!app.Init()) {
        std::cerr << "App init failed\n";
        return 1;
    }

    app.Run();
    app.Shutdown();
    return 0;
}
