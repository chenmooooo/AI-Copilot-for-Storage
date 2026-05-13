#include "ui/App.h"

int main() {
    App app;
    if (!app.init(1600, 1000, "AI Copilot for Storage")) {
        return 1;
    }

    app.run();
    app.shutdown();
    return 0;
}
