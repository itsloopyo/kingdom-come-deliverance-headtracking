#include <iostream>

int RunViewInjectionTests();
int RunAimProjectionTests();
int RunPoseChannelTests();
int RunConfigTests();
int RunBuildProfileTests();
int RunGameStateTests();

int main()
{
    std::cout << "Kingdom Come: Deliverance Head Tracking Tests\n";
    std::cout << "============================================\n";

    int failures = 0;
    failures += RunViewInjectionTests();
    failures += RunAimProjectionTests();
    failures += RunPoseChannelTests();
    failures += RunConfigTests();
    failures += RunBuildProfileTests();
    failures += RunGameStateTests();

    if (failures == 0)
    {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
