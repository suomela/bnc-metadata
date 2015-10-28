#include <iostream>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <pugixml.hpp>

using namespace std::string_literals;
using namespace boost::filesystem;

bool process(path p) {
    std::cout << "." << std::flush;
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(p.c_str());
    if (!result) {
        std::cout << std::endl;
        std::cerr << p.string() << ": error: " << result.description() << std::endl;
        return false;
    }
    return true;
}

bool process_all(path p0) {
    bool ok = true;
    p0 /= "Texts";
    path xml(".xml");
    for (auto& p1 : directory_iterator(p0)) {
        if (!is_directory(p1)) {
            continue;
        }
        for (auto& p2 : directory_iterator(p1)) {
            if (!is_directory(p2)) {
                continue;
            }
            for (auto& p3 : directory_iterator(p2)) {
                if (!p3.path().has_extension()) {
                    continue;
                }
                if (p3.path().extension() != xml) {
                    continue;
                }
                if (!process(p3)) {
                    ok = false;
                }
            }
        }
    }
    return ok;
}

int main(int argc, const char** argv) {
    if (argc != 2) {
        std::cout << "usage: bnc-metadata BNC-ROOT" << std::endl;
        exit(1);
    }
    try {
        process_all(argv[1]);
    }
    catch (filesystem_error e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }
}
