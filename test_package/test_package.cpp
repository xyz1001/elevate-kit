#include "elevate_kit/elevate_kit.h"

#include <functional>

int main() {
    elevate_kit::SetApplicationId("com.example.elevate_kit");
    elevate_kit::RegisterTask("identity", [](const nlohmann::json& params) {
        return params;
    });
    return 0;
}
