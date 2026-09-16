#include <chrono>
#include <fstream>
#include <thread>

int main() {
    std::ofstream launches{"os_info_launches.txt", std::ios::app};
    launches << "launch\n";
    launches.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    return 17;
}
