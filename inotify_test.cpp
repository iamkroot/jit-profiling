#include <inotify-cpp/NotifierBuilder.h>
#include <filesystem>
#include <fmt/core.h>
#include <iostream>
#include <fstream>

int main() {
    std::filesystem::path dir{"/tmp/.lldump"};
    std::filesystem::create_directories(dir);

    auto createDirNotif = [&](inotify::Notification notif) {
        switch (notif.event) {
            case inotify::Event::close_write: {
                auto rel = relative(notif.path, dir);
                if (rel.extension() == ".lldump") {
                    fmt::print("Wrote to lldump file: {}\n", rel.string());
                    std::ifstream f{notif.path};
                    
                }
            }
            case inotify::Event::create:
            default:{
                fmt::print("notif! {} ", notif.path.string());
                std::cout << notif.event << "\n";
            }
        }
    };
    auto events = {inotify::Event::create | inotify::Event::is_dir, inotify::Event::all};
    auto notifier = inotify::BuildNotifier().watchPathRecursively(dir).onEvents(events, createDirNotif);
    notifier.run();
    return 0;
}